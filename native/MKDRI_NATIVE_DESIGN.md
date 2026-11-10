# `mkdri` — CP/M-68K Native Port: Technical Design

Date: 2026-06-12
Author: GitHub Copilot (for review)
Status: **Draft** — implementation has not started.

The standalone host build is described in [README.md](../README.md).
Other toolchain references below remain in the `worm68k` repository.

Companion documents:
* [MKDRI_NATIVE_PLAN.md](MKDRI_NATIVE_PLAN.md) — phasing and acceptance.
* [../MKDRI_SPEC.md](../MKDRI_SPEC.md) — the parent (host) specification.
* [reloc/reloc.c](https://github.com/dougdu/worm68k/blob/main/68kTools/reloc/reloc.c) — historical reference port.
* [asm68K/DRI_OBJECT_FORMAT.md](https://github.com/dougdu/worm68k/blob/main/68kTools/asm68K/DRI_OBJECT_FORMAT.md)
  — DRI `c.out` on-disk layout.
* [bas68k/basrtm-cpm/BASRTM_CPM_PLAN.md](https://github.com/dougdu/worm68k/blob/main/68kTools/bas68k/basrtm-cpm/BASRTM_CPM_PLAN.md)
  — exhaustive CP/M-68K BDOS reference and call conventions; this
  port follows the same conventions.

This document specifies **how** the native port is structured.
**What** it implements and **when** is in
[MKDRI_NATIVE_PLAN.md](MKDRI_NATIVE_PLAN.md); the spec the user
ultimately sees is [../MKDRI_SPEC.md](../MKDRI_SPEC.md).

---

## 1. Target environment

### 1.1 Operating system

CP/M-68K v1.3 (Jun 1984) — and earlier v1.2 — running on a 68000
or later, big-endian, supervisor + user split as documented in the
*CP/M-68K Programmer's Guide* (Jun 1983 / Jun 1984).

Memory map seen by `MKDRI.68K`:

```
$0000_0000  ┐
            │ 68000 exception vectors (1024 bytes, OS-owned, R/W)
$0000_0400  ┤
            │ CP/M-68K zero page / base page (256 bytes, per program)
            │   $0000_0480: cmdtail length byte
            │   $0000_0481: cmdtail bytes, $-terminated
$0000_0500  ┤
            │ Program text (`ch_entry`)   ───┐
            │ Program data                   │  produced by `lo68 -L 0x500`
            │ Program bss                    │  + this same `reloc -b500 ...`
            │                            ────┘
            │ Heap (machine heap from `_HeapInit` if used) — grows up
            │ ⋮
            │ User stack — grows down
$0000_xxxx  ┤  USP = hightpa on entry, per BASRTM-CPM startup convention
            │ BDOS / BIOS — owns the top of RAM
$0001_FFFF  ┘
```

Typical CP/M-68K configurations have **128 KB** total RAM with a
**~56 KB TPA** after the BDOS/BIOS and base page take their share.
The shipped `MKDRI.68K` (~24 KB text + data) leaves ~32 KB for
input buffers and per-file scratch.  Worst-case input today is
`BAS68K.ELF` (~30 KB) — see §6.2 for the buffer-sizing
strategy that keeps us under budget.

### 1.2 Toolchain

The native port is **K&R / Alcyon C**, the dialect understood by the
v1.03 Alcyon compiler chain.  Crucially, the worm68k tree carries
**two** parallel realizations of that toolchain:

* **On-target binaries** (historical) on the CP/M-68K v1.3
  distribution disks `DISK3` / `DISK5`:

  | Tool | Disk | Role |
  |---|---|---|
  | `CP68.68K` | DISK5 | C preprocessor (`.c` → `.i`) |
  | `C068.68K` | DISK5 | C front end (`.i` → `.1`) |
  | `C168.68K` | DISK5 | C code generator (`.1` → `.s`) |
  | `AS68.68K` | DISK3 | assembler (`.s` → `.o`) |
  | `LO68.68K` | DISK3 | linker (`.o*` → `.REL`) |
  | `RELOC.68K` | DISK6 | re-base (`.REL` → `.68K`) — replaced by `MKDRI.68K` once N1 lands |
  | `CLIB` (lib) | DISK3 | C standard library (the original of `alclib`) |

* **Host-side Win64 binaries** (the ones we are *building* in the
  T-milestone described in [MKDRI_NATIVE_PLAN.md §2](MKDRI_NATIVE_PLAN.md)),
  staged under `68kTools/builds/win64/bin/Release/`:

  | Tool | In-tree source | Origin |
  |---|---|---|
  | `cp68.exe` | `68kTools/c68/cp68/` | port of [cpm68k/src/v102a/cp68/](https://github.com/dougdu/worm68k/tree/main/cpm68k/src/v102a/cp68/) |
  | `c068.exe` | `68kTools/c68/c068/` | port of [cpm68k/src/v102a/c068/](https://github.com/dougdu/worm68k/tree/main/cpm68k/src/v102a/c068/) |
  | `c168.exe` | `68kTools/c68/c168/` | port of [cpm68k/src/v102a/c168/](https://github.com/dougdu/worm68k/tree/main/cpm68k/src/v102a/c168/) |
  | `c68.exe` (driver) | `68kTools/c68/cc/` | port of [cpm68k/src/v102a/cc/](https://github.com/dougdu/worm68k/tree/main/cpm68k/src/v102a/cc/) |
  | `as68_43.exe` | `68kTools/c68/as68/` | port of [cpm68k/src/v102a/as68/](https://github.com/dougdu/worm68k/tree/main/cpm68k/src/v102a/as68/) (suffix avoids collision with the existing [as68/](https://github.com/dougdu/worm68k/tree/main/68kTools/as68/) C++ port) |
  | `lo68_43.exe` | `68kTools/c68/lo68/` | port of [cpm68k/src/v102a/lo68/](https://github.com/dougdu/worm68k/tree/main/cpm68k/src/v102a/lo68/) (suffix avoids collision with [lo68/](https://github.com/dougdu/worm68k/tree/main/68kTools/lo68/)) |
  | `reloc.exe` | [reloc/](https://github.com/dougdu/worm68k/tree/main/68kTools/reloc/) | already shipped (used by both the existing `bas68k`/`tbasic` chain and by the new c68 driver — the c68/lo68/reloc.c is an older 1981 baseline that we do **not** build) |
  | `clib.a` | `68kTools/c68/clib/` | port of [cpm68k/src/v102a/clib/](https://github.com/dougdu/worm68k/tree/main/cpm68k/src/v102a/clib/) |

  These produce DRI 0x601A object files indistinguishable from those
  built on-target by the historical `cp68`/`c068`/`c168`/`as68`/`lo68`
  chain.  CI builds `MKDRI.68K` through this path; the on-target
  build is a verification step rather than a primary pipeline.

> Implementation note: Alcyon C as defined by v102a (the 4.3
> compiler from January 1984) rejects `void *`, `<stdint.h>`,
> function prototypes (it expects K&R declarations and `extern`
> decls without a parameter list), and `static` initializers of
> non-scalar aggregates.  All native sources must conform to that
> dialect on **both** sides — because the Win64 `c68.exe` is
> literally the same parser/codegen as the on-target `c068`/`c168`,
> just running on a modern host.  See §3.4.

### 1.3 Run-time conventions

* **Entry frame** as documented in
  [sid68k/SID68K_PGMLOAD_ENTRY_FRAME_DESIGN.md](https://github.com/dougdu/worm68k/blob/main/68kTools/sid68k/SID68K_PGMLOAD_ENTRY_FRAME_DESIGN.md)
  and `cpm68k/Documents/CPM-68K_Programmers_Guide_Jun83.txt §4.1`:
  the CCP loads the image, sets USP = hightpa, pushes the base-page
  pointer, and `jsr`s to `ch_entry`.  The C startup (from `CLIB`)
  consumes the base page, sets up `argc` / `argv` from `cmdtail`,
  installs `_exit` via BDOS 0, and calls `main()`.
* **BDOS calls** via `trap #2`, with the convention summarized in
  [bas68k/basrtm-cpm/BASRTM_CPM_PLAN.md §3.1](https://github.com/dougdu/worm68k/blob/main/68kTools/bas68k/basrtm-cpm/BASRTM_CPM_PLAN.md).
  Caller-save: D0, D1, D2, A0, A1.  Callee-save: D3-D7, A2-A6.
  See user memory `m68k-cabi-clobbers.md` for the painful empirical
  details.
* **Stack discipline**: the `CLIB` startup leaves us with ≥ 4 KB of
  stack between USP and the top of the heap arena.  We never recurse
  more than 4 deep (CLI dispatch → reader → relocation loop) so this
  is comfortable.

---

## 2. Component map

The native port mirrors the host source tree, with one C file per
host C++ file.  C++ classes become K&R structs; methods become
`<file>_<method>` C functions; templates and `std::vector` become
fixed-size arrays plus a separately tracked `int count`.

```
68kTools/mkdri/native/
    MKDRI_NATIVE_PLAN.md         this folder, the plan
    MKDRI_NATIVE_DESIGN.md       this folder, the design (this file)
    README.md                    pointers to both
    makefile                     CP/M-68K-side nmake-compatible
    makefile-cross               host CI driver
    main.c                       CLI parser + dispatch          (host: main.cpp)
    image.h                      canonical image struct         (host: Image.h)
    bdos.c / bdos.h              BDOS wrappers (no host equiv)
    portab.h                     U8/U16/U32 typedefs, BE accessors
    elf_reader.c / elf_reader.h  Phase N3+N4                    (host: ElfReader.{h,cpp})
    coff_reader.c / coff_reader.h  Phase N2                     (host: CoffReader.{h,cpp})
    dri_reader.c / dri_reader.h  Phase N1                       (host: DriReader.{h,cpp})
    dri_writer.c / dri_writer.h  Phases N1+N4                   (host: DriWriter.{h,cpp})
    dri_rel_writer.c / .h        Phase N5                       (host: DriRelWriter.{h,cpp})
    sym_writer.c / sym_writer.h  Phase N1                       (host: SymFile.{h,cpp})
    test/
        run_n1.sub               CP/M-68K SUB script, Phase N1 fixtures
        fixtures/                input .REL files
            cpm.rel              (the largest v1.2 .REL)
            hello.rel
            ...
```

Each `.c` file is independently compilable under both `c68` and
host `clang-cl /TC` (smoke check in CI).

---

## 3. Coding-style invariants

### 3.1 Strict K&R declarations

```c
/* Function definition: K&R, types declared between the closing
   paren and the opening brace.  c68 accepts BOTH this and ANSI,
   but the v1.2 cp68 *only* accepts K&R, so we use K&R for
   portability across both distribution disks. */

int read_dri_header(fp, hdr)
    FILE *fp;
    HDR2 *hdr;
{
    ...
}
```

Header declarations are extern-only (no parameter lists):

```c
extern int read_dri_header();        /* int read_dri_header(FILE*, HDR2*) */
```

### 3.2 Typedefs

`portab.h` centralizes the fixed-width types and the big-endian
accessors:

```c
/* portab.h — shared by every .c in the native port */
typedef unsigned char  U8;
typedef unsigned short U16;
typedef unsigned long  U32;
typedef          long  S32;
typedef          short S16;
typedef          char  S8;
#define ERROR  (-1)
#define SUCCESS  0

U16  be16( /* U8 *p */ );
U32  be32( /* U8 *p */ );
void stbe16(/* U8 *p, U16 v */ );
void stbe32(/* U8 *p, U32 v */ );
```

`portab.h` is hand-aligned with the host's `Image.h` enum values
(`SecId::Text == 1`, etc.) so that the two implementations can share
fixture descriptions without translation.

### 3.3 No `void *`

`c68` chokes on `void *`.  We use `char *` as the generic-pointer
shim and cast at use sites (the K&R convention).  The compile-time
type-erasure that the host's C++ `Image` enjoys (`std::vector<Reloc>`)
is replaced with a flat `Reloc relocs[MAX_RELOCS]` and an int
`reloc_count`.

### 3.4 No `static` aggregate initializers beyond scalar

```c
/* OK */
static U16 magic = 0x601A;

/* NOT OK on Alcyon c68 (per v102a c068 parser) */
static struct { U16 a; U16 b; } init = { 0x601A, 0 };

/* Workaround: explicit at-startup initialization */
static struct { U16 a; U16 b; } init;
void init_globals() { init.a = 0x601A; init.b = 0; }
```

The CLI dispatcher calls `init_globals()` once before any other code.

### 3.5 No bitfields, no `enum` typedef

`c68` accepts `enum` but treats them as `int`.  We use `#define`
macros (matching `reloc.c`'s style) for compactness:

```c
#define DABS    0
#define DRELOC  1
#define TRELOC  2
#define BRELOC  3
#define EXTVAL  4
#define LUPPER  5
#define EXTREL  6
#define INSABS  7
```

### 3.6 Defensive register save/restore at C/asm boundaries

Per user memory `m68k-cabi-clobbers.md`: every BDOS wrapper in
`bdos.c` saves D2 (caller-save under c68 ABI, but trashed by some
BDOS implementations) before issuing `trap #2` and restores it after.

```asm
* bdos_open(fcb)
*   d1.l = ptr to FCB
*   returns d0.w = 0 success / 0xFF failure
_bdos_open:
        movem.l d2/a2-a6,-(sp)        * save callee-save we touch
        move.l  4(sp),d1               * arg: fcb
        ; c68 caller saved d0/d1/d2/a0/a1, but we belt+braces
        move.w  #15,d0                 * F_OPEN
        trap    #2
        ext.l   d0
        movem.l (sp)+,d2/a2-a6
        rts
```

`bdos.c` cannot reach down to push individual registers without
inline asm; instead each BDOS thunk lives in `bdos_asm.s` (hand-asm)
and `bdos.c` only declares them `extern`.  This mirrors the
`basrtm-cpm` pattern.

---

## 4. CLI parsing

### 4.1 Source of `argc` / `argv`

CP/M-68K's CCP places the command tail (everything after the program
name, max 126 chars) at base-page offset $80.  The C startup in
`CLIB` already tokenizes this on whitespace and exposes it as
classical `argc`/`argv`:

```c
int main(argc, argv)
    int argc;
    char **argv;
{
    ...
}
```

We use the standard form.  `argv[0]` is *not* the program name on
CP/M-68K v1.3 — it is the first token of the command tail, if any —
so the CLI driver never relies on `argv[0]` (a tiny divergence from
the host).  Help text refers to the program as `MKDRI` literally.

### 4.2 Option set

Identical to host (see [../MKDRI_SPEC.md §3](../MKDRI_SPEC.md)) with
only one user-visible difference: paths are uppercased (CCP folds
them anyway) and `-y` defaults to `<basename>.SYM` rather than `.sym`.

```
MKDRI [-bHEX] [-O OUTPUT] [-F AUTO|ELF|COFF|DRI] [-DHEX] [-BHEX]
      [-SHEX] [-R] [-Y[SYMFILE]] [-Q|-V] INPUT
```

### 4.3 Parsing implementation

A straight port of `mkdri/main.cpp`'s `parseCli()` (linear scan over
`argv`).  No `getopt` — `c68` doesn't bundle it and the option set
is small.  `parseHex()` is a verbatim port of `reloc.c`'s `decode()`
(see [reloc/reloc.c lines 196–250](https://github.com/dougdu/worm68k/blob/main/68kTools/reloc/reloc.c)).

### 4.4 Exit codes

Match host exactly (see [../MKDRI_SPEC.md §3 "Exit codes"](../MKDRI_SPEC.md)):

```c
#define EX_OK         0
#define EX_BADCLI     1
#define EX_BADFORMAT  2
#define EX_UNRESOLVED 3
#define EX_INTERNAL   4
#define EX_IO         5
```

Returned to the CCP via the `CLIB` startup's `_exit(rc)`, which
issues BDOS 0 (`P_TERMCPM`).

---

## 5. Internal image and pipeline

### 5.1 `Image` (canonical, format-agnostic)

```c
/* image.h — mirrors host Image.h */

#define MAX_SYMBOLS  512        /* see §6.2 budget table */
#define MAX_RELOCS  4096
#define MAX_TEXT   24576UL
#define MAX_DATA    8192UL

#define SEC_UNDEF 0
#define SEC_TEXT  1
#define SEC_DATA  2
#define SEC_BSS   3
#define SEC_ABS   4

#define REL_ABS32 0
/* (no other kinds needed: COFF/ELF readers normalize to ABS32; see
   host Image.h::Reloc::Kind comment about Phase 4) */

typedef struct {
    char    name[16];           /* truncated to 15+NUL — see §6.3 */
    U32     value;
    U8      section;
    U8      isUndefined;
    U8      isWeak;
    U8      pad;
} SYMBOL;

typedef struct {
    U8      kind;               /* REL_ABS32 always */
    U8      inSection;
    U8      targetSection;
    U8      pad;
    U32     offset;
    S32     addend;
} RELOC;

typedef struct {
    U32     textVA;
    U32     dataVA;
    U32     bssVA;
    U32     entry;
    U32     bssSize;

    U32     textLen;
    U32     dataLen;
    U8      text[MAX_TEXT];
    U8      data[MAX_DATA];

    int     symCount;
    SYMBOL  symbols[MAX_SYMBOLS];

    int     relCount;
    RELOC   relocs[MAX_RELOCS];

    U8      hasRelocs;
} IMAGE;

extern IMAGE g_image;           /* single global, allocated in BSS */
```

`g_image` lives in BSS — ~64 KB if we use the upper bounds above,
which is too big.  See §6.2 for the actual budget; we shrink
`MAX_TEXT` / `MAX_DATA` / `MAX_RELOCS` to fit, and reject inputs
that exceed at startup with a clear "image too large for native
mkdri; rebuild on host" message.

### 5.2 BE accessors

`portab.h`'s `be16` / `be32` are a direct copy of the host code:

```c
U16 be16(p)
    U8 *p;
{
    return ((U16)p[0] << 8) | p[1];
}

U32 be32(p)
    U8 *p;
{
    return ((U32)p[0] << 24) | ((U32)p[1] << 16) |
           ((U32)p[2] <<  8) |  (U32)p[3];
}
```

68000 is big-endian, so a 16-bit `move.w` from a word-aligned address
would produce the same result.  We use the byte-level form for
portability and to avoid alignment traps on the data path (the input
file is read into BSS, which is word-aligned, but record boundaries
inside the file are not always aligned — e.g. ELF symbols at offset
4 inside a 16-byte record).

### 5.3 Pipeline shape

Identical to host (see [../MKDRI_SPEC.md §5](../MKDRI_SPEC.md)):

```
+-----------+        +----------+        +----------------+
|  Reader   |  --->  |  IMAGE   |  --->  |  Writer        |
| (ELF /    |        |          |        | (DRI absolute  |
|  COFF /   |        |          |        |  or DRI .REL)  |
|  DRI)     |        |          |        |                |
+-----------+        +----------+        +----------------+
                           │
                           └──► sym_writer (optional, -y)
```

The reader fills `g_image`; the writer reads it.  `g_image.text` /
`g_image.data` are written *in place* by the writer's reloc-apply
loop, then dumped to disk.  No intermediate copy.

---

## 6. BDOS file I/O

### 6.1 FCB-backed stream wrappers

CP/M-68K BDOS file I/O is **record-oriented** (128-byte sectors,
single DMA address, sequential or random by record number).  Modern
C code wants byte streams.  We bridge with a small per-FILE struct
that owns one 128-byte DMA buffer:

```c
/* file.h */

#define IO_BUFSZ 128

typedef struct {
    U8   fcb[36];           /* CP/M FCB layout */
    U8   buf[IO_BUFSZ];
    int  bufPos;            /* next byte to read from buf */
    int  bufFill;           /* valid bytes in buf */
    U32  filePos;           /* bytes consumed so far */
    U8   mode;              /* 'r' or 'w' */
    U8   eof;
    U8   err;
} FILE_CPM;

FILE_CPM *cpm_fopen( /* char *path, char *mode */ );
int       cpm_fclose(/* FILE_CPM * */ );
int       cpm_lgetb( /* FILE_CPM * */ );      /* returns int (-1 EOF) */
U16       cpm_lgetw( /* FILE_CPM * */ );
U32       cpm_lgetl( /* FILE_CPM * */ );
int       cpm_lputb( /* FILE_CPM *, U8 b */ );
int       cpm_lputw( /* FILE_CPM *, U16 w */ );
int       cpm_lputl( /* FILE_CPM *, U32 l */ );
int       cpm_fread(  /* FILE_CPM *, U8 *dst, U32 n */ );
int       cpm_fwrite( /* FILE_CPM *, U8 *src, U32 n */ );
int       cpm_fseek(  /* FILE_CPM *, U32 absOffset */ );
U32       cpm_ftell(  /* FILE_CPM * */ );
```

`cpm_lgetw` / `cpm_lputl` etc. are big-endian (matching `alclib`),
because every on-disk format we touch is big-endian.

The implementation refills `buf` from the FCB whenever `bufPos ==
bufFill` (read mode) and flushes whenever `bufFill == IO_BUFSZ`
(write mode).  This is the same shim `alclib` provides on the host,
ported back to its CP/M-68K origin.

Allocation policy: `FILE_CPM` instances are statically allocated
(2 at startup — one input, one output) rather than malloc'd, to keep
the binary self-contained.  We never have more than 3 simultaneously
open (input + DRI output + symfile output).

### 6.2 Memory budget (per Phase)

| Phase | Image struct | Per-file FILE_CPM | Stack | Code+static | Total | TPA fit (56 KB)? |
|---|---|---|---|---|---|---|
| N0 | 0 | 0 | 4 KB | 6 KB | 10 KB | yes |
| N1 | 36 KB | 0.5 KB × 3 = 1.5 KB | 4 KB | 12 KB | 53.5 KB | tight |
| N2 | 36 KB | 1.5 KB | 4 KB | 16 KB | 57.5 KB | **overflow** |
| N3 | 36 KB | 1.5 KB | 4 KB | 20 KB | 61.5 KB | **overflow** |
| N4 | 36 KB | 1.5 KB | 4 KB | 22 KB | 63.5 KB | **overflow** |
| N5 | 36 KB | 1.5 KB | 4 KB | 24 KB | 65.5 KB | **overflow** |

**Phase N2 and later overflow** the default 56-KB TPA at the current
buffer sizes.  Mitigation, applied at the Phase N2 boundary:

1. Reduce `MAX_TEXT` from 24 KB to 16 KB and `MAX_DATA` from 8 KB to
   4 KB.  Reject larger inputs with `image too large for native
   mkdri; please re-run mkdri.exe on the host` and exit 4.  This
   reclaims 12 KB.
2. Reuse one 8-KB scratch arena (`g_scratch[8192]`) for the ELF
   string table, the COFF string table, the ELF program-header
   table, and the COFF section-header table — not concurrently.
   This reclaims another 4-6 KB versus per-table buffers.
3. Strip `MAX_SYMBOLS` to 256 (down from 512).  Real-world C/ASM
   programs we deploy have <120 globals; 256 is comfortable headroom.

Post-mitigation budget for Phase N5:

| Component | Bytes |
|---|---|
| g_image (text 16K + data 4K + structures 4K) | 24 KB |
| g_scratch (multi-purpose 8K arena) | 8 KB |
| 3× FILE_CPM | 1.5 KB |
| stack | 4 KB |
| code + static (Phase N5 maximum) | 16 KB |
| **Total** | **53.5 KB** |

Leaves 2.5 KB of headroom in the 56 KB TPA — enough margin for the
BDOS to grow on slightly more recent CP/M-68K variants.  If the user
has a larger TPA (e.g. 64 KB on a "fat" machine), the extra space is
unused.

### 6.3 Symbol-name truncation

CP/M-68K disk filenames are 8.3, but DRI symbol names are 8 chars
flat (`DRISymbol.h`).  The host symbol-map format permits long names
inside the file content.  Our `SYMBOL.name[16]` matches the DRI 8-char
storage plus margin for "long" symbol names that may come from ELF
input (typical: `_HeapAlloc`, `__rt_ExitBasic` — both ≤ 14).

On the few symbols longer than 15 chars we truncate **and append `~`**
to the truncated name so the user can spot it and know what happened.
This mirrors the standard 8.3 + `~N` Windows shortname convention.

The truncation is *only* in `SYMBOL.name`; the on-disk DRI symbol
table is left untouched (the writer drops it anyway — `ch_ssize = 0`
in absolute output, and `.REL` output is generated from the ELF
input, not from the DRI symbol table).

---

## 7. Per-phase implementation detail

### 7.1 Phase N0 — skeleton

`main.c`:

```c
#include "portab.h"
#include "bdos.h"

int main(argc, argv)
    int argc; char **argv;
{
    if (argc < 2) usage(EX_BADCLI);
    init_globals();
    parse_cli(argc, argv);
    /* dispatch is a stub returning EX_OK for now */
    return EX_OK;
}
```

`usage()` and `version_and_exit()` mirror host helpers exactly.
`init_globals()` runs because cp68 doesn't accept aggregate static
initializers (§3.4).

### 7.2 Phase N1 — DRI input → absolute

`dri_reader.c` is a near-verbatim port of `mkdri/DriReader.cpp`.
The key change is reading source-of-truth bytes from the *file*
incrementally rather than slurping into a `std::vector<U8>`:

```c
/* dri_reader.c */

#include "portab.h"
#include "file.h"
#include "image.h"

static U16 EX_MAGIC   = 0x601A;
static U16 EX_ABMAGIC = 0x601B;

#define HDR_SZ 28

int read_dri(path, opts)
    char *path; DRI_OPTS *opts;
{
    FILE_CPM *fp;
    HDR2 hdr;
    U32 i, j, bytesProcessed;
    U16 r;

    fp = cpm_fopen(path, "r");
    if (!fp) return EX_IO;

    if (read_hdr2(fp, &hdr) != SUCCESS) {
        diag_set("read error on '%s'", path);
        cpm_fclose(fp);
        return EX_IO;
    }
    if (hdr.ch_magic != EX_MAGIC) {
        diag_set("not a DRI c.out (magic = 0x%X)", hdr.ch_magic);
        cpm_fclose(fp);
        return EX_BADFORMAT;
    }
    /* ... rest mirrors DriReader.cpp::readDri() ... */
}
```

The reloc-apply loop is literally identical to `reloc.c`'s
`reloc()` function (see [reloc/reloc.c lines 282–360](https://github.com/dougdu/worm68k/blob/main/68kTools/reloc/reloc.c))
modulo the rename to `cpm_lgetw` / `cpm_lputw`.

`dri_writer.c` ports `mkdri/DriWriter.cpp` minus the Phase 4 reloc-
apply block (which arrives in Phase N4).  The header layout is
identical.

`sym_writer.c` is a 60-line port of `mkdri/SymFile.cpp`.

### 7.3 Phase N2 — COFF input

`coff_reader.c` mirrors `mkdri/CoffReader.cpp`.  The reader:

1. Reads the COFF file header (`0x150` magic, 20 bytes).
2. Reads the optional header (variable length, indicated in file
   header) and discards it — we only need the size.
3. Reads N section headers (40 bytes each), classifies them by name
   (`.text` / `.data` / `.bss`), and records each section's file
   offset, file size, virtual address.
4. Reads N symbol table entries (18 bytes each) plus the trailing
   string table.  Detects unresolved (`n_scnum == 0`).
5. Reads each section's relocation list (per-section offset is in
   the section header).  Maps the COFF reloc types per the table in
   [../MKDRI_SPEC.md §6 "Phase 2"](../MKDRI_SPEC.md) and the constants
   in [asm68K/coff/](https://github.com/dougdu/worm68k/tree/main/68kTools/asm68K/coff/).

Stream-processing: relocations are applied directly into
`g_image.text` / `g_image.data` as they are read, *not* materialized
into `g_image.relocs[]`, because we have no `-r` output path for
COFF inputs.  This saves the 4096-entry `RELOC` array's worth of
RAM (~48 KB) — the array is reused for Phase N4 only.

### 7.4 Phase N3 — ELF input (`ET_EXEC`)

`elf_reader.c` Part 1 mirrors the `ET_EXEC` path of
`mkdri/ElfReader.cpp`.  Algorithm:

1. Read the 52-byte ELF header from offset 0.  Validate magic,
   class (32), endianness (BE), machine (EM_68K = 4), type
   (ET_EXEC = 2).
2. Read the program-header table (typically 2-3 entries, 32 bytes
   each).  For each `PT_LOAD`:
   * `p_filesz` bytes from `p_offset` into either `g_image.text`
     (PF_X) or `g_image.data` (PF_W & !PF_X).
   * `p_memsz - p_filesz` bytes get added to `g_image.bssSize`.
3. Read `.symtab` and `.strtab` via section-header walk.  Detect any
   `STT_FUNC` / `STT_OBJECT` symbol with `st_shndx == SHN_UNDEF` and a
   non-empty name; collect into a comma-joined string and return
   `EX_UNRESOLVED` if any.
4. **Do not** parse `.rela.text` / `.rela.data` — `ET_EXEC` is
   already relocated.  `g_image.hasRelocs = 0`.

### 7.5 Phase N4 — `ET_DYN` + `R_68K_RELATIVE`

`elf_reader.c` Part 2 extends the same parser to accept `ET_DYN`.
Algorithm:

1. Re-classify input: if `e_type == ET_DYN`, switch from
   PT_LOAD-based extraction (Phase N3) to **section-based**
   extraction.  This is critical: `ET_DYN` packs `.dynamic`,
   `.dynsym`, `.dynstr`, `.hash`, `.rela.dyn` into the same `PT_LOAD`s
   as program code.  See [../MKDRI_SPEC.md §6 "Phase 4"](../MKDRI_SPEC.md)
   for why.
2. Walk section headers; for each `SHT_PROGBITS` with `SHF_ALLOC`,
   classify by name (`.text`/`.rodata`/`.data`/`.bss`) and stream
   into `g_image.text` / `g_image.data` / `g_image.bssSize`.
3. Walk every `SHT_RELA` section.  For each 12-byte `Elf32_Rela`:
   * Read incrementally — never materialize the full table; the
     largest fixture (`bas68k.dyn`) carries ~1500 entries × 12 B
     = ~18 KB, which would overflow our scratch arena.
   * Reject any reloc type other than `R_68K_RELATIVE` (22) or
     `R_68K_NONE` (0) with `EX_INTERNAL`.
   * Classify `r_offset` into `inSection` (text / data) by VA range.
   * Classify `r_addend` into `targetSection` (text / data / bss)
     similarly.  Skip GOT-slot fixups (offset falls outside known
     sections).
   * Append to `g_image.relocs[]`.
4. Set `g_image.hasRelocs = 1`.

`dri_writer.c` gains the per-segment-bias path (already designed for
Phase 4 host; trivial port).

### 7.6 Phase N5 — `-r` → DRI `.REL`

`dri_rel_writer.c` is a verbatim port of `mkdri/DriRelWriter.cpp`.
The implementation is small (~250 LoC) because the heavy lifting was
done in the ELF reader: by Phase N4 we already have `g_image.relocs[]`
populated with canonical `ABS32 / inSection / targetSection / addend`
tuples, and the writer's job is just to lay them out as `LUPPER`-pair
reloc-bit-stream tags and patch the corresponding 4-byte slots in
text/data with the "as-if-linked-at-0" rebased value.

---

## 8. Error reporting

All diagnostics go to **CON:** (BDOS `C_WRITESTR` function 9 — `$` is
the terminator).  Stdout/stderr are not distinguishable on CP/M-68K
v1.x, so the host convention of "stderr for errors, stdout for `-v`"
collapses into one stream.  Wording matches the host exactly so a
user can grep portable build logs:

```
MKDRI: unsupported input format (magic = 0xABCD)$
MKDRI: unresolved symbol(s): _foo, _bar, _baz$
MKDRI: unsupported relocation type 14 at .text+0x123A$
```

`diag_set(fmt, ...)` is a 60-line vsprintf-ish helper using a static
256-byte buffer and a tiny printf subset (`%s`, `%X`, `%d` only).
We do not pull in `CLIB`'s full `printf` because it adds ~6 KB and we
need only three conversion specifiers.

---

## 9. Build orchestration

### 9.1 On-target build (canonical)

`mkdri/native/makefile` (CP/M-68K nmake syntax, identical idiom to
`cpm68k/CPM68Kv1_3/DISK5/C.SUB`):

```make
# C.SUB style: cp68 → c068 → c168 → as68 → lo68 → mkdri (formerly reloc)

OBJS = main.o bdos.o portab.o dri_reader.o dri_writer.o sym_writer.o \
       coff_reader.o elf_reader.o dri_rel_writer.o

MKDRI.68K : MKDRI.REL
        MKDRI -b500 MKDRI.REL MKDRI.68K          # bootstrap: ourselves!
        # (initial build uses RELOC.68K; subsequent builds eat their own dog food)

MKDRI.REL : $(OBJS)
        LO68 -L 500 $(OBJS) -lCLIB -o MKDRI.REL

.c.o:
        CP68 -DCPM $< > $*.i
        C068 $*.i $*.1
        C168 $*.1 $*.s
        AS68 $*.s -o $@
```

Once the very first `MKDRI.68K` has been built using DRI's stock
`RELOC.68K`, all subsequent rebuilds replace `RELOC` with `MKDRI` in
the final step, closing the self-host loop.

### 9.2 Host CI build (the canonical CI path)

`mkdri/native/makefile-cross` runs the in-tree cross-toolchain from
end to end — every binary in this make is a Win64 executable we
build ourselves:

```make
WIN64BIN = $(WORM68KROOT)\68kTools\builds\win64\bin\Release
C68      = $(WIN64BIN)\c68.exe               # T3 output
AS68_43  = $(WIN64BIN)\as68_43.exe           # T1 output
LO68_43  = $(WIN64BIN)\lo68_43.exe           # T2 output
RELOC    = $(WIN64BIN)\reloc.exe             # already-shipped in-tree
MKDRI    = $(WIN64BIN)\mkdri.exe             # host C++17 mkdri
CLIB     = $(WORM68KROOT)\68kTools\c68\clib\clib.a   # T4 output

OBJS = main.o bdos.o portab.o \
       dri_reader.o dri_writer.o sym_writer.o \
       coff_reader.o elf_reader.o dri_rel_writer.o

MKDRI.68K : MKDRI.REL
        $(MKDRI) -b500 MKDRI.REL MKDRI.68K
        # (eventually: replace $(MKDRI) with the prior build's MKDRI.68K
        # running under sim68k, to validate the self-host loop)

MKDRI.REL : $(OBJS)
        $(LO68) -L 500 $(OBJS) $(CLIB) -o MKDRI.REL

.c.o :
        $(C68) -O -DCPM -c $<     # invokes cp68 → c068 → c168 → as68_43
```

This path produces a byte-identical `MKDRI.68K` to the on-target
build in §9.1 because the Win64 `c68.exe` is literally the v102a
Alcyon 4.3 code-gen ported to MSVC, emitting the same `.o` bytes for
the same input source.

### 9.3 Host syntax-check build

`mkdri/native/makefile-cross` also defines a `check` target that
runs `clang-cl /TC /Wall /WX /c` on each `.c` file with a host
`portab.h` that maps `U16` → `uint16_t` etc.  This catches
K&R-vs-ANSI breakage and stale references at every commit, without
requiring a sim68k run, **and** without requiring the Win64
`c68.exe` itself to be present — useful for a contributor who just
wants to syntax-check their `mkdri/native/*.c` edit before sending
a PR.

The `c68`-side compile is the **authoritative** check; clang-cl is
a fast-iteration second opinion only.

---

## 10. Testing

### 10.1 Phase N1 — DRI input

Fixture set (Phase N1 acceptance):

* All 60 `.REL` files from the CP/M-68K v1.2 distribution (already
  the host's Phase 3 acceptance set).
* All `.REL` files generated as a by-product of building the v1.3
  distribution (`cpm68k/CPM68Kv1_3/DISK*/*.REL`).
* The synthetic `hello.rel` from `mkdri/test/`.

Test driver: `mkdri/native/test/run_n1.sub` (a CP/M-68K SUB script)
loops the fixture set under sim68k, running `MKDRI -b500 IN.REL
OUT.68K` and `RELOC -b500 IN.REL REF.68K` side by side, then diffing.
Failure is any byte mismatch.  A host-side `tools/build/test_n1.ps1`
does the same comparison against host `mkdri.exe` for CI.

### 10.2 Phase N2+ — COFF and ELF inputs

Per-phase fixture under `mkdri/native/test/fixtures/`:

* `hello.coff` (32-byte trivial program from `mkdri/test/hello-coff.S`,
  assembled with in-tree `asm68K /coff`).
* `hello.elf` (same, but `asm68K /elf`).
* `hello.dyn` (same, but ELF `-shared -Bsymbolic` from the in-tree
  binutils toolchain at `tools/elf/m68k-elf-ld.exe`).
* `bas68k.elf` (size-stress test).

Acceptance: byte-identical output against host `mkdri.exe` for the
same fixtures.

### 10.3 CCP launch test

For each phase, the final `OUT.68K` from the simplest fixture is
loaded via sim68k's `cpm.sys` boot, verifying the CCP can `A>HELLO`
without trapping.  This catches issues that byte-identical-to-host
testing would miss (e.g. a faulty `ch_entry` field that nevertheless
matches a buggy host).

---

## 11. Lessons-learned hooks

These project-wide gotchas already burned someone; reproduced here so
the native port doesn't step on the same rake:

* **`m68k-cabi-clobbers.md`** — every BDOS thunk in `bdos_asm.s`
  defensively saves D2 (and D3-D7 / A2-A6 if it touches them).
* **`m68k-flag-after-pop.md`** — the BDOS thunks return success/fail
  in D0; the C wrapper checks the return value, not CCR.  No `bne`
  immediately after a `move` from the stack.
* **`m68k-fallthrough-trap.md`** — every assembly routine in
  `bdos_asm.s` ends with explicit `rts`.  No source-order
  fall-through.
* **`powershell-heredoc-tab-stripping.md`** — the host CI test
  scripts use file-based fixtures only, never inline heredocs into
  the assembler.

(See user memory for the full text of each.)

---

## 12. Future extensions (out of scope for the initial port)

Documented for traceability, *not* committed:

1. **`-Y` long-name mode.**  If we ever lift the 8-char DRI symbol
   limit (parent spec §9 question 3), the native port follows
   automatically — the truncation in §6.3 becomes unnecessary.
2. **PT_GNU_STACK respect.**  Same answer as host: ignore (host §9
   question 5).
3. **Multi-input mode (archive expansion).**  Not in either spec.
4. **An `-x` archive-extractor.**  Tempting (replaces `AR68.68K`'s `x`
   sub-command), but explicitly out of scope — `ar68` already exists
   natively.

---

*End of native port design.*
