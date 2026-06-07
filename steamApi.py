from pathlib import Path
from json import loads
from re import compile as regex
from enum import IntEnum


class BindingsFile:
	def __init__(self):
		fpath = Path('bindings.cpp')
		self.f = open(fpath, 'r+')
		#print('bindings file open')

	def reset(self):
		p = 0
		SEPERATOR = '/* __PROCEDURALY_GENERATED__ */'
		while line := self.f.readline():
			if SEPERATOR in line: break
			p = self.f.tell() + len(SEPERATOR)+1
		self.f.truncate(p)
		self.f.seek(p)

	def __iadd__(self, s:str)->BindingsFile:
		self.f.write(f' \t{s}\n')
		return self

	def __del__(self):
		#print('bindings file released')
		self.__iadd__('\n}') # close module
		self.f.close()

binds = BindingsFile()
binds.reset()
binds += '\n'


api_json = loads(Path('../public/steam/steam_api.json').read_text())
struct_json = api_json['callback_structs'] + api_json['structs']

policies = {

	'SteamNetworkingConfigValue_t' : {
	# SteamNetworkingConfigValue_t union member is weird, renaming it fixes compile error
		'm_int64': 'm_val',
	},

	# SteamNetworkingMessage_t has no dextructor, expects you to call release
	# use this type as a proxy for python
	'SteamNetworkingMessage_t' : { 'py_type' : 'ReleasePtr<SteamNetworkingMessage_t>', },

	# SteamParamStringArray is only struct with const char **
	# we have a custom type_caster for it
	'const char **' : 'py_skip',

	# we have another way to bind relase/free
	'void (*m_pfnFreeData)(SteamNetworkingMessage_t *)' : 'py_skip',
	'void (*m_pfnRelease)(SteamNetworkingMessage_t *)'  : 'py_skip',
	
	'method_renames' : {
		#'ToString'   : '__str__', # not 1 to 1
		'operator=='  : '__eq__',
		'operator!='  : '__ne__',
		'operator<'   : '__lt__',
		'operator>'   : '__gt__',
		'operator<='  : '__le__',
		'operator>='  : '__ge__',
		'operator+'   : '__add__',
		'operator-'   : '__sub__',
		'operator*'   : '__mul__',
		'operator/'   : '__truediv__',
		'operator%'   : '__mod__',
		'operator+='  : '__iadd__',
		'operator-='  : '__isub__',
		'operator*='  : '__imul__',
		'operator/='  : '__itruediv__',
		'operator%='  : '__imod__',
		#'operator//' : '__floordiv__',	 # not a C++ operator
		#'operator//= : '__ifloordiv__', # not a C++ operator
		#'operator**' : '__pow__',		 # not a C++ operator
		#'operator**= : '__ipow__',		 # not a C++ operator
	},
}
method_renames = policies['method_renames']

# to determine whether to forward declare types
# ik const, unsigned and * are not actually types but this simplifies things
known_types = {'const', 'unsigned', 'signed', '*', '**', 'void', 'char', 'short', 'long', 'int', 'float', 'double' }

# types that have been properly defined
# for structs, that means full declaration, with struct members
defined_types = set()


array_regex = regex('(\\w+)\\s*\\[([0-9]+)\\]')
def parse_type(name, typename:str)->tuple:
	# functions / callbacks
	if '(*)' in typename:
		params = typename.split('(*)')[-1][1:-1].split(',')
		#print(params)

		# forward declare unknown arguments
		for param in params:
			parse_type('', param)

		return '', typename.replace("(*)", f"(*{name})"), ''
	
	arr_size = ''
	# arrays
	if '['  in typename:
		typename, arr_size = array_regex.match(typename).groups()

	# inner types
	if '::' in typename:
		typename = typename.split('::')[-1]

	# forward declare unknown types
	#for t in typename.strip().split(' '):
	#	if not t in known_types:
	#		typedef(t, f'struct {t} {t}')

	return name, typename, arr_size


#scalars = {'signed', 'unsigned', 'char', 'short', 'int', 'long', 'float', 'double'}
#for types_ in api_json['typedefs']:
#	name = types_['typedef']
#	actualtype = types_['type']
#	if all(map(lambda s: s in scalars, actualtype.split(' '))):
#		scalars.add(name)

