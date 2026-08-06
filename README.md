# OpenSesbianLex

OpenSesbianLex is a Flex/Bison-based syntax checker for a subset of C and
OpenCL C.

## Requirements

- CMake 3.20 or newer
- A C++ compiler with C++11 support
- Bison 2.7 or newer
- Flex 2.6 or newer
- Ninja is recommended, but another CMake generator can be used

### Windows: choose one toolchain

The Visual Studio and MSYS2 configurations below are alternatives. Do not
install both. If Visual Studio, CMake, and WinFlexBison are already installed,
use the Visual Studio instructions and skip MSYS2.

#### Option 1: Visual Studio (recommended for this repository)

Install [WinFlexBison](https://github.com/lexxmark/winflexbison/releases),
extract the release archive, and add the directory containing
`win_bison.exe` and `win_flex.exe` to `PATH`. CMake accepts both the GNU
executable names and the WinFlexBison names.

Visual Studio 2019 or newer can provide the C++ compiler and the default CMake
generator. Ninja is not required for this configuration.

#### Option 2: MSYS2 UCRT64

Use this option only if you prefer a GCC/Ninja environment or do not have a
Visual Studio C++ toolchain.

Install [MSYS2](https://www.msys2.org/) in its default location, open the
**MSYS2 UCRT64** terminal, update the installation, and install the toolchain:

```sh
pacman -Syu
pacman -S --needed \
  mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja \
  bison flex
```

If the first update asks you to close the terminal, reopen **MSYS2 UCRT64**,
run `pacman -Syu` again, and then install the packages.

### Ubuntu/Debian

```sh
sudo apt update
sudo apt install cmake ninja-build g++ bison flex
```

## Build with Visual Studio

Run these commands from the repository root in PowerShell or a Developer
Command Prompt:

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The executable is `build/Release/OpenSLex.exe`.

## Build with Ninja

Run these commands from the repository root:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

With a single-config Ninja build the executable is `build/OpenSLex.exe` on
Windows and `build/OpenSLex` on Linux.

## Test

CTest runs the valid C/OpenCL inputs and verifies that invalid C inputs return
the expected error exit code:

```sh
ctest --test-dir build --output-on-failure
```

## Run

```sh
./build/OpenSLex docs/cl_examples/add_numbers.cl
```

For a Visual Studio Release build on Windows use
`./build/Release/OpenSLex.exe` instead.
