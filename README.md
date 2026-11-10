# mkdri

Convert linked Motorola 68000 ELF, COFF, and DRI executables to CP/M-68K
DRI load modules. Relocatable ELF input can also be emitted as a DRI
`.REL` module, and symbol maps can be generated alongside the output.

## Build and test on Windows

Requires CMake 3.16 or newer and Visual Studio 2022 with the C++ build
tools installed. Run from the repository root:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
cmake -E chdir build ctest -C Release --output-on-failure
.\build\Release\mkdri.exe --help
```

The required `alclib` C sources and shared headers are included under
[alclib/](alclib/) and [Include/](Include/). No sibling `worm68k` checkout
or external 68k toolchain is needed to build the host converter or run
its CTest regression test.

## Usage

```powershell
.\build\Release\mkdri.exe -o program.68K program.elf
.\build\Release\mkdri.exe -b500 -o program.68K program.dyn
.\build\Release\mkdri.exe -r -o program.REL program.dyn
```

See [MKDRI_SPEC.md](MKDRI_SPEC.md) for supported formats, relocation
requirements, switches, and exit codes. The documents in [native/](native/)
describe a proposed native CP/M-68K port, not an implemented build target.

## Repository history

This repository was extracted from
[dougdu/worm68k](https://github.com/dougdu/worm68k) at commit
`843eb09ac870fb98c065b1bb70ba3a2db6a0a9ee`. The history of
`68kTools/mkdri` and its included build dependencies is retained, including
original authors, committers, dates, and messages. Commit IDs changed
because the paths and repository contents changed.

The standalone build and documentation updates follow the extracted
history in a separate commit. Other toolchain components referenced by
the design documents remain in `worm68k`.

The `origin` remote is `https://github.com/dougdu/mkdri.git`. The extraction
does not publish any commits; when ready to publish and establish branch
tracking, run:

```powershell
git push -u origin main
```
