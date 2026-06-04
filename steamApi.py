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

# to determine whether to forward declare types
# ik const, unsigned and * are not actually types but this simplifies things
known_types = {'const', 'unsigned', 'signed', '*', '**', 'void', 'char', 'short', 'long', 'int', 'float' }

def typedef(name:str, declaration:str)->None:
	add_type(name)
	declaration = f'typedef {declaration};\n'
	#print(declaration)
	ffi_builder.cdef(declaration)

def add_type(name:str)->None:
	global known_types
	known_types.add(name)


## Enums ---------------------------------
# NOTE: we already create the classes that contain the enums

for enum in api_json['enums']:
	enumname = enum['enumname']

	values = ( (d['name'], int(d['value'])) for d in enum['values'] )

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
	globals()[enumname] = cls
	#print(cls)

	# old declaration as c enum, generate strings that are too long
	# see https://github.com/python-cffi/cffi/pull/167
	#typedef(enumname,
	#	f'enum {enumname} {{'
	#	+ ', '.join( (f"{value['name']} = {value['value']}" for value in enum['values']) )
	#	+ f'}} {enumname}'
	#)


## typedefs ---------------------------------
# NOTE: requires some forward declarations

array_regex = regex('(\\w+)\\s*(\\[[0-9]+\\])')

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


for td in api_json['typedefs']:
	typename = td['typedef']
	typename, actualtype, sizebracketed = parse_type(typename, td['type'])
	#print(typename, actualtype, sizebracketed)
	typedef(typename, f'{actualtype} {typename}{sizebracketed}')

# missing typedefs (really valve, really ?)
typedef('CSteamID', 'uint64 CSteamID')
typedef('CGameID',  'uint64 CGameID')


## structs ---------------------------------
# TODO: when encountering an unknow type in struct fields, try to find it in api.json 
# TODO: create wrapper python classes using type()
ignore = {} #{'SteamInputActionEvent_t', 'SteamDatagramGameCoordinatorServerLogin', 'SteamDatagramHostedAddress'}
for st in api_json['structs']:
	structname = st['struct']
	if structname in ignore: continue
	
	# forward declaration
	typedef(structname, f'struct {structname} {structname}')

	fields = [ (
		f.get('private') == None, *parse_type(f['fieldname'], f['fieldtype'])
		) for f in st['fields']
	]
	#print(*fields)

	partial = any(map(lambda _public: not _public[0], fields))

	declaration = f"""
struct {structname} {{{ ''.join((
	f'\n\t{actualtype} {name}{sizebracketed};'
	for public, name, actualtype, sizebracketed in fields if public
	))
}{ "\n\t...; // allow partial declarations" if partial else ''}
}} {structname};"""
	print(declaration)
	#print(known_types)
	typedef(structname, declaration)


#ffi_builder.cdef("""
#struct SteamInputActionEvent_t
#{
#	InputHandle_t controllerHandle;
#	ESteamInputActionEventType eEventType;
#	struct AnalogAction_t {
#		InputAnalogActionHandle_t actionHandle;
#		InputAnalogActionData_t analogActionData;
#	};
#	struct DigitalAction_t {
#		InputDigitalActionHandle_t actionHandle;
#		InputDigitalActionData_t digitalActionData;
#	};
#	union {
#		AnalogAction_t analogAction;
#		DigitalAction_t digitalAction;
#	};
#};
#""")


ffi_builder.cdef(
"""
ESteamAPIInitResult SteamAPI_InitFlat(SteamErrMsg *pOutErrMsg);
void SteamAPI_Shutdown(void);
""")


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
		# need to go up 2 dirs since 'build' is a subdir
		"""
		#include "../../public/steam/steam_api_flat.h"
		#include "../../public/steam/steam_gameserver.h" // for EServerMode enum
		//#include "../../public/steam/matchmakingtypes.h" // for servernetadr_t
		""",
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

err = ffi.new("SteamErrMsg *")
result = lib.SteamAPI_InitFlat(err)
#print(result)
#print(ffi.string(err[0]).decode("utf-8", errors="replace"))

lib.SteamAPI_Shutdown()