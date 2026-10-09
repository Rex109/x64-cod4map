[![License](https://img.shields.io/badge/license-MIT-blue)](https://creativecommons.org/licenses/by-nc/4.0/)
[![Personal Discord](https://img.shields.io/discord/953653773962739793?color=%237289DA&label=Personal%20Discord&logo=discord&logoColor=%23FFFFFF)](https://discord.gg/QDYk75vBBk)
[![ManyAsset](https://img.shields.io/discord/585171589750849538?color=%23FF8711&label=ManyAsset&logo=discord&logoColor=%23FFFFFF)](https://discord.gg/v2TWkeR)

# x64-CoD4Map
<img width="1820" height="396" alt="logo2" src="https://github.com/user-attachments/assets/310d56b2-4c89-4439-8404-e25d94e14ddf" />

*<p align="center"><sub>Let there be light!</sub></p>*
<br>
A 64-bit fork of the `cod4map.exe` source reconstruction, the BSP compiler from the Call of Duty 4 mod tools (originally recovered from the shipped 2007 binary).
<br>

> [!WARNING]
> **This project was made entirely with AI.**
> Every change in this fork, the 64-bit port, the crash reports and the build files, was written by an AI model, and the underlying code is a machine-assisted reconstruction of a decompiled binary. It works on the maps it has been tried on, but it has not been reviewed the way real software should be. **Do not treat it as an example of good programming**, and do not copy its patterns into code you care about.

## ✨ What this fork adds

- 📐 **Automatic lightmap splitting** (`-splitLightmaps`): a surface too big for one 512x512 lightmap is cut into pieces that fit, instead of stopping with "Lightmap ... is larger than 512x512"
- 🧠 **64-bit build**, so big maps stop running out of the 32-bit address space
- 🩺 **Crash reports** that name the function and line instead of silently closing
- 🛠️ **Premake build**, with a one-click `generate-buildfiles_vs26.bat`

The executable is still called `cod4map.exe`, so it can replace the one in your mod tools.

## Build

Requirements:

- Visual Studio 2026 with the C++ toolset for the platform you want: **x64** for the 64-bit build, **x86** for the byte-exact Win32 build
- Premake 5 (bundled as `tools\premake5.exe`)

```
generate-buildfiles_vs26.bat
```

Then open `build\cod4map.slnx` in Visual Studio and build the platform you want, or from a command line:

```
msbuild build\cod4map.slnx /p:Configuration=Release /p:Platform=x64
msbuild build\cod4map.slnx /p:Configuration=Release /p:Platform=Win32
```

| Platform | Output | Notes |
| --- | --- | --- |
| x64 | `bin\x64\cod4map.exe` | No 32-bit memory limit. Uses SSE2, so it is **not byte-exact** with the original. |
| Win32 | `bin\cod4map.exe` | Byte-exact with the original (see below). Limited to the 32-bit address space. |

## Usage

```
cod4map -platform pc -loadFrom map_source\<mapname>.map raw\maps\mp\<mapname>
```

Run it from the [Call of Duty 4 mod tools](https://github.com/promod/CoD4-Mod-Tools) directory.

### Big surfaces and lightmaps

A lightmap page is 512x512 texels, so a surface that needs more than that stops the compile ("Lightmap 633x523 is larger than 512x512; need a sampleScale of at least 1.3"). Add **`-splitLightmaps`** and:

- A face that is too big is **cut into pieces** along planes, each piece getting its own lightmap area at the **same resolution**. The tool prints `split N windings that were too big for one lightmap`.
- Faces are no longer **merged** into one big surface if the result wouldn't fit a page.
- It is **off unless you give the option**, and only surfaces that don't fit are affected, so maps that compiled before come out the same either way.
- If a surface still can't be cut (for example a very oddly angled one), the old error appears, with the `sampleScale` hint.

The cuts add some extra edges, so very large surfaces cost a few more triangles. The lightmap still has a 31 page limit in total.

### Crash reports

If the program crashes, it prints the exception, the function and source line, and a call stack, and writes the same to `cod4map_crash.txt` in the current directory. Keep `cod4map.pdb` next to the exe to get names.

## Notes

The Win32 build uses `/arch:IA32 /fp:precise` on purpose: the original is an x87 build, and SSE2 code generation changes floating-point results and therefore the output bytes. The x64 build cannot do this, which is why it is not byte-exact.
