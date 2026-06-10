from pathlib import Path

def joinlines(ls:list[str])->str:
	return '\n'.join(ls)


BUILD_DIR = './build/'
bindings_main = f'{BUILD_DIR}bindings.cpp'

class BindingsFile:

	def __init__(self, target_char_per_file = 50000):
		self.file_count = 0
		self.impl_file = None
		self.last_calls = ''
		self.target_char_per_file = target_char_per_file

	def newFile(self, header:str = '')->None:
		bind_method = f'bind_{self.file_count}'
		fPath = self._filePath(self.file_count)
		self.file_count += 1

		self.impl_file = open(Path(fPath), 'w')
		self.impl_file.write(f'''
#include "../common.h"

{header}

void {bind_method}(py::module_& m) {{
''')

	def _filePath(self,i:int)->str:
		return f'{BUILD_DIR}bind_{i}.cpp'

	def __iadd__(self, s:str)->'BindingsFile':
		self.impl_file.write(f' \t{s}\n')
		return self

	def endDefinition(self)->None:
		written = self.impl_file.tell()
		if written > self.target_char_per_file:
			self.endFile()
			self.newFile()

	def endFile(self)->None:
		self.impl_file.write('\n}') # close function
		self.impl_file.close()

	def get_filenames(self)->None:
		return [bindings_main, *(self._filePath(i) for i in range(self.file_count)) ]

	def __enter__(self)->'BindingsFile':
		return self

	def __exit__(self, *args)->None:
		self.close()

	def close(self)->None:

		forward = ( f'void bind_{i}(py::module_& m);' for i in range(self.file_count) )

		calls = ( f'\tbind_{i}(m);' for i in range(self.file_count) )

		with open(Path(bindings_main), 'w') as main:
			main.write(f'''
#include "../additional_implementations.h"

namespace py = pybind11;

{joinlines(forward)}

PYBIND11_MODULE(steamworks, m) {{
	bind_init(m);

{joinlines(calls)}
{self.last_calls}

	bind_response_adapters(m);
}}
''')