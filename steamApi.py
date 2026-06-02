#import ctypes
from cffi import FFI
from pathlib import Path
from json import loads
from re import compile as regex

api = loads(Path('../public/steam/steam_api.json').read_text())

ffi_builder = FFI()

# these strings that are too long, patch pending
# see https://github.com/python-cffi/cffi/pull/167
bannedEnums = {'EControllerActionOrigin', 'EInputActionOrigin'}

# to determine whether to forward declared types
# ik const, unsigned and * are not actually types but this simplifies things
known_types = {'const', 'unsigned', '*', 'void', 'char', 'long', 'int' }

def declare_type(name:str, declaration:str)->None:
	global known_types
	known_types.add(name)
	declaration = f'typedef {declaration};\n'
	#print(declaration)
	ffi_builder.cdef(declaration)

for enum in api['enums']:
	enumname = enum['enumname']
	if enumname in bannedEnums: continue
	declare_type(enumname,
		f'enum {enumname} {{'
		+ ', '.join( (f"{value['name']} = {value['value']}" for value in enum['values']) )
		+ f'}} {enumname}'
	)

detect_array = regex('(\\w+)\\s*(\\[[0-9]+\\])')
for typedef in api['typedefs']:
	typename = typedef['typedef']
	actualtype = typedef['type']

	# fixed array
	if '[' in actualtype:
		actualtype, sizebracketed = detect_array.match(actualtype).groups()
		declare_type(typename, f'{actualtype} {typename}{sizebracketed}')
		continue

	## callback
	if '(*)' in actualtype:
		params = actualtype.split('(*)')[-1][1:-1].split(',')
		#print(params)

		# forward declare unknown arguments
		for param in params:
			for t in param.strip().split(' '):
				if not t in known_types:
					#print('forward declare of', t)
					declare_type(t, f'struct {t} {t}')

		# callback must be dclared after its params
		declare_type(typename, actualtype.replace("(*)", f"(*{typename})"))
		continue

	declare_type(actualtype, f'{actualtype} {typename}')

# TODO: define structs
# TODO: create corresponding python classes using type()

ffi_builder.cdef(
"""
ESteamAPIInitResult SteamAPI_InitFlat(SteamErrMsg *pOutErrMsg);
void SteamAPI_Shutdown(void);
""")


ffi_builder.set_source(
	'steamworks_cffi',
	# need to go up 2 dirs since 'build' is a subdir
	"""
	#include "../../public/steam/steam_api_flat.h"
	#include "../../public/steam/steam_gameserver.h" // for EServerMode enum
	""",
	include_dirs=[
		'../../public',
	],
	library_dirs=[
		'../../redistributable_bin/win64',
	],
	libraries=[
		'steam_api64',
	],
    source_extension='.cpp',
)


# now works with dynamic ffi and compiled
# TODO: use dynamic as fallback 

if __name__ == "__main__":
	ffi_builder.compile(tmpdir='build',)#verbose=True)
	#steam = ffi_builder.dlopen(str(Path("../redistributable_bin/win64/steam_api64.dll").resolve()))

	# TODO: copy steam_api64.dll|.so into build dir

	from build.steamworks_cffi import ffi, lib as steam

	#from pprint import pp
	#pp(dir(steam))

	#err = ffi_builder.new("SteamErrMsg *")
	err = ffi.new("SteamErrMsg *")
	result = steam.SteamAPI_InitFlat(err)

	#print(result)
	#print(ffi.string(err[0]).decode("utf-8", errors="replace"))

	steam.SteamAPI_Shutdown()