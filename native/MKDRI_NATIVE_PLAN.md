# `mkdri` — CP/M-68K Native Port: Phasing & Delivery Plan

Date: 2026-06-12
Author: GitHub Copilot (for review)
Status: **Draft** — no native code landed yet.
Companion documents:
* [MKDRI_NATIVE_DESIGN.md](MKDRI_NATIVE_DESIGN.md) — technical design.
* [../MKDRI_SPEC.md](../MKDRI_SPEC.md) — the parent (host) spec that the
  native port reproduces semantically.
* [../../reloc/reloc.c](../../reloc/reloc.c) — the historical DRI
  `reloc` source (already Win64-buildable), which the Phase N1 drop
  strictly supersedes.
* [../../../cpm68k/src/v102a/](../../../cpm68k/src/v102a/) — the
  **Alcyon 4.3 (January 1984)** source kit (`cp68` / `c068` / `c168`
  / `cc` / `as68` / `lo68` / `clib` / `libe` / `libf` / `cutils` /
  `util`), the supply source for the Win64 toolchain port described
  in §2.  This is the latest Alcyon C we have in-tree — newer than
  both `cpm68k/src/v102/` (Feb 1983) and `cpm68k/src/v103/`
  (Sep 1983).

---

## 1. What this document is

A native CP/M-68K build of `mkdri` — i.e. `MKDRI.68K`, loadable from
the CCP on a real (or simulated) CP/M-68K system — that ultimately
reaches feature parity with the host (Win32 / POSIX) `mkdri.exe`
described in [../MKDRI_SPEC.md](../MKDRI_SPEC.md).

It is *not* the host tool, *not* a Worm68K bare-metal tool, and *not*
a rewrite of the cross-build pipeline.  It is a **second**
implementation of the same spec that runs on top of the BDOS, links
with the Alcyon C runtime, and uses BDOS-FCB sequential I/O instead
of `<stdio.h>` streams over Win32 handles.

> **Why two implementations?** Because the host tool requires a host
> OS to build and run.  A native `MKDRI.68K` lets a CP/M-68K user who
> has *only* the CP/M-68K v1.3 distribution disks plus the modern
> `worm68k` tree:
>
> 1. Build any of the bare-bones m68k programs included in
>    `cpm68k/src/v1.3/` directly on the target (boot CP/M, then
>    `A>mkdri foo.rel`), which is the historical workflow that
>    DRI's `reloc.68k` enabled.
> 2. Bootstrap-build their own `MKDRI.68K` from `MKDRI.C` using only
>    `c68` + `as68` + `lo68` + the new `MKDRI.68K` itself — closing
>    the self-hosting loop the same way `bas68k` does for BASIC.
> 3. Re-base any third-party `.REL` they find without leaving CP/M-68K
>    (e.g. a `.REL` they just compiled, or one from the v1.2 disks).

The native build is **strictly opt-in**: nothing in the existing host
build, runtime, or test suite needs to change for it.  It lives under
`68kTools/mkdri/native/` and ships as a CMake side-target plus an
nmake-style makefile that drives the in-tree `c68` / `as68` / `lo68`
/ `reloc` cross-toolchain (see §2) to produce `MKDRI.68K` +
`MKDRI.SYM`.

---

## 2. Implementation language: Alcyon C, ported to Win64

### 2.1 The toolchain we will build

A standalone "DRI-era C" cross-toolchain on Windows, made from the
complete **Alcyon 4.3 (January 1984)** source distribution already
sitting in-tree at
[cpm68k/src/v102a/](../../../cpm68k/src/v102a/):

```
cpm68k/src/v102a/
    cp68/         C preprocessor       4.3, Jan 5 1984  (= on-target `cp68`)
    c068/         C parser / sem chk  4.3, Jan 3 1984  (= on-target `c068`)
    c168/         C code generator    4.3, Dec 30 1983 (= on-target `c168`)
    cc/           driver, fexec's the 3 above  4.3, Nov 17 1983
    as68/         68000 assembler     4.3, Dec 30 1983 (= on-target `as68`)
    lo68/         relocatable linker + reloc.c + ar.c + nm.c + size.c +
                  sendc68.c + nuxi.c (byte-order swap)
    clib/         C library (full — includes basepage.h, bdos.h,
                  cpm.h, sgtty.h, signal handling, malloc/sbrk, etc.)
    libe/         IEEE-754 float library
    libf/         fast (Motorola FFP) float library
    misc/         C startup file + `-g` (gdb) termination glue
    util/         standalone build wrappers for the util binaries
    cutils/       Alcyon C re-implementations of CP/M-68K utilities
                  (`ed`, `pip`, `stat`) — the v1.2 SRCKIT.DOC notes
                  these existed only in PL/H form at the time
    plmutils/     PL/M-source versions of same utilities (reference only)
    xutils/       cross-host helpers (sendc68, setstack, ar, nm, size)
    orgas68/      pristine pre-DRI baseline of as68 (reference)
    orgc068/      pristine pre-DRI baseline of c068 (reference)
    orgc168/      pristine pre-DRI baseline of c168 (reference)
    orgc268/      pristine pre-DRI baseline of c268 (reference)
    orgcp68/      pristine pre-DRI baseline of cp68 (reference)
    orglibe/      pristine baseline (reference)
    orglibf/      pristine baseline (reference)
    orglo68/      pristine baseline (reference)
    test/         compiler regression suite
    doc/          compiler manual
```

