from pathlib import Path
from json import loads
from re import compile as regex
from enum import IntEnum


class BindingsFile:
	def __init__(self):
		fpath = Path('bindings.cpp')
		self.f = open(fpath, 'r+')
		p = 0
		SEPERATOR = '/* __PROCEDURALY_GENERATED__ */'
		while line := self.f.readline():
			if SEPERATOR in line: break
			p = self.f.tell() + len(SEPERATOR)+1
		self.f.truncate(p)
		self.f.seek(p)
		#print('bindings file open')

	def __iadd__(self, s:str)->BindingsFile:
		self.f.write(f' \t{s}\n')
		return self

	def __del__(self):
		#print('bindings file released')=
		self.__iadd__('\n}') # close module
		self.f.close()

binds = BindingsFile()
binds += '\n'


api_json = loads(Path('../public/steam/steam_api.json').read_text())
struct_json = api_json['callback_structs'] + api_json['structs']


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

def define_class(class_json:dict):
	global binds
	class_ = class_json['struct']
	bName = class_.lower()
	simple_class_ = class_[:-2] if class_.endswith('_t') else class_

	binds += f'py::class_<{class_}> {bName}(m, "{simple_class_}");'

	# TODO: take method definitions into account
	binds += f'{bName}.def(py::init<>())'
	
	fields = [ # name, actualtype, sizebracketed
		parse_type(f['fieldname'], f['fieldtype'])
		 for f in class_json['fields']
		if f.get('private') == None # if public
	]

	for fName, actualtype, size in fields:
		simple_fName = fName[2:] if fName.startswith('m_') else fName
		if actualtype == 'char' and size:
			binds += f'''	.def_property("{simple_fName}",
			[](const {class_}& x) {{ return get_char_array(x.{fName}); }},
			[]({class_}& x, const std::string& s) {{ return set_char_array(x.{fName}, s); }})'''
			continue

		binds += f'\t.def_readwrite("{simple_fName}", &{class_}::{fName})'
	binds += ';'


def find_class_json(name:str):
	return [ d for d in struct_json if d['struct'] == name ][0]

if True:
	define_class(find_class_json('MatchMakingKeyValuePair_t'))
	define_class(find_class_json('FriendGameInfo_t'))










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


for enum in api_json['enums']:
	define_enum(enum)


## typedefs ---------------------------------
# NOTE: requires some forward declarations


for td in api_json['typedefs']:
	typename = td['typedef']
	typename, actualtype, sizebracketed = parse_type(typename, td['type'])
	#print(typename, actualtype, sizebracketed)
	typedef(typename, f'{actualtype} {typename}{sizebracketed}')


## structs ---------------------------------
# TODO: create wrapper python classes using type()
# NOTE: not sure what to do with callback_structs' callback_id

def define_struct(struct_json:dict):
	structname = struct_json['struct']
	if structname in defined_types: return
	
	# forward declaration for structs thatuse references to themselves
	#typedef(structname, f'struct {structname} {structname}')

	fields = [ ( # public, name, actualtype, sizebracketed
		f.get('private') == None, *parse_type(f['fieldname'], f['fieldtype'])
		) for f in struct_json['fields']
	]
	#print(*fields)

	partial = any(map(lambda _public: not _public[0], fields))

	declaration = f"""struct {structname} {{{ ''.join((
	f'\n\t{actualtype} {name}{sizebracketed};'
	for public, name, actualtype, sizebracketed in fields if public
	))
}{ '\n\t...; // allow partial declarations' if partial else ''}
}}"""

	extern(structname,  declaration)

if False:
	for st in api_json['callback_structs'] + api_json['structs']:
		define_struct(st)

del binds