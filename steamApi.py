from pathlib import Path
from json import loads
from re import compile as regex
from enum import IntEnum
from copy import copy
from itertools import batched as chunks

BUILD_DIR = './build/'
bindings_main = f'{BUILD_DIR}bindings.cpp'
all_bind_files = [ bindings_main ]

class BindingsFile:

	def __init__(self, max_defs_per_file:int=30):
		self.max_defs_per_file = max_defs_per_file
		self.file_count = 0
		self.current_lines = 0
		self.impl_file = None
		#self.newFile()

	def newFile(self, header:str = '')->None:
		bind_method = f'bind_{self.file_count}'
		self.file_count += 1
		self.current_lines = 0

		fPath = f'{BUILD_DIR}{bind_method}.cpp'
		all_bind_files.append(fPath)
		self.impl_file = open(Path(fPath), 'w')
		self.impl_file.write(f'''
#include "../common.h"

{header}

void {bind_method}(py::module_& m) {{
''')

	def __iadd__(self, s:str)->'BindingsFile':
		self.impl_file.write(f' \t{s}\n')
		return self

	def endFile(self)->None:
		self.impl_file.write('\n}') # close function
		self.impl_file.close()

	def __del__(self)->None:
		#self.endFile()

		forward = '\n'.join( ( f'void bind_{i}(py::module_& m);' for i in range(self.file_count) ) )

		calls = ''.join( ( f'\tbind_{i}(m);\n' for i in range(self.file_count) ) )

		calls += '\n'.join( (f'\tbind_fixed_array_view<{tp}, {sz}>(m, "{tp}array{sz}");' for tp,sz in sorted(arraybinds) ) )

		with open(Path(bindings_main), 'w') as main: 
			main.write(f'''
#include "../additional_implementations.h"

namespace py = pybind11;

{forward}

PYBIND11_MODULE(steamworks, m) {{
	bind_init(m);

{calls}

	bind_response_adapters(m);
}}
''')

binds = BindingsFile()


api_json = loads(Path('../public/steam/steam_api.json').read_text())

policies = {
	
	# field renames
	'SteamNetworkingConfigValue_t' : {
	# SteamNetworkingConfigValue_t union member is weird, renaming it fixes compile error
		'm_int64': 'm_val',
	},

	'type_conversion' : {
		# SteamNetworkingMessage_t has no destructor, expects you to call release
		# use this type as a proxy to allow python gc to manage lifetime
		'SteamNetworkingMessage_t' : 'ReleasePtr<SteamNetworkingMessage_t>',
		#'ISteamMatchmakingPingResponse': 'PingResponseAdapter',

		# NOTE: when binding cpp style methods, these conversions are not needed
		'CSteamID' : 'uint64_steamid',
		'CGameID' : 'uint64_gameid',
	},

	# field types to skip / ignore
	'field_types_skipped': {
		# SteamParamStringArray is only struct with const char **
		# we have a custom type_caster for it
		'const char **',
		# we have another way to bind relase/free
		'void (*m_pfnFreeData)(SteamNetworkingMessage_t *)',
		'void (*m_pfnRelease)(SteamNetworkingMessage_t *)',
	},

	# no constructor
	'no_constructor' : { 'SteamNetworkingMessage_t', },

	'constructible_interfaces' : {'ISteamApps', 'ISteamUserStats', 'ISteamTimeline', 'ISteamRemoteStorage', 'ISteamGameServerStats', 'ISteamNetworkingSockets', 'ISteamUser', 'ISteamParentalSettings', 'ISteamFriends', 'ISteamMatchmakingServers', 'ISteamController', 'ISteamGameServer', 'ISteamMatchmaking', 'ISteamNetworkingUtils', 'ISteamNetworking', 'ISteamUtils', 'ISteamClient', 'ISteamMusic', 'ISteamNetworkingMessages', 'ISteamScreenshots', 'ISteamRemotePlay', 'ISteamHTMLSurface', 'ISteamVideo', 'ISteamUGC', 'ISteamParties', 'ISteamHTTP', 'ISteamInventory', 'ISteamInput'},
	# do not exist : SteamMatchmakingServerListResponse, SteamMatchmakingPingResponse, SteamMatchmakingPlayersResponse, SteamMatchmakingRulesResponse, SteamNetworkingFakeUDPPort

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

	# unimplemented structs
	'implementations' : {
		'SteamDatagramHostedAddress': """
struct SteamDatagramHostedAddress {
	int m_cbSize;
	char m_data[128];
};
""",
		'SteamDatagramGameCoordinatorServerLogin' : """
struct SteamDatagramGameCoordinatorServerLogin 
{
	SteamNetworkingIdentity m_identity;
	SteamDatagramHostedAddress m_routing;
	AppId_t m_nAppID;
	RTime32 m_rtime;
	int m_cbAppData;
	char m_appData[2048];
};
"""

	}
}
method_renames : dict = policies['method_renames']
field_types_skipped : set = policies['field_types_skipped']
type_conversions : dict = policies['type_conversion']
no_constructor : set = policies['no_constructor']
constructible_interfaces : set = policies['constructible_interfaces']
implementations : dict = policies['implementations']

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

