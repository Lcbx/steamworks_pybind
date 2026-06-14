import sys
from pathlib import Path
from shutil import copy2 as copy_file

from setuptools import setup
from pybind11.setup_helpers import Pybind11Extension


platform = { 'win32':'win64', 'linux':'linux64', 'android':'androidarm64', 'darwin':'osx' }[sys.platform]
lib_name = { 'win64':'steam_api64.dll', 'linux64':'libsteam_api.so', 'androidarm64': 'libsteam_api.so', 'osx':'libsteam_api.dylib' }[platform]
distrib_path = Path(f'../redistributable_bin/{platform}')

if __name__ == '__main__' and (len(sys.argv) == 1 or 'build_ext' in sys.argv):
	sys.argv = [__name__, 'build_ext', '--build-lib', 'build']

# to be able to import genereate_cpp
sys.path.insert(0, str(Path(__file__).resolve().parent))

import generate_cpp

if not (b_dir := Path('build')).exists(): b_dir.mkdir()

# copy dll in  work dir
destination = Path(f'./build/{lib_name}')
origin = Path(f'{distrib_path}/{lib_name}')
if not destination.exists(): copy_file(origin, destination)

setup(
	name='steamworks',
	version='0.1.4',
	author='Lcbx',
	author_email='https://github.com/Lcbx',
	description='WIP procedural bindings for steamworks',
	ext_modules=[
		Pybind11Extension(
			'steamworks',
			generate_cpp.run(),
			include_dirs=[
				'../public',
			],
			library_dirs=[
				str(distrib_path),
			],
			libraries=[
				destination.stem,
			],
			cxx_std=11,
		),
	],
)