#import ctypes
from cffi import FFI
from pathlib import Path
from json import loads
from re import compile as regex

api = loads(Path('../public/steam/steam_api.json').read_text())

ffi = FFI()

# to determine whether to forward declared types
# ik const, unsigned and * are not actually types but this simplifies things
known_types = {'const', 'unsigned', '*', 'void', 'char', 'long', 'int' }

for enum in api['enums']:
	enumname = enum['enumname']
	ffi.cdef(f'typedef enum {enumname} {{'
	+ ', '.join( (f"{value['name']} = {value['value']}" for value in enum['values']) )
	+ f'}} {enumname};'
	)
	known_types.add(enumname)

detect_array = regex('(\\w+)\\s*(\\[[0-9]+\\])')
for typedef in api['typedefs']:
	typename = typedef['typedef']
	actualtype = typedef['type']

	# fixed array
	if '[' in actualtype:
		actualtype, end = detect_array.match(actualtype).groups()
		ffi.cdef(f'typedef {actualtype} {typename}{end};\n')
		continue

	# callback
	if '(*)' in actualtype:
		params = actualtype.split('(*)(')[-1][:-1].split(',')
		#print(params)

		# forward declare unknown arguments
		for param in params:
			for t in param.strip().split(' '):
				if not t in known_types:
					#print('forward declare of', t)
					ffi.cdef(f'typedef struct {t} {t};')

		ffi.cdef(f'typedef {actualtype.replace("(*)", f"(*{typename})")};\n')
		continue

	ffi.cdef(f'typedef {actualtype} {typename};\n')


# TODO: define structs
# TODO: create corresponding python classes using type()

print()

ffi.cdef(
#Path('../public/steam/steam_api_flat.h').read_text()
"""
ESteamAPIInitResult SteamAPI_InitFlat(SteamErrMsg *pOutErrMsg);
void SteamAPI_Shutdown(void);
""")

# TODO: detect platform and load the correct lib
steam = ffi.dlopen(str(Path("../redistributable_bin/win64/steam_api64.dll").resolve()))

err = ffi.new("SteamErrMsg *")
result = steam.SteamAPI_InitFlat(err)

#print(result)
#print(ffi.string(err[0]).decode("utf-8", errors="replace"))

steam.SteamAPI_Shutdown()