def define_methods(class_:str, class_json:dict, use_flat_methods:bool=True):
	global binds
	methods = ({mName:m for m in mtds
		if not (mName:=m['methodname']).endswith('_DEPRECATED')
		} if (mtds := class_json.get('methods'))
		else {})

	for mName, method_json in methods.items():
		if mName in ('Construct','Release'): continue # already defined above

		param_json = method_json['params']
		param_names = [ p['paramname'] for p in param_json ]
		param_types = [ type_conversions.get(pType := p['paramtype']) or pType for p in param_json ]
		
		# TODO : wrap return_type if necessary
		return_type = method_json['returntype']

		py_mName = method_renames.get(mName) or mName

		# TODO: expose getters & setters as pybind properties
		flat_mName = method_json.get('methodname_flat') if use_flat_methods else None
		methodname = flat_mName or f'{class_}::{mName}'
		methodcall = f'{methodname}(self, ' if use_flat_methods else f'self->{mName}('
		py_params_str = ''.join( (f', py::arg("{k}")' for k in param_names) ) 

		# indices of parameters of types : const T*, T* or T&
		ptrParam_idx = list(filter(lambda i: (
			(pEnd := (pType := param_types[i])[-1]) == '*' or
			(not pType.startswith('const') and pEnd == '&')
		), range(len(param_types))))
		#print(py_class, mName, param_types, ptrParam_idx)

		if ptrParam_idx: #(new_return_type := type_conversions.get(return_type)) or len(ptrParam_idx):
			conversions = ''
			new_param_names = copy(param_names)
			for i in ptrParam_idx:
				newParamName = f'ptr{i}'
				old_pType = param_types[i]
				old_pName = param_names[i]
				cast_type = old_pType.replace('&', '*'); dereference = '*' if cast_type != old_pType else ''
				conversions += f'\n\t\t\t{old_pType} {newParamName} = {dereference}reinterpret_cast<{cast_type}>({old_pName});'
				param_types[i] = 'uintptr_t'
				new_param_names[i] = newParamName

			rType = return_type #new_return_type or return_type
			if rType != 'void': methodcall = f'return ({rType}){methodcall}'
			typed_params = ', '.join( f'{t} {n}' for t, n in zip(param_types, param_names) )
			mReference = f'''[]({class_}* self, {typed_params}) {{ {conversions}
			{methodcall}{', '.join(new_param_names)}); }}'''
		else:
			mReference = f'&{methodname}'
		return_policy = ', py::return_value_policy::reference' if return_type.replace(' *', '') in all_interface_names else ''
		binds +=  f'\t.def("{py_mName}", {mReference}{py_params_str})' #{return_policy})'


def define_class(class_:str,class_json:dict, is_struct:bool=True):
	global binds
	class_policy = policies.get(class_) or {}

	py_class = class_[:-2] if class_.endswith('_t') else class_
	bName = f'_{class_.lower()}'

	if class_ in constructible_interfaces:
		concreteType = class_[1:] # no 'I' ex: ISteamClient -> SteamClient
		binds += f'm.def("{concreteType}",[]() -> {class_}* {{ return {concreteType}(); }}, py::return_value_policy::reference);'
	
	binds += f'py::class_<{class_}{(
		# specify proxy type if one is defined
		f", {converted}" if (converted := type_conversions.get(class_))
		# use a unique_ptr with no delete for interfaces
		else f", std::unique_ptr<{class_}, py::nodelete>" if not is_struct else ""
		)}> {bName}(m, "{py_class}");'
	binds += bName

	# JIC we need a pointer sometimes on the python side
	binds += f'\t.def_property_readonly("ptr", []({class_}& self) {{ return reinterpret_cast<uintptr_t>(&self); }})'

	if is_struct and not class_ in no_constructor:
		binds += f'\t.def(py::init<>())'

	define_methods(class_, class_json)

	
	fields = [ # name, actualtype, sizebracketed
		parse_type(f['fieldname'], f['fieldtype'])
		 for f in class_json['fields']
		if f.get('private') == None # if public
	]

	for fName, actualtype, size in fields:

		if actualtype in field_types_skipped:
			#print('ignoring', py_class, fName, actualtype, size)
			continue

		fName = class_policy.get(fName) or fName
		
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



struct_json = api_json['callback_structs'] + api_json['structs']
interfaces_json = api_json['interfaces']
all_interface_names = { i['classname'] for i in interfaces_json }
#print(all_interface_names)

arraybinds = set()
for chunk in chunks(struct_json,20):

	unimplemented = [ implementations[cls_name] for cls in chunk if (cls_name := cls['struct']) in implementations ]
	header = '\n'.join(unimplemented)

	binds.newFile(header)
	for cls in chunk:
		class_ = cls['struct']
		define_class(class_, cls)
	binds.endFile()

#def find_class_json(name:str):
#	return [ d for d in classes if d['struct'] == name ][0]
#define_class('SteamIPAddress_t', find_class_json('SteamIPAddress_t'))

for chunk in chunks(interfaces_json, 20):
	binds.newFile()
	for cls in chunk:
		class_ = cls['classname']
		define_class(class_, cls, is_struct=False)
	binds.endFile()


# TODO:
# * bind enums
# * cleanup
# * test, test, test

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