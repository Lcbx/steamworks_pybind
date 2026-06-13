# steamworks_pybind

Experimental Python bindings for the Steamworks SDK, generated with [pybind11](https://github.com/pybind/pybind11).

The goal is to expose the Steamworks API to Python with mostly generated bindings based on Steam's `steam_api.json`, while keeping handwritten C++ code to a minimum.

> **Warning**
> This project is experimental. The bindings are low-level and may contain unexpected corner cases, incorrect ownership assumptions, crashes, or memory leaks. Use with care.
## Install

Install from an existing wheel (see [releases](https://github.com/Lcbx/steamworks_pybind/releases)) :

```bash
python -m pip install <wheel-file>.whl
```

Or install the project directly from the repository:

```bash
python -m pip install .
```

After installation:

```python
import steamworks as steam
```

## Usage

See [`test.py`](https://github.com/Lcbx/steamworks_pybind/blob/master/test.py) for current usage examples.

The tests cover initialization, Steam user/friend APIs, structs, enums, lobby creation, lobby metadata, callbacks, and call results.

Run all tests:

```bash
python -m pytest -qs test.py
```

Or run the test file directly:

```bash
python test.py
```

A single test can also be run by name:

```bash
python test.py test_friends_persona_name
```

## Build locally

You need:

* Python 3.12+
* a C++ compiler compatible with your Python installation
* the Steamworks SDK
* the Steam client running and logged in for runtime tests

see [pyproject.toml](https://github.com/Lcbx/steamworks_pybind/blob/master/pyproject.toml) for the python libraries used

The current build expects this repository to be next to the unpacked Steamworks SDK folders:

```text
some-parent-directory/
├── public/
│   └── steam/
│       ├── steam_api.json
│       ├── steam_api_flat.h
│       └── ...
├── redistributable_bin/
│   ├── win64/
│   ├── linux64/
│   └── ...
└── steamworks_pybind/
    ├── setup.py
    └── ...
```

In particular, the build expects to find :

```text
../public
../redistributable_bin/<platform>
```
To build from the repository root:

```bash
python setup.py build_ext --build-lib build
```

This generates the C++ bindings and builds the native `steamworks` extension into `build/`.

You can then import it locally with:

```python
import build.steamworks as steam
```

To build wheels :

```bash
py -m cibuildwheel --platform windows --output-dir wheelhouse
```

## Notes

* A valid `steam_appid.txt` may be required for local testing.
* Steam must usually be running and logged in.
* Pointer-heavy APIs are still exposed in a low-level way.
* Generated bindings may change as the generator evolves.

## License

This project is licensed under the MIT License.
See [`LICENSE`](https://github.com/Lcbx/steamworks_pybind/blob/master/LICENSE) for details.