def define_class(class_json:dict):
	global binds
	class_ = class_json['struct']
	class_policy = policies.get(class_) or {}

	py_class = class_[:-2] if class_.endswith('_t') else class_
	bName = f'_{class_.lower()}_'

	py_type = class_policy.get('py_type')
	
	binds += f'py::class_<{class_}{f", {py_type}" if py_type else ""}> {bName}(m, "{py_class}");'
	binds += bName

	methods = {m['methodname']:m for m in mtds} if (mtds := class_json.get('methods')) else {}
	has_constructor = not 'Release' in methods

	if has_constructor:
		binds += f'\t.def(py::init<>())'

	# TODO: make this machinery common with interfaces
	for mName, method_json in methods.items():
		if mName in ('Construct','Release'): continue # already defined above

		mReference = method_json.get('methodname_flat') or f'{class_}::{mName}'
		
		params = { p['paramname']:p for p in method_json['params']}
		params_str = ''.join( (f', py::arg("{k}")' for k in params.keys()) ) 
		
		# TODO : wrap return_type if necessary
		return_type = method_json['returntype']

		py_mName = method_renames.get(mName) or mName
		binds +=  f'\t.def("{py_mName}", &{mReference}{params_str})'
	
	fields = [ # name, actualtype, sizebracketed
		parse_type(f['fieldname'], f['fieldtype'])
		 for f in class_json['fields']
		if f.get('private') == None # if public
	]

	for fName, actualtype, size in fields:

		if policies.get(actualtype) == 'py_skip':
			#print('ignoring', py_class, fName, actualtype, size)
			continue

		if class_policy: fName = class_policy.get(fName) or fName
		
		py_fName = fName[2:] if fName.startswith('m_') else fName

		if '(*m_' in actualtype:
			print('ignoring method pointer', py_class, actualtype)
			continue

		if actualtype == 'char' and size:
			binds += f'''	.def_property("{py_fName}",
			[](const {class_}& x) {{ return get_char_array(x.{fName}); }},
			[]({class_}& x, const std::string& s) {{ set_char_array(x.{fName}, s); }})'''
			continue
		
		if size:
			#binds += f'''	.def_property("{py_fName}",
			#[](const {class_}& x) {{ return get_fixed_array(x.{fName}); }},
			#[]({class_}& x, const std::vector<{actualtype}>& values) {{ set_fixed_array(x.{fName}, values); }})'''

			binds += f'''	.def_property_readonly("{py_fName}",
			[]({class_}& x) {{ return FixedArrayView<{actualtype}, {size}>{{ x.{fName} }}; }},
			py::return_value_policy::reference_internal)'''

			# allows use of array view
			arraybinds.add( (actualtype, size) )

			continue

		binds += f'\t.def_readwrite("{py_fName}", &{class_}::{fName})'
	binds += ';'



def find_class_json(name:str):
	return [ d for d in struct_json if d['struct'] == name ][0]


arraybinds = set()
if False:
	define_class(find_class_json('MatchMakingKeyValuePair_t'))
	define_class(find_class_json('FriendGameInfo_t'))
	define_class(find_class_json('FriendsEnumerateFollowingList_t'))
	define_class(find_class_json('SteamNetworkingMessage_t'))
else:
	classes = api_json['callback_structs'] + api_json['structs']
	for cls in classes:
		define_class(cls)
		#if cls['struct'] == 'SteamNetworkingMessage_t': break
for tp,sz in sorted(arraybinds): # sorted is to have deterministic order
	binds += f'bind_fixed_array_view<{tp}, {sz}>(m, "{tp}array{sz}");'










def typedef(name:str, declaration:str)->None:
	global known_types
	known_types.add(name)
	declaration = f'typedef {declaration};\n'

def extern(name:str, declaration:str)->None:
	global defined_types
	defined_types.add(name)
	declaration = f'extern {declaration};\n'

def add_global(name:str, clss:object)->None:
	globals()[name] = cls



## Enums ---------------------------------
# NOTE: we already create the classes that contain the enums

# NOTE: some interfaces also define enums
# TODO?: use fqname to add to relevant interface (ex: "fqname": "ISteamHTMLSurface::EHTMLMouseButton")

def define_enum(enum_json:dict, interface:dict|None=None)->None:
	
	enumname = enum['enumname']
	values = ( (d['name'], int(d['value'])) for d in enum_json['values'] )

	# typedef as int and enum values as constants
	typedef(enumname, f'int {enumname}')
	for name, value in values:
		declaration = f'const {enumname} {name} = {value};'

	# create a class with the values and names
	# TODO: check that steam api accepts those
	cls = IntEnum(
		enumname,
		{ name.replace(enumname+'_', ''):value for name, value in values },
		module=__name__
	)
	#print(cls)
	
	# add to globals if not part of interface
	#defined_types.add(enumname)
	(interface if interface else globals())[enumname] = cls


#for enum in api_json['enums']:
#	define_enum(enum)


## typedefs ---------------------------------
# NOTE: requires some forward declarations


#for td in api_json['typedefs']:
#	typename = td['typedef']
#	typename, actualtype, sizebracketed = parse_type(typename, td['type'])
#	#print(typename, actualtype, sizebracketed)
#	typedef(typename, f'{actualtype} {typename}{sizebracketed}')



del binds