# cod4map

A source reconstruction of `cod4map.exe`, the BSP compiler from the Call of Duty 4 mod tools, recovered from the shipped 2007 binary.

## Requirements

- Visual Studio 2019 or later with the **x86 (32-bit) C++ toolset** installed
  (Desktop development with C++ → "MSVC ... C++ x64/x86 build tools")

## Build

```
build.bat
```

The executable is written to `bin\cod4map.exe`.

## Usage

```
cod4map -platform pc -loadFrom map_source\<mapname>.map raw\maps\mp\<mapname>
```

Run it from the [Call of Duty 4 mod tools](https://github.com/promod/CoD4-Mod-Tools) directory.

## Notes

The build uses `/arch:IA32 /fp:precise` on purpose: the original is an x87
build, and SSE2 code generation changes floating-point results and therefore
the output bytes.
