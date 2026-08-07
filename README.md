# OpenSesbianLex

OpenSesbianLex is a Flex/Bison-based parser and source obfuscator for a subset
of C and OpenCL C. A successful parse produces an obfuscated source file.

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
the expected error exit code. It also compiles and runs an original C++
program and its obfuscated version, then compares their results:

```sh
ctest --test-dir build --output-on-failure
```

The Linux GitHub Actions jobs run the OpenCL tests on two independent
implementations: the PoCL CPU runtime and the Oclgrind OpenCL simulator. The
semantic fixture covers vector swizzles, structures and `ptr->field`, macros
and conditional compilation, nested scopes, loops, local memory, `barrier`,
and `CLK_LOCAL_MEM_FENCE`. The jobs execute the original and obfuscated
kernels with the same input and compare their results. They also ask the
OpenCL compiler to build both forms of every valid `.cl` example in
`docs/cl_examples`.

To enable this test locally on a system with an OpenCL SDK and runtime:

```sh
cmake -S . -B build -DOPEN_SLEX_ENABLE_OPENCL_RUNTIME_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

To execute the same tests through Oclgrind:

```sh
cmake -S . -B build \
  -DOPEN_SLEX_ENABLE_OPENCL_RUNTIME_TESTS=ON \
  -DOPEN_SLEX_OPENCL_RUNTIME_LAUNCHER=oclgrind
cmake --build build
ctest --test-dir build --output-on-failure
```

### Test in Docker

The Docker image uses Ubuntu 24.04 and runs the tests while the image is being
built. No GPU passthrough is required. Build the PoCL target with:

```sh
docker build --progress=plain --target test-pocl -t openslex-test-pocl .
```

Run the same suite through Oclgrind with:

```sh
docker build --progress=plain --target test-oclgrind -t openslex-test-oclgrind .
```

The default target is PoCL, so `docker build .` also runs the PoCL suite. A
failed CMake build or CTest test makes `docker build` return a nonzero exit
code.

## Run

```sh
./build/OpenSLex docs/cl_examples/add_numbers.cl
```

For a Visual Studio Release build on Windows use
`./build/Release/OpenSLex.exe` instead.

By default the obfuscated source is written to `obfuscated_result.cl` in the
current directory. An explicit output path can be supplied as the second
argument:

```sh
./build/OpenSLex input.cl output.cl
```

The obfuscator currently renames detected variable identifiers, removes
selected whitespace and comments, inserts an unreachable transformed
block into braced `if` statements, and replaces supported punctuators with C
digraphs or trigraphs.

## Exit codes

- `0` — parsing succeeded
- `1` — the input contains a syntax error
- `2` — invalid command-line usage, such as a missing input path
- `3` — the input file could not be opened or read

## Floating-point literals

The lexer accepts the C/OpenCL decimal forms `1.`, `.5`, `1.0`, `1e5`, and
`1.0e-5`, including the `f`/`F`, `l`/`L`, and OpenCL `h`/`H` suffixes.
Hexadecimal floating-point literals use a mandatory binary exponent, for
example `0x1.fp3`, `0x1p0f`, or `0x1.ffcp15h`.

A standalone `.`, an exponent without digits such as `1e+`, and a hexadecimal
fraction without a complete `p`/`P` exponent are rejected.

## Strings and comments

String literals accept the C99/OpenCL simple escapes, octal and hexadecimal
escapes, `\u`/`\U` universal character names, and backslash-newline line
continuations. Unknown escapes, unescaped newlines, and EOF before the closing
quote are reported as `invalid string literal`.

Both `//` and `/* ... */` comments are supported. A `//` comment may end at
EOF without a final newline; an unclosed block comment is reported as
`unterminated block comment`.