Per the v102a [`readme`](../../../cpm68k/src/v102a/readme):

> ALCYON Compiler/Assembler/Loader — version 4.3 — January 1984
> […]
> Depending on the machine/operating system you wish this compiler
> to run on they must be compiled with the appropriate defines.
>
> Operating System: REGULUS, UNIX, VMS, CPM, VERSADOS
> Machine: MC68000, VAX11, PDP11
> VMS source compiler: DECC (Dec C on VMS), WHITE (Whitesmith's C on VMS)

In other words, the Alcyon 4.3 sources are **explicitly designed as
a portable host-cross-compiler**: they already build under five
OSes and three host C compilers including DEC C and Whitesmith's C
on VAX/VMS.  We are following exactly the same path, just to a more
modern host: MSVC + clang-cl on Win64 x86_64.  The `org*/` mirror
directories preserve the pristine pre-DRI baseline of each pass so
we always have a clean diff target when investigating an Alcyon-vs-DRI
behavior question.

### 2.2 Precedent in this tree

We have **already** ported the Alcyon-era assembler, linkers, and
utility tools to Win64.  Each preserves the Alcyon copyright header
verbatim and required only ANSI/C99 modernization plus 32→64-bit-host
fixes:

| Tool | In-tree location | Original source |
|---|---|---|
| `as68` | [68kTools/as68/](../../as68/) | Alcyon `as.c` family, ©1981–1983 |
| `lo68` | [68kTools/lo68/](../../lo68/) | Alcyon `lo68/main.c` ©1982–1983 |
| `link68` | [68kTools/link68/](../../link68/) | DRI `link68.c` ©1983 |
| `reloc` | [68kTools/reloc/](../../reloc/) | DRI `reloc.c` |
| `ar68`, `nm68`, `size68` | [68kTools/ar68/](../../ar68/) etc. | Alcyon `util/` |
| `alclib` (libget/libput/getchd/putchd) | [68kTools/alclib/](../../alclib/) | Alcyon `clib/` subset |

The same recipe applies to the four remaining Alcyon C-compiler
pieces (`cp68`, `c068`, `c168`, `c68` driver).  Once they exist as
`68kTools/c68/cp68/`, `68kTools/c68/c068/`, `68kTools/c68/c168/`,
and `68kTools/c68/cc/` (running on Windows under MSVC), we have a
fully self-contained **Win64 ⇒ DRI `.o`** C cross-compiler — the
same dialect (K&R, with `#define DRI` quirks) that the original
`MKDRI.C` will be authored in.  The Win64 `c68.exe` driver chains
them together exactly the way the v102a driver does on a VAX.

For the full port plan, see [../../c68/PORT_PLAN.md](../../c68/PORT_PLAN.md).

That toolchain stays in worm68k tree (no msys2, no winget, no
external prerequisite) and unblocks every future "we want this C
program to run on CP/M-68K" decision, not just mkdri.

### 2.3 What the user gets

Two artefacts ship from a single source tree:

1. **Win64 cross-compiler**: `c68.exe`, `cp68.exe`, `c068.exe`,
   `c168.exe`, plus `as68_43.exe` / `lo68_43.exe` (per the
   [c68 PORT_PLAN §4.3](../../c68/PORT_PLAN.md) naming scheme; the
   `_43` suffix avoids collision with the already-shipped
   `as68.exe` / `lo68.exe`).  `nuxi_43.exe` is also new (no
   in-tree equivalent).  The remaining utility binaries
   (`reloc.exe`, `ar68.exe`, `nm68.exe`, `size68.exe`,
   `sendc68.exe`) reuse the already-shipped in-tree versions —
   their c68/lo68/ source copies are *older* baselines than what
   is already in-tree.  Plus `clib.a` / `libe.a` / `libf.a`
   archives, all under `68kTools/builds/win64/bin/Release/`.
   Drop-in for the existing `asm68K` / `lo68` cross chain —
   `c68 foo.c` produces `foo.o` in DRI 0x601A format, immediately
   consumable by in-tree `lo68.exe` (or `lo68_43.exe`).
2. **Native CP/M-68K binary**: `MKDRI.68K`, built with that
   cross-compiler from `mkdri/native/*.c`.  Identical to what the
   historical on-target Alcyon `c68` 4.3 would have produced
   (same algorithm, same code-gen pass).  Self-hosts: once
   `MKDRI.68K` is on the disk image, rebuilding it from inside
   CP/M-68K just works.

The "language compatibility" concern raised earlier disappears:
because **we are the authors of `c68.exe` on Windows**, every C
source file in `mkdri/native/` is verified to compile by our own
cross-compiler on every host CI run, with no on-target sim68k boot
required.

---

## 3. Goals (and explicit non-goals)

### 3.1 Goals

Split into a **toolchain milestone** (T-series: port Alcyon C to
Win64) and a **mkdri milestone** (N-series: build `MKDRI.68K` with
it).

#### Toolchain milestone (T-series)

| # | Goal |
|---|---|
| T1 | `68kTools/c68/` builds cleanly under MSVC + clang-cl on x64 Windows, producing `cp68.exe`, `c068.exe`, `c168.exe`, `c68.exe`. |
| T2 | `c68.exe foo.c -o foo.o` produces a DRI 0x601A `.o` byte-identical to one produced by historical on-target `c68` for the same input (validated against the Alcyon `test/` regression set in `cpm68k/src/v102a/test/`). |
| T3 | The full Alcyon `test/` regression suite passes when driven from Win64 via `c68.exe` + in-tree `lo68_43.exe` + in-tree `reloc.exe`. |
| T4 | `clib`, `libe`, `libf` archives build (re-using the cross chain — `c68 -c` per object, in-tree `ar68.exe` for the archive). |

#### mkdri milestone (N-series)

| # | Goal | Phase |
|---|---|---|
| G1 | `MKDRI.68K` runs from the CCP and prints `--version` | N0 |
| G2 | `mkdri foo.REL` produces a `FOO.68K` byte-identical to `reloc foo.REL foo.68k` | N1 |
| G3 | `mkdri -b<hex> foo.REL` accepts the same `-b<hex>` syntax as the host tool and DRI's `reloc` | N1 |
| G4 | `mkdri -y[<sym>]` writes the host's symbol-map format alongside the absolute output | N1 |
| G5 | `mkdri foo.O` (DRI relocatable object — Alcyon `c68` / `as68` output) is rejected with the same wording as the host: "input is a relocatable object, run `lo68` first" | N1 |
| G6 | The 60 stock `.REL` files on the CP/M-68K v1.2 / v1.3 distribution disks pass the byte-identical Phase 3 round-trip vs host `mkdri` and historical `reloc` | N1 acceptance gate |
| G7 | `mkdri foo.coff` produces a CCP-loadable `FOO.68K`; matches the host's Phase 2 output byte-for-byte | N2 |
| G8 | `mkdri foo.elf` (`ET_EXEC`) produces a CCP-loadable `FOO.68K`; matches the host's Phase 1 output byte-for-byte | N3 |
| G9 | `mkdri -b<hex> foo.DYN` (`ET_DYN`, `R_68K_RELATIVE`) → `FOO.68K`; matches the host's Phase 4 output byte-for-byte | N4 |
| G10 | `mkdri -r foo.DYN` → `FOO.REL` that round-trips: `mkdri -r foo.DYN; reloc -b<x> foo.REL` ≡ `mkdri -b<x> foo.DYN` | N5 |
| G11 | Self-host: `MKDRI.68K` built on-target from its own C sources via `c68` / `as68` / `lo68` / `mkdri` (eating its own dog food in the last step) at TPA = 0x500 fits within the typical 56-KB TPA budget of a 128-KB CP/M-68K machine | N1 acceptance |

### 3.2 Non-goals

| # | Explicitly out of scope |
|---|---|
| N1 | A linker pass.  `mkdri` does not resolve symbols.  Inputs with unresolved externals are rejected with the same wording as the host (`unresolved symbol(s): foo, bar, baz`), exit 3. |
| N2 | A debugger / interactive REPL / file inspector.  Use `sid68k` or `nm68` for those. |
| N3 | Dynamic loading.  CP/M-68K's CCP loads absolute images only; that is what we produce. |
| N4 | A C++ runtime on CP/M-68K.  The native port is K&R / Alcyon C, *not* a clang-cl recompile of the host's C++17 sources.  Wherever the host code uses `std::vector`, `std::string`, exceptions, RAII, or lambdas, the native port substitutes a static buffer + length-prefixed character array idiom matching the existing `reloc.c` / `lo68/main.c` style. |
| N5 | A new file format.  All on-disk shapes (DRI `EX_MAGIC`, ELF Ehdr32, COFF F_EXEC, ELF `ET_DYN`'s `R_68K_RELATIVE`) match the host spec exactly. |
| N6 | A re-implementation of `alclib`.  The host's `alclib` `_lgetw` / `_lgetl` / `_lputw` / `_lputl` / `getchd` / `putchd` helpers were originally *ported from* the CP/M-68K library (`CLIB`); on the native side we use the equivalent `CLIB` entry points directly. |
| N7 | A re-implementation of Alcyon C.  We **port** v102a to Win64; we do not rewrite, refactor for clarity, or modernize the language it accepts.  The price of that discipline is that we get a self-hosting cross-compiler "for free" — see §2 above. |

### 3.3 Out-of-scope items that *may* land later

Tracked here so they aren't forgotten.

* Wildcard inputs (`mkdri *.REL`).  The host spec says one input file
  only; we keep that.
* `-q` / `-v` flags identical to the host.  Native always behaves as
  if `-q` is set (no per-call summary line), and `-v` becomes a no-op
  unless we wire up the same stderr stream the host uses.
* Recompiling under a third-party DRI-era K&R C (e.g. Sozobon C, the
  86-targeting Hendrix Small-C at `68kTools/smallc68k/`).  Possible
  but pointless once we own a working `c68.exe`.

---

## 4. Phasing

Two strictly ordered milestones (T then N), then six native phases.
The N-phases roughly mirror the host spec's phases (Phase 1 = ELF;
Phase 3 = DRI), but the native port **inverts the priority** and
lands **DRI first** because:

1. DRI is the canonical CP/M-68K input format — the most useful
   on-target.
2. The algorithm is already in `reloc.c` and already cross-compiles
   cleanly.  Net new code: a CLI driver plus the optional symbol-map
   dump.
3. ELF / COFF require ~1.5 KB each of parser code; we want a working
   tool first.

### 4.1 T-milestone (Alcyon C port to Win64)

Each T-phase is a CMake `add_subdirectory(c68/<pass>)` landing plus
a regression-test gate.

| Phase | Adds | What lands |
|---|---|---|
| T1 | `68kTools/c68/cp68/` | Win64 `cp68.exe`, K&R-to-ANSI fixes per the `as68` / `lo68` precedent.  Smoke test: preprocess `MKDRI.C` (Phase N1's source) and diff against a hand-preprocessed reference. |
| T2 | `68kTools/c68/c068/` (parser pass) | Win64 `c068.exe`.  Reads the `cp68`-output `.i` file, emits the intermediate `.1` file the cgen consumes.  Regression test: every `.c` in `v102a/test/` parses without diff against a captured "golden" intermediate. |
| T3 | `68kTools/c68/c168/` (cgen pass) + `68kTools/c68/cc/` (driver) | Win64 `c168.exe` and `c68.exe`.  Driver chains `cp68 → c068 → c168 → as68_43 → lo68_43`.  Acceptance: full Alcyon `test/` suite passes (T3 goal). |
| T4 | `68kTools/c68/clib/` plus `libe/` / `libf/` | Win64 `c68.exe -c` per object, in-tree `ar68.exe` archives `clib.a` / `libe.a` / `libf.a` (output formats already understood by `lo68`).  Acceptance: link a trivial `hello.c` against `clib.a` and run under sim68k CP/M-68K. |

The T-milestone is the **prerequisite for every N-phase**.  Once it
lands, the rest of the project (mkdri-native and anything else we
ever want to author in C for CP/M-68K) has a real toolchain.

### 4.2 N-milestone (mkdri-native itself)

| Phase | Adds | Approx LoC | Approx TPA | Acceptance gate |
|---|---|---|---|---|
| N0  | Skeleton: CLI parser, `-h` / `--version`, BDOS helpers, exit codes | 250 | 6 KB | `mkdri --version` matches host's first banner line |
| N1  | DRI input + DRI absolute writer + `-y` symbol map.  Re-uses ~80 % of `reloc.c` | 800 | 12 KB | G2, G3, G4, G6 |
| N2  | COFF input | 600 | 16 KB | G7 |
| N3  | ELF input (`ET_EXEC` only) | 900 | 20 KB | G8 |
| N4  | ELF `ET_DYN` + `R_68K_RELATIVE` → absolute DRI | 350 | 22 KB | G9 |
| N5  | DRI `.REL` writer (`-r`) | 300 | 24 KB | G10 |

TPA numbers are post-strip estimates with `lo68 -S`; the symbol table
is dropped from the shipped binary.  N1 is well below the 56-KB TPA
that a typical 128-KB CP/M-68K machine offers, leaving the user plenty
of room for read buffers (~16 KB) and the input-image buffer (up to
~16 KB).

### 4.2.1 Phase N0 — skeleton

Deliverables:

* `mkdri/native/main.c` — argv parsing (CP/M-68K BDOS-style cmdtail),
  switch decoding, dispatch stub.
* `mkdri/native/bdos.c` + `mkdri/native/bdos.h` — thin wrappers over
  `trap #2` (`P_TERMCPM`, `C_WRITE`, `F_OPEN`, `F_CLOSE`, `F_READ`,
  `F_WRITE`, `F_MAKE`, `F_DELETE`, `F_DMAOFF`) so the rest of the
  port can call BDOS the same way `reloc.c`'s host build calls
  `fopen` / `fread`.
* `mkdri/native/makefile-cross` — drives the in-tree `c68.exe` +
  `as68.exe` + `lo68.exe` + `reloc.exe` (bootstrap; later `mkdri.exe`)
  chain from Windows.
* `mkdri/native/makefile` — CP/M-68K-side nmake-syntax driver for the
  on-target self-host build.
* `mkdri/native/README.md` — pointer to this plan + the design doc.

Acceptance: `A>mkdri --version` prints the same first line as the
host (`mkdri 1.3 - ELF/COFF/DRI to DRI converter`) and exits 0.

### 4.2.2 Phase N1 — DRI input → absolute, with `-y`

Deliverables:

* `mkdri/native/dri_reader.c` — port of `mkdri/DriReader.cpp`,
  K&R style, using a single static buffer for the input file (size:
  ≤ `MAX_TPA - 8 KB`, configurable at build time).
* `mkdri/native/dri_writer.c` — port of `mkdri/DriWriter.cpp`'s
  absolute path.  Reuses `alclib`'s `putchd` (which is itself
  ported from CP/M-68K's `CLIB`).
* `mkdri/native/sym_writer.c` — port of `mkdri/SymFile.cpp`.
* Test: feed the 60-file `.REL` fixture set through both the new
  `MKDRI.68K` running under sim68k / `cpm.sys` and the host
  `mkdri.exe`; verify byte-identical outputs against historical
  `reloc.exe` (the existing acceptance gate for the host's Phase 3).

Done when: G2, G3, G4, G6, G11.  At this point the native port is
already a strict superset of historical `reloc` (adds `-y`).

### 4.2.3 Phase N2 — COFF input

Deliverables:

* `mkdri/native/coff_reader.c` — port of `mkdri/CoffReader.cpp`.
  COFF section headers + symbol table + per-section relocation list.

Done when: G7.

### 4.2.4 Phase N3 — ELF input (`ET_EXEC`)

Deliverables:

* `mkdri/native/elf_reader.c` Part 1 — `ET_EXEC` path only.  Walk
  `PT_LOAD` program headers to fill `text` / `data` / `bss`; walk
  `.symtab` to detect unresolved externals.
* CLI: add `-f elf` recognition.

Done when: G8.

Notes:

* The ELF parser is the largest single addition (~900 LoC) because
  Alcyon `c68` lacks `<stdint.h>` — we have to define our own
  `U16` / `U32` typedefs and call BE-readers explicitly (see design
  doc §5.2).
* Memory budget: an ELF header + program headers + section headers
  + name string table + symbol table + symbol-name strings can total
  ~16 KB for a 32-KB text-segment program (e.g. `bas68k.elf`).  This
  fits comfortably; we use a single 16-KB scratch arena.

### 4.2.5 Phase N4 — ELF `ET_DYN` + `R_68K_RELATIVE`

Deliverables:

* `mkdri/native/elf_reader.c` Part 2 — `ET_DYN` path.  Section-based
  extraction, `R_68K_RELATIVE` collection from `.rela.dyn`.
* `mkdri/native/dri_writer.c` extension — per-segment-bias path
  (already present on host; tiny port).

Done when: G9.

### 4.2.6 Phase N5 — DRI `.REL` writer (`-r`)

Deliverables:

* `mkdri/native/dri_rel_writer.c` — port of `mkdri/DriRelWriter.cpp`.

Done when: G10.

---

## 5. Acceptance criteria summary

A T-phase is **done** when:

1. The new `68kTools/c68/<pass>/` subdirectory builds under MSVC
   and clang-cl on Windows x64.
2. The pass produces byte-identical output (intermediate file or
   final object) versus a captured reference from the same Alcyon
   source running under sim68k.
3. The Alcyon `cpm68k/src/v102a/test/` regression suite passes
   (gated on T3+).

An N-phase is **done** when:

1. The host CI build (CMake + MSVC) still passes — host `mkdri.exe`
   is unaffected.
2. The new `MKDRI.68K` builds via two independent paths and produces
   byte-identical binaries:
   * **A:** in-tree Win64 `c68.exe` + `as68_43.exe` + `lo68_43.exe` +
     (bootstrap: in-tree `reloc.exe`; thereafter: `mkdri.exe`).
   * **B:** on-target CP/M-68K `c68.68k` + `as68.68k` + `lo68.68k`
     + `mkdri.68k`, run under sim68k.
3. `MKDRI.68K` runs to completion under sim68k's `cpm.sys` boot.
4. The phase's specific G-goals pass.

Phase **N6** is *not* a separate phase; the symbol-map dump (`-y`)
ships in Phase **N1** because the code is small (<60 lines) and we
want the auto-loadable symbol files to show up in the sid68k
workflow immediately.

---

## 6. Build & deployment

### 6.1 Toolchain build (T-milestone artefacts)

`68kTools/c68/CMakeLists.txt` adds five executables and three
static-archive build steps:

```cmake
add_subdirectory(c68/cp68)        # cp68.exe
add_subdirectory(c68/c068)        # c068.exe
add_subdirectory(c68/c168)        # c168.exe
add_subdirectory(c68/cc)          # c68.exe (driver)

# Cross-compilation steps producing .a archives consumable by lo68:
add_custom_target(clib68  COMMAND c68 -c ... && ar68 r clib.a ...)
add_custom_target(libe68  COMMAND c68 -c ... && ar68 r libe.a ...)
add_custom_target(libf68  COMMAND c68 -c ... && ar68 r libf.a ...)
```

All five executables stage to
`68kTools/builds/win64/bin/Release/`, next to `asm68K.exe`,
`lo68.exe`, `reloc.exe`, etc.  The standard
`tools/build/officialbuild.bat` orchestration calls them.

### 6.2 mkdri-native cross build (N-phase outputs, from Windows)

`mkdri/native/makefile-cross` runs the in-tree cross-toolchain from
end to end:

```make
ROOT  = $(WORM68KROOT)
BIN   = $(ROOT)\68kTools\builds\win64\bin\Release
C68      = $(BIN)\c68.exe                # T3 output
AS68_43  = $(BIN)\as68_43.exe            # T1 output
LO68_43  = $(BIN)\lo68_43.exe            # T2 output
RELOC    = $(BIN)\reloc.exe              # already-shipped in-tree
AS68  = $(BIN)\as68.exe
LO68  = $(BIN)\lo68.exe
RELOC = $(BIN)\reloc.exe
MKDRI = $(BIN)\mkdri.exe              # bootstrap: host C++ tool
CCFLAGS = -O -DCPM

OBJS = main.o bdos.o portab.o dri_reader.o dri_writer.o sym_writer.o

MKDRI.68K : MKDRI.REL
        $(MKDRI) -b500 MKDRI.REL MKDRI.68K     # later: eat own dog food

MKDRI.REL : $(OBJS)
        $(LO68_43) -L 500 $(OBJS) -lclib -o MKDRI.REL

.c.o :
        $(C68) $(CCFLAGS) -c $<
```

`MKDRI.68K` is byte-identical to a CP/M-68K-native build because both
go through the same `c68` source.

### 6.3 On-target build (canonical self-host)

Once `MKDRI.68K` is on the disk image:

```
A>cd MKDRI
A>c68 -c main.c
A>c68 -c bdos.c
... (repeat for each .c)
A>lo68 -L 500 main.o bdos.o ... -lclib -o MKDRI.REL
A>mkdri -b500 MKDRI.REL MKDRI.68K        (replaces RELOC.68K)
```

The on-target `c68` chained tools used here are themselves built
*by the Win64 `c68.exe`* from the same v102a source — guaranteeing
identical code-gen across host and target.

### 6.4 Deployment

`MKDRI.68K` plus `MKDRI.SYM` is copied into the standard `bin/`
directory and into the sim68k worm68k-8m disk image alongside the
host-built `BAS68K.68K`, `TBASIC.68K`, etc.  The existing
`bas68k`-style `deploy:` target in the parent makefile is extended
to include it.

---

## 7. Risks and mitigations

| # | Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|---|
| TR1 | Alcyon `cp68` / `c068` / `c168` carry the same 32→64-bit and K&R-to-ANSI defects we saw in `lo68/main.c` (see `dri-link-chain.md`) | High (the defects are systematic across the era) | Per-pass debugging | Apply the same fix recipe used for `as68` / `lo68`: keep K&R declarations where safe, retype `long` loop counters to `int32_t`, and run a clang-cl `/W3` pass to find the obvious offenders.  Hold each `.c` file's diff to the v102a original as small as possible so upstream bug fixes are reviewable. |
| TR2 | Alcyon's `mdriver.h` / `def.h` use single-letter Hungarian-ish macros that collide with modern `<stdio.h>` symbols | Med | Compile-time clashes | `#define DRI 1` at the top of every Alcyon `.c` per the v102a build conventions; on Win64 also `#define ALCYON_HOST 1` and add a `c68_compat.h` shim that selectively renames the collisions.  We can crib the renaming list from the WHITE (Whitesmith) and DECC (DEC C on VMS) source-conditional blocks that already exist in v102a. |
| TR3 | Alcyon `cgen` emits Motorola syntax that asm68K (modern) rejects | Low — same dialect as `as68` already accepts | Phase blocker if hit | The in-tree `as68.exe` is itself the Alcyon assembler; it accepts the Alcyon mnemonic set by construction.  We do **not** route c68 output through `asm68K` — we route it through `as68_43`. |
| TR4 | Alcyon `cgen` output is wired to a specific Alcyon `clib` ABI; mixing with `m68k-elf` libgcc would not work | Med | Constrains the C runtime we ship | Use the in-tree Alcyon `clib` from `cpm68k/src/v102a/clib/` (ported alongside the compiler).  Do not mix runtimes. |
| R1 | TPA blowout in Phase N3 (ELF reader pushes us past 56 KB) | Med | Build-blocker on real 128-KB machines | Strip the symbol-name table after the unresolved-externals check passes — we never write it out anyway.  Free the program-headers buffer before reading sections.  Reuse the same 16-KB scratch arena. |
| R2 | `c68` (the Alcyon dialect) is K&R only — no function prototypes, no `void *`, no `<stdint.h>` | High (it's the language we just ported) | All-pervasive coding-style impact | Treat this as a hard constraint from day one (see design §3.4).  Adopt a `portab.h`-style typedef set up front.  Our Win64 `c68.exe` accepts the same K&R style, so the constraint is uniform across host-test and on-target. |
| R3 | We ship a buggy Win64 `c68.exe` and miscompile `MKDRI.C` | Low after T3 acceptance | High if hit | The T3 regression-suite gate (Alcyon `v102a/test/`) is broad and battle-tested — historically used to qualify every DRI release.  Plus: every N-phase compares the byte-exact output of `MKDRI.68K` against the host `mkdri.exe`'s output on the same input, which would catch a code-gen miscompile immediately. |
| R4 | BDOS file I/O is record-oriented (128-byte sectors); our parsers want byte streams | High | Pervasive but routine | Wrap BDOS `F_READ` / `F_WRITE` in `_lgetb` / `_lgetw` / `_lgetl` shims with a single 128-byte buffer per open file.  This is the `alclib` model — port it. |
| R5 | CP/M-68K BDOS clobbers D0/D1/A0/A1 (see `m68k-cabi-clobbers.md`) | High | Subtle bugs at the C/asm boundary | All BDOS wrappers in `bdos.c` save/restore the caller-save set defensively, matching the convention already documented in user memory. |
| R6 | Filename limit is 8.3 — output names like `foo.elf.68K` would truncate to `FOO.ELF` | Med | Default-output handling needs care | The CLI default-output logic uppercases and re-applies the 8.3 rule the same way the host does on Windows (already in `mkdri/main.cpp`).  Port that verbatim. |
| R7 | sim68k's CP/M-68K mode is not byte-perfect | Low | False negatives in acceptance | We already use sim68k to validate `BAS68K.68K`, `TBASIC.68K`, and `RELOC.68K` — sim68k is a known-good test substrate.  Bug-trap: also smoke-test under qemu-system-m68k or a real CP/M box if available. |
| R8 | `R_68K_RELATIVE` parsing requires reading a `.rela.dyn` section that may be larger than the 16-KB scratch arena for big binaries (e.g. self-host `bas68k`) | Med | Phase N4 blocker on real ET_DYN inputs | Stream-process the relocation table: read 12 bytes (one `Elf32_Rela`) at a time, apply, discard.  Don't ever materialize the full table in memory. |
| R9 | The host symbol-map format (`-y`) was added after the original `reloc` and is not present on real CP/M-68K | Low | None — we're authoring the format | We control both producer (Phase N1) and consumer (`sid68k`'s `Y` command).  The format is text and trivially backward-compatible. |
| R10 | The native build's `-r` writer (Phase N5) must produce a `.REL` byte-identical to a host-built `.REL` to satisfy the byte-identical round-trip requirement | High | Acceptance for G10 | Drive the test from a fixed set of `.DYN` fixtures committed alongside the test files; diff the two outputs byte-for-byte.  Same fixtures the host's `mkdri` already round-trips against. |

The big change from earlier drafts: **no external toolchain
dependency**.  No msys2.  No winget.  No `m68k-elf-gcc`.  No "find
a working `c68` somewhere".  We own the entire stack from `.c` to
`.68K`, and every component compiles under MSVC on a stock Windows
worm68k checkout.

---

## 8. Schedule and ordering

No schedule estimates per project policy.  Strict dependency order:

```
T1  ──►  T2  ──►  T3  ──►  T4
                            │
                            ▼
N0  ──►  N1  ──►  N2  ──►  N3  ──►  N4  ──►  N5
                  │
                  └─── (N2 and N3 are independent; either order works,
                       parallel work allowed)
```

N4 strictly depends on N3 (it extends the ELF reader).  N5 strictly
depends on N4 (same `Reloc` records).

The natural shipping order is therefore T1→T2→T3→T4→N0→N1→(N2 ∥ N3)
→N4→N5, with each phase's acceptance gate green before the next
starts.

---

## 9. Open questions

1. **Do we want a `-r` flag in Phase N1 already, so the unsupported
   path emits a clear "Phase N5 not yet implemented" instead of
   `unknown option '-r'`?**  Recommended: yes; matches host CLI shape
   from day one and avoids breaking a script that wants to be portable
   across host and native.

2. **Should the native `MKDRI.SYM` files be 8.3-name aware** (i.e.
   never emit a sym basename longer than 8 chars) **when the input
   has long names?**  Decision: keep full names in the file content
   — only the filename itself is constrained.  sid68k already accepts
   long names inside the file.

3. **Bootstrap question:** How do we build the very first
   `MKDRI.68K`?  Three valid orderings:

   * **A** (canonical) Cross-build with Win64 `c68.exe` + in-tree
     `as68_43.exe` + in-tree `lo68_43.exe` + in-tree `reloc.exe`.
     This is the CI path.  Available the moment the T-milestone
     completes.
   * **B** Use on-target Alcyon `c68.68k` from the CP/M-68K v1.3
     distribution disks.  Available the moment the user has a
     working CP/M-68K system; the on-target binary is itself
     produced by path A from the v102a sources.
   * **C** (transitional) Use the v1.3 disks' original `CC.68K` +
     `LO68.68K` + `RELOC.68K` as a sanity check.

   Decision: **A** is canonical for CI; **B** is the documented
   self-host path; **C** is a one-time sanity check.  Either path
   produces an identical `MKDRI.68K` because they all go through
   the same Alcyon code-gen.

4. **Should the host CMake target depend on `c68`?**  Decision: no.
   Host `mkdri.exe` (C++17) is the official host implementation and
   has no dependency on `c68`.  CMake builds `c68` only when the
   user opts in by selecting the `c68` subdirectory in the top-level
   build, or when the mkdri-native target is requested.

5. **Should the Alcyon C source tree live under `68kTools/c68/` or
   stay co-located with the OS sources at `cpm68k/src/v102a/`?**
   Recommended: copy-with-modifications into `68kTools/c68/`,
   preserving the `cpm68k/src/v102a/` originals as untouched
   reference.  Same pattern we used for `as68` (port) vs. v1.2's
   `dist/9` (stubs).  Modifications get tracked with git diff
   against `v102a/`.  The `org*/` subdirectories within `v102a/`
   are pristine pre-DRI baselines and stay where they are — they
   are useful when investigating an Alcyon-vs-DRI behavior
   difference at the `git blame` level.

---

## 10. Definition of done (entire native port)

The native port is **done** when all of the following are true:

1. T1–T4 have shipped: a Win64 `c68.exe` + `as68_43.exe` +
   `lo68_43.exe` chain exists in-tree with passing Alcyon `test/`
   regressions.  See [../../c68/PORT_PLAN.md](../../c68/PORT_PLAN.md)
   for the standalone port plan.
2. N0–N5 have shipped (all `G*` goals satisfied).
3. `cpm68k/src/v1.3` and `cpm68k/src/v1.2` build under the on-target
   workflow without `reloc.exe` participating: the user can replace
   `reloc` with `mkdri` in every `RELOCn.SUB` file on the
   distribution disks and rebuild cleanly.
4. `mkdri/native/test/` contains regression fixtures that exercise
   each phase, runnable under sim68k via `tools/build/` scripts.
5. `MKDRI.68K` deploys to the sim68k `worm68k-8m` image and shows up
   in CCP `A>DIR`.
6. The companion design document
   [MKDRI_NATIVE_DESIGN.md](MKDRI_NATIVE_DESIGN.md) is updated to
   reflect any divergence from this plan that arose during
   implementation.

---

*End of native port plan.*
