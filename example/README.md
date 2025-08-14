# Examples for Python bindings for FoveAPI

## Setting up python environment

To use the Python bindings for FoveAPI, you will need to install Python and our Fove Runtime.

On Windows, we assume that the samples will be run using `cmd.exe`.
(If you prefer to use a Python IDE, please refer to the manual of the IDE for propely setting up a `python` executable, `PYTHONPATH`etc.)

Firstly, make sure that a version of `python` is installed and available.
On `cmd.exe`:
```
> python
Python 3.7.5 (default, Oct 14 2019, 23:08:55)
>>> print("Hello, Fove!")
Hello, Fove!
>>> 1 + 1
2
```

## Usage

Copy the build folder from our workflow or your local build to your poject directory.
The structure of your project directory might look like this:
```
project_dir
    ├── fove
        ├── FoveClient.dll/libFoveClient.so(Depening on your system)
        ├── headset.py
        ├── __init__.py
        ├── capi.cpython-38-x86_64-linux-gnu.so(Depending on your system and python version)
    |── sample.py
```

Then run sample.py:
```
> python sample.py
```


