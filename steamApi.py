from sys import platform
from cffi import FFI
from pathlib import Path
from shutil import copy2 as copy_file
from json import loads
from re import compile as regex
from enum import IntEnum


# some renaming to match steam dir names
# TODO: test other pltforms
platform = { 'win32':'win64', 'linux':'linux64', 'android':'androidarm64', 'darwin':'osx' }[platform]
lib_name = { 'win64':'steam_api64.dll', 'linux64':'libsteam_api.so', 'androidarm64': 'libsteam_api.so', 'osx':'libsteam_api.dylib' }[platform]

api_json = loads(Path('../public/steam/steam_api.json').read_text())

ffi_builder = FFI()

cpp_source =  """
#include "../../public/steam/steam_api_flat.h"
#include "../../public/steam/steam_gameserver.h" // for EServerMode enum
"""

# to determine whether to forward declare types
# ik const, unsigned and * are not actually types but this simplifies things
known_types = {'const', 'unsigned', 'signed', '*', '**', 'void', 'char', 'short', 'long', 'int', 'float', 'double' }

# types that have been properly defined
# for structs, that means full declaration, with struct members
defined_types = set() #{ t for t in known_types }

def typedef(name:str, declaration:str)->None:
	global known_types
	known_types.add(name)
	declaration = f'typedef {declaration};\n'
	#print(declaration)
	ffi_builder.cdef(declaration)

def extern(name:str, declaration:str)->None:
	global defined_types
	defined_types.add(name)
	declaration = f'extern {declaration};\n'
	#declaration = f'{declaration};\n'
	#print(declaration)
	ffi_builder.cdef(declaration)

def add_global(name:str, clss:object)->None:
	globals()[name] = cls


def parse_type(name, typename:str)->tuple:
	# functions / callbacks
	if '(*)' in typename:
		params = typename.split('(*)')[-1][1:-1].split(',')
		#print(params)

		# forward declare unknown arguments
		for param in params:
			parse_type('', param)

		return '', typename.replace("(*)", f"(*{name})"), ''
	
	sizebracketed = ''
	# arrays
	if '['  in typename:
		typename, sizebracketed = array_regex.match(typename).groups()

	# inner types
	if '::' in typename:
		typename = typename.split('::')[-1]

	# forward declare unknown types
	for t in typename.strip().split(' '):
		if not t in known_types:
			typedef(t, f'struct {t} {t}')

	return name, typename, sizebracketed


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
		ffi_builder.cdef(f'const {enumname} {name} = {value};')

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

array_regex = regex('(\\w+)\\s*(\\[[0-9]+\\])')

for td in api_json['typedefs']:
	typename = td['typedef']
	typename, actualtype, sizebracketed = parse_type(typename, td['type'])
	#print(typename, actualtype, sizebracketed)
	typedef(typename, f'{actualtype} {typename}{sizebracketed}')


# c is silly
typedef('bool',    'int bool')
typedef('int64_t', 'int64 int64_t')

# NOTE : need to write shims for conversion to and from uint64
# for structs and functions....
#typedef('CSteamID', 'uint64 CSteamID')
#typedef('CGameID',  'uint64 CGameID')
# has union members
defined_types.add('SteamInputActionEvent_t')


## structs ---------------------------------
# TODO: create wrapper python classes using type()
# NOTE: not sure what to do with callback_structs' callback_id

def define_struct(struct_json:dict):
	structname = st['struct']
	if structname in defined_types: return
	
	# forward declaration for structs thatuse references to themselves
	#typedef(structname, f'struct {structname} {structname}')

	fields = [ ( # public, name, actualtype, sizebracketed
		f.get('private') == None, *parse_type(f['fieldname'], f['fieldtype'])
		) for f in st['fields']
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

# NOTE: some structs have unions
# it seems the json only takes the first set of properties
# ex: here the struct declared only has a AnalogAction_t field
#extern('DigitalAction_t', """struct DigitalAction_t {
#	InputDigitalActionHandle_t actionHandle;
#	InputDigitalActionData_t digitalActionData;
#}""")
#extern('SteamInputActionEvent_t', """struct SteamInputActionEvent_t
#{
#	InputHandle_t controllerHandle;
#	ESteamInputActionEventType eEventType;
#	//union {
#		AnalogAction_t analogAction;
#	//	DigitalAction_t digitalAction;
#	//};
#}""")


ffi_builder.cdef(
"""
ESteamAPIInitResult SteamAPI_InitFlat(SteamErrMsg *pOutErrMsg);
void SteamAPI_Shutdown(void);
""")


#if False:
if __name__ == "__main__":

	# create build dir if it does not exist
	if not (b_dir := Path('build')).exists() or not b_dir.is_dir():
		b_dir.mkdir()
	
	# copy dll in build folder
	destination = Path(f'./build/{lib_name}')
	origin = Path(f'../redistributable_bin/{platform}/{lib_name}')
	if not destination.exists():
		copy_file(origin, destination)


	ffi_builder.set_source(
		'steamworks_cffi',
		cpp_source,
		# need to go up 2 dirs since 'build' is a subdir,
		include_dirs=[
			'../../public',
		],
		library_dirs=[
			f'../../redistributable_bin/{platform}',
		],
		libraries=[
			destination.stem,
		],
	    source_extension='.cpp',
	)
	ffi_builder.compile(tmpdir='build',)#verbose=True)

# import the compiled module
try:
	from build.steamworks_cffi import ffi, lib
except Exception as ex:
	print(f'import compiled module failed ({ex}), fallback to dynamic')
	ffi = ffi_builder
	lib = ffi.dlopen(str(Path("./build/steam_api{lib_ext}").resolve()))

#from pprint import pp
#pp(dir(lib))
#pp(globals()['ESteamIPType'])
#pp(dir(ESteamIPType))

err = ffi.new("SteamErrMsg *")
result = lib.SteamAPI_InitFlat(err)
#print(result)
#print(ffi.string(err[0]).decode("utf-8", errors="replace"))

lib.SteamAPI_Shutdown()