# OpenSesbianLex

OpenSesbianLex is an OpenCL C source obfuscator. Its primary semantic frontend
is Clang/libclang: Clang validates the translation unit and binds declarations
to their exact references before identifiers are rewritten. Flex performs the
final lexical output pass. The older Flex/Bison parser remains available as a
compatibility frontend for builds without libclang and for historical C input.

## Requirements

- CMake 3.20 or newer
- A C++ compiler with C++14 support
- Bison 2.7 or newer
- Flex 2.6 or newer
- Clang and the libclang development package (recommended and required by
  `-DOPEN_SLEX_FRONTEND=CLANG`)
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

Install the official LLVM Windows package as well. A default installation
under `C:\Program Files\LLVM` is detected automatically, and CMake copies
`libclang.dll` beside `OpenSLex.exe`. Adding LLVM's `bin` directory to `PATH`
is optional. If LLVM is elsewhere, pass its prefix as
`-DLibClang_ROOT=<path-to-LLVM>` when configuring CMake.

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

This MSYS2 recipe builds the compatibility frontend. Configure it with
`-DOPEN_SLEX_FRONTEND=LEGACY`. Linux and Visual Studio are the supported
libclang configurations.

### Ubuntu/Debian

```sh
sudo apt update
sudo apt install cmake ninja-build g++ bison flex clang libclang-dev
```

## Build with Visual Studio

Run these commands from the repository root in PowerShell or a Developer
Command Prompt:

```powershell
cmake -S . -B build -DOPEN_SLEX_FRONTEND=CLANG
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The executable is `build/Release/OpenSLex.exe`.

## Build with Ninja

Run these commands from the repository root:

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DOPEN_SLEX_FRONTEND=CLANG
cmake --build build
```

With a single-config Ninja build the executable is `build/OpenSLex.exe` on
Windows and `build/OpenSLex` on Linux.

## Test

CTest runs valid and invalid C/OpenCL inputs, verifies Clang semantic binding,
and checks that an undeclared OpenCL identifier is rejected. It also compiles
and runs an original C++ program and its obfuscated version, then compares
their results:

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

Run the host-code ASan/UBSan suite and a bounded libFuzzer campaign with:

```sh
docker build --progress=plain --target test-sanitizers -t openslex-test-sanitizers .
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

For `.cl` files, an AUTO build uses libclang whenever it is available. You can
select the frontend explicitly:

```sh
OpenSLex --frontend clang input.cl output.cl
OpenSLex --frontend legacy historical-input.c output.c
```

The Clang frontend renames variables, parameters, helper functions, typedefs,
structure tags, fields, and enum constants together with the AST references
bound to each declaration. Shadowed declarations and a field/local pair with
the same spelling therefore have independent identities. Externally visible
OpenCL kernel names, macro definitions and parameters, unresolved OpenCL
built-ins, and vector selectors such as `.xy` and `.s0` are preserved.

Clang parses OpenCL C 1.2 by default and automatically loads its OpenCL builtin
header. Additional compiler options may be repeated, for example:

```sh
OpenSLex --clang-arg=-I/path/to/headers \
  --clang-arg=-DFEATURE_LEVEL=2 input.cl output.cl
```

Only the preprocessor configuration selected by those arguments has a full
AST. Identifiers found in excluded conditional blocks are conservatively left
unchanged so that another macro configuration is not silently broken.

The output pass also removes selected whitespace and comments, inserts a
side-effect-free opaque-false branch into braced `if` statements, and replaces
supported punctuators with C digraphs or trigraphs. The opaque predicate uses
only unsigned arithmetic and never evaluates the original condition again.

## Preprocessor directives

C99/OpenCL preprocessing directives are recognized only when `#` is the first
non-whitespace character of a logical line. The obfuscator preserves `#if`,
`#ifdef`, `#ifndef`, `#elif`, `#else`, `#endif`, `#define`, `#undef`, `#include`,
and `#pragma` directives without rewriting their preprocessing tokens.
Backslash-continued definitions, object-like expressions, variadic parameters,
stringizing (`#`) and token pasting (`##`) are retained. Conditional groups may
be nested and their pairing is validated. Words such as `define`, `include`,
and `ifdef` remain ordinary identifiers outside directive lines.

Macros are preserved rather than expanded by OpenSLex. User symbols referenced
from a macro replacement list are conservatively kept unchanged so that the
preserved macro remains valid after obfuscation.

## Reproducible obfuscation seeds

Pass an unsigned 32-bit seed to reproduce a particular set of generated names
and opaque-predicate constants:

```sh
OpenSLex --seed 12345 input.cl output.cl
```

The same source and seed produce the same output. Regression tests exercise
several distinct seeds and reparse every generated source file.

## Sanitizers and fuzzing

GCC or Clang host builds can enable AddressSanitizer and
UndefinedBehaviorSanitizer with `-DOPEN_SLEX_ENABLE_SANITIZERS=ON`. With Clang,
`-DOPEN_SLEX_BUILD_FUZZER=ON` also builds `OpenSLexFuzzer`, which passes
arbitrary byte sequences directly through the Flex lexer and Bison parser.
The Linux CI job runs the full regression suite under ASan/UBSan and a bounded
fuzz smoke test. A longer local campaign can be started with:

```sh
./build-sanitized/OpenSLexFuzzer -max_total_time=600 fuzz/corpus
```

## Exit codes

- `0` — parsing succeeded
- `1` — the input contains a syntax error
- `2` — invalid command-line usage, such as a missing input path
- `3` — the input file could not be opened or read
- `4` — the requested semantic frontend could not be initialized

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
