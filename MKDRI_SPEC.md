# `mkdri` — Spec for an ELF/COFF/DRI to Absolute-DRI Converter

Date: 2026-05-18
Author: GitHub Copilot (for review)
Status: **Phases 1 + 2 + 3 + 4 + 5 + 6 implemented**.
  * Phases 1–3 (ELF/COFF/DRI inputs, absolute output): byte-identical to
    `reloc.exe` across all 60 `.REL` files in the CP/M-68K v1.2
    distribution.
  * Phase 4 (`ET_DYN` + `R_68K_RELATIVE` → absolute DRI): applies
    per-segment bias from the ELF dynamic relocs.
  * Phase 5 (`-r` → DRI `.REL`): the output, when re-based by
    `reloc.exe`, is byte-identical to a direct Phase 4 conversion.
  * Phase 6 (`-y[<file>]`): optional symbol-map dump.
Location of source: repository root.
Location of this spec: [MKDRI_SPEC.md](MKDRI_SPEC.md).

For the standalone build, see [README.md](README.md). References to other
toolchain projects point to the original `worm68k` repository.

---

## 1. Purpose

CP/M-68K's CCP only loads files in the DRI/Alcyon **c.out** executable format
(magic `0x601A` family — see
[68kTools/asm68K/DRI_OBJECT_FORMAT.md](https://github.com/dougdu/worm68k/blob/main/68kTools/asm68K/DRI_OBJECT_FORMAT.md)).
Today the `worm68k` workspace assembles and links most code with the modern
toolchain that emits **ELF** (or, optionally, **COFF**). We can run those
binaries directly under `sim68k` / `sid68k`, but they cannot be launched
from inside CP/M-68K (`A>BASIC HELLO.BAS` style). The historical answer
to this problem was DRI's `reloc` tool: take a fully-linked relocatable
`c.out`, apply a chosen base address to every relocation site, and write
an absolute `.68K` image the CCP can `LOAD` directly.

This document specifies a new tool, **provisionally named `mkdri`**, that
plays the same role as `reloc` but accepts **ELF** and (in later phases)
**COFF** and pre-linked **DRI** inputs in addition. The end goal is:

```
asm68K /elf        ----> .o
ld (modern)        ----> .out (fully linked ELF)
mkdri              ----> HELLO.68K    <-- CCP-loadable
```

with the same tool also able to:

```
asm68K /coff       ----> .o
ld (modern, /coff) ----> .out (fully linked COFF)
mkdri              ----> HELLO.68K

asm68K /dri        ----> .o
lo68 / link68      ----> a.out (DRI c.out, contiguous, relocatable)
mkdri              ----> HELLO.68K          (= equivalent of running `reloc`)
```

`mkdri` is **not** a linker. It rejects any input that still contains
unresolved symbols, undefined externals, or unmerged sections. It only
applies relocation arithmetic against a fixed base and writes the result
out as a stripped, **non-relocatable** DRI `c.out` file.

---

## 2. Naming

**Chosen name: `mkdri`.** Matches the lowercase style of the existing 68k
tools (`as68`, `ar68`, `lo68`, `link68`, `reloc`, `nm68`, `size68`, `sim68k`,
`sid68k`, `sendc68`). Short, reads as "make a DRI image".

The executable is `mkdri.exe` on Windows and `mkdri` on POSIX. CMake target
name `mkdri`. The rest of this document uses `mkdri`.

---

## 3. Command-Line Interface

```
mkdri [options] <input-file>
```

| Switch | Argument | Meaning |
|---|---|---|
| `-b<hex>` | hex address, no `0x`/`$` prefix, even | Base load address for the text segment. Default: TPA base + `0x100` (= **`0x500`** on the current CP/M-68K layout, matching DRI's `reloc` default). Synonyms below for usability. |
| `-base <hex>` | hex address | Long form of `-b`. |
| `-o <path>` | output filename | Output DRI `c.out` path. Default: input filename with extension replaced by `.68K`. |
| `-f <fmt>` | one of `auto`, `elf`, `coff`, `dri` | Force the input format instead of using magic-number auto-detect. Default: `auto`. |
| `-T <hex>` | text address override | Equivalent to `-b<hex>`. (Symmetric with `lo68 -T`/`ld -Ttext`.) |
| `-D <hex>` | data address override | Force the data segment to a separate load address. Emits an `EX_ABMAGIC` (`0x601B`) header instead of `EX_MAGIC` (`0x601A`). |
| `-B <hex>` | bss  address override | Force the bss  segment to a separate load address. Implies `-D`. |
| `-s` | none | Emit `ch_stksize` = supplied value (used with `-S<hex>`). Otherwise written as `0` (let the OS default the stack). |
| `-S<hex>` | hex bytes | Stack-size value to record in the header. |
| `-r`, `--rel` | none | **Phase 5.** Emit a DRI relocatable `.REL` module (`EX_MAGIC` header with `ch_rlbflg = 0`, `ch_entry = 0`, followed by text, data, and parallel 16-bit text/data reloc-bit streams) instead of an absolute file. Requires an ELF `ET_DYN` input that retained `R_68K_RELATIVE` relocations. With `-r` the default output extension flips from `.68K` to `.REL`. |
| `-y[<symfile>]` | optional path | **Phase 6.** Also write a plain-text symbol map. Off by default. With bare `-y` the path defaults to `<output_basename>.sym`. `-y<path>` (glued) or `-y <path>` (space-separated) overrides. Format: `<hex8> <T\|D\|B\|A> <name>` per line, addresses biased to match the output layout. Loaded by sid68k's `Y` command and auto-loaded on `E` / command-line file argument. |
| `-q` | none | Quiet — suppress informational stdout. Warnings and errors still go to stderr. |
| `-v` | none | Verbose — print the per-segment summary, the symbol-count, and a count of each relocation type processed. |
| `-h`, `--help` | none | Print usage and exit `0`. |
| `--version` | none | Print version, build date, supported input formats, then exit `0`. |

Exact synopsis printed by `-h`:

```
mkdri 1.3 - ELF/COFF/DRI to DRI converter
Usage:
    mkdri [-b<HEX>] [-o <output>] [-f auto|elf|coff|dri] [-D<HEX>] [-B<HEX>]
          [-S<HEX>] [-r] [-y[<symfile>]] [-q|-v] <input>

  -b<HEX>      Base load address for the text segment (default: input's linked address)
  -o <output>  Output filename (default: <input>.68K, or .REL with -r)
  -f <format>  Force input format; default = auto-detect by magic number
  -D<HEX>      Data segment load address (forces split layout, EX_ABMAGIC)
  -B<HEX>      BSS  segment load address (forces split layout, EX_ABMAGIC)
  -S<HEX>      Initial stack-size hint to record in header (default: 0)
  -r           Emit a DRI relocatable .REL module (Phase 5) instead of an absolute file
  -y[<file>]   Also write a symbol-map file. If <file> is omitted, defaults to <output>.sym
  -q           Quiet
  -v           Verbose
  -h           This help
  --version    Print version and supported input formats

Exit codes:
   0 = success
   1 = bad command line
   2 = unsupported input format
   3 = unresolved symbols in input
   4 = unsupported relocation type or other internal error
   5 = I/O error (cannot read input, cannot write output)
```

`-r` is only valid with ELF `ET_DYN` input. COFF `F_EXEC` and DRI
`c.out`/`.REL` inputs are already absolute or pre-resolved by their
respective linkers — the relocation-site information needed to
produce a fresh DRI `.REL` simply is not present in those formats.
Giving `-r` with a COFF or DRI input is a hard error (exit 4).

### CLI parsing rules

- Switches and their arguments may be space-separated (`-o foo.68K`) or
  glued (`-ofoo.68K`, `-b500`). Spec mandates **both** be accepted for
  every option that takes a value, to match the inconsistencies in the
  existing DRI tools (`reloc` glues, GNU-style is spaced).
- The first non-switch positional argument is the **input file**.
- Exactly one input file. More than one is a CLI error (exit 1).
- All hex addresses are parsed as case-insensitive hex *with no prefix*
  (matching the historical `reloc -b39000` style). A leading `0x` or
  `$` MAY also be accepted for clarity but the doc shows the bare form.

### Defaults

- Base address: **`0x500`**, i.e. CP/M-68K TPA base (`0x400`) + `0x100`
  load offset.
- Output name: `basename(input)` with the extension stripped and `.68K`
  appended. Case is preserved on POSIX; on Windows uppercase to suit
  the CP/M-68K 8.3 convention.
- Header magic: `EX_MAGIC` (`0x601A`, contiguous). Promoted to
  `EX_ABMAGIC` (`0x601B`) only when `-D` or `-B` is given.
- For pass-through linked input, contiguous output zero-pads the preceding
  segment through the next segment's declared virtual address. Alignment gaps
  between text/data or data/BSS therefore remain part of the loaded layout.
- `ch_rlbflg` in output: **`-1`** (relocation suppressed). The output is
  not further relocatable — it is an absolute image.
- Stack size: `0` (let CP/M-68K's loader use the default), unless
  `-S<hex>` is given.

---

## 4. Input-Format Detection

The first two bytes of the input file determine the format:

| Bytes | Format | Detection result |
|---|---|---|
| `7F 45 4C 46` (`\x7FELF`, first 4 bytes) | ELF | dispatch to ELF reader |
| `01 50` `00 50` `00 52` (COFF magic `0x150` etc.) | COFF | dispatch to COFF reader (phase 2) |
| `60 1A` | DRI `EX_MAGIC` | dispatch to DRI absolutiser (phase 3) |
| `60 1B` | DRI `EX_ABMAGIC` | dispatch to DRI absolutiser, split layout |
| `60 1C`, `60 1D`, `60 1E` | DRI shared-text/I&D | Recognised, but rejected with "Unsupported magic" (the CP/M-68K CCP doesn't load these either) |
| anything else | unsupported | exit 2 with "Unsupported input format: leading bytes XX XX" |

`-f <fmt>` overrides detection and is useful for diagnostics when the
magic word is munged. If the override conflicts with the magic word
the tool prints a warning to stderr (verbose only) and proceeds.

---

## 5. Architecture

The tool is internally a three-stage pipeline:

```
   +-----------+        +----------------------+        +----------------+
   |  Reader   |  --->  |  Internal Image      |  --->  |  DRI Writer    |
   | (ELF /    |        |  (canonical          |        |  (always       |
   |  COFF /   |        |   segments, syms,    |        |   c.out 0x601A |
   |  DRI)     |        |   relocs)            |        |   or 0x601B)   |
   +-----------+        +----------------------+        +----------------+
```

### 5.1 Internal Image

A flat C++ POD (`mkdri::Image`) with these fields:

```c++
struct Section {
    std::string             name;       // ".text", ".data", ".bss"
    uint32_t                vaddr;      // virtual address as linked
    uint32_t                size;       // bytes
    std::vector<uint8_t>    data;       // empty for .bss
};

struct Reloc {
    uint32_t                sec;        // section index
    uint32_t                offset;     // byte offset within section
    enum Kind { ABS32, ABS16, PC32, PC16, ABS32_HI16, ABS32_LO16 } kind;
    int32_t                 addend;     // explicit addend (RELA) or extracted (REL)
    uint32_t                symIndex;   // 0 if not symbol-based
};

struct Symbol {
    std::string             name;
    uint32_t                value;
    uint8_t                 section;    // 0 = undef, 1 = text, 2 = data, 3 = bss
    bool                    isUndefined;
    bool                    isWeak;
};

struct Image {
    Section                 text;
    Section                 data;
    uint32_t                bssSize = 0;
    uint32_t                entry = 0;          // virtual address as linked
    std::vector<Symbol>     symbols;
    std::vector<Reloc>      relocs;             // all relocs, both text and data
};
```

### 5.2 Reader Contract

A reader populates an `Image` from a file. Common rules:

- **Fully linked input only.** If after the reader runs **any** symbol
  has `isUndefined == true`, the tool emits a comma-separated list to
  stderr ("Unresolved symbol(s): foo, bar, baz") and exits 3.
- **Single text, single data, single bss.** Multiple ELF/COFF
  text/data sections are concatenated in load order; the input's
  section virtual addresses are the source of truth for ordering.
- **Discard everything else.** Debug sections (`.debug_*`, `.stab*`,
  `.comment`, `.note`), the section-header string table, dynamic
  linking tables — all dropped.

### 5.3 DRI Writer

A single writer, identical for all phases:

1. Compute `textBase = -b` (or default `0x500`).
2. Compute `dataBase`:
   - If `-D` given, use `-D` directly. Set magic = `EX_ABMAGIC`.
   - Otherwise, `dataBase = textBase + text.size`. Magic = `EX_MAGIC`.
3. Compute `bssBase`:
   - If `-B` given, use `-B`. Set magic = `EX_ABMAGIC`.
   - Otherwise, `bssBase = dataBase + data.size`.
4. **Apply relocations** to the in-memory text and data buffers
   (see § 5.4).
5. Emit a 28- or 36-byte header per
   [DRI_OBJECT_FORMAT.md § "File Header"](https://github.com/dougdu/worm68k/blob/main/68kTools/asm68K/DRI_OBJECT_FORMAT.md):
   - `ch_magic`        = `EX_MAGIC` or `EX_ABMAGIC`
   - `ch_tsize`        = `text.size` (rounded up to even, zero-padded)
   - `ch_dsize`        = `data.size` (rounded up to even, zero-padded)
   - `ch_bsize`        = `bssSize`   (rounded up to even)
   - `ch_ssize`        = `0`         (symbols are stripped)
   - `ch_stksize`      = `0` or `-S` value
   - `ch_entry`        = `textBase`  (CP/M-68K starts execution at the first byte of text)
   - `ch_rlbflg`       = `-1`        (no relocation bits in output)
   - `ch_dstart`       = `dataBase`  (`EX_ABMAGIC` only)
   - `ch_bstart`       = `bssBase`   (`EX_ABMAGIC` only)
6. Emit `text.data` (zero-pad to even size).
7. Emit `data.data` (zero-pad to even size).
8. (Symbol table is empty by design — `-1` `ch_rlbflg` makes it
   irrelevant anyway.)
9. (No relocation bits emitted.)

The resulting file is a self-contained absolute load module the
CP/M-68K CCP can load with no further processing.

### 5.4 Applying Relocations

The reader has already translated whatever the input format used into
the canonical `Reloc::Kind` set. The writer walks `image.relocs` and
patches text/data in place. For each relocation:

| Kind | Operation |
|---|---|
| `ABS32` | Read 4 bytes big-endian at `offset`, add `(targetVA - sourceLinkedVA)`, write back. |
| `ABS16` | Read 2 bytes big-endian, range-check `int16_t`, write back. |
| `ABS32_HI16` / `ABS32_LO16` | Reassemble the 32-bit absolute from the two 16-bit halves (high word at `offset_hi`, low word at `offset_lo`), apply slide, write both back. |
| `PC32` / `PC16` | **No-op.** PC-relative references encode `target - PC` already and survive any rigid translation of both `target` and `PC`. (If the addends ever turn out to differ between linked-VA and final-VA, the reader did the wrong thing — emit an internal error.) |

Where `slide = (final_base_of_target_section) - (linked_va_of_target_section)`.

Unknown kinds are a fatal "Unsupported relocation type N at section .text offset OOOO" with exit code 4.

### 5.5 DRI `.REL` Writer (Phase 5)

A second writer, `DriRelWriter`, emits a *relocatable* DRI `.REL`
module when `-r` is given.  It is selected only after the reader has
produced a non-empty `image.relocs` vector — today that means ELF
`ET_DYN` input.

Layout, big-endian throughout:

```
+--- 28-byte hdr2 ----------------------------------------+
|  ch_magic   = EX_MAGIC  (0x601A)                        |
|  ch_tsize   = text size (rounded up to even)            |
|  ch_dsize   = data size (rounded up to even)            |
|  ch_bsize   = bss  size                                 |
|  ch_ssize   = 0                                         |
|  ch_stksize = 0  (or -S<hex>)                           |
|  ch_entry   = 0                                         |
|  ch_rlbflg  = 0  (relocation bits present)              |
+---------------------------------------------------------+
|  text bytes                                             |
|  data bytes                                             |
|  text reloc-bit stream  (one 16-bit tag per text word)  |
|  data reloc-bit stream  (one 16-bit tag per data word)  |
+---------------------------------------------------------+
```

Content is rewritten to behave as if the module was *linked at
text base = 0* with a single contiguous layout:

```
.text @ 0
.data @ tsize
.bss  @ tsize + dsize
```

For each canonical `Reloc{ABS32, inSection, offset, targetSection,
addend}`, the writer stores at the patch site:
`(addend - target_VA) + new_target_link_base[targetSection]`
where `new_target_link_base` is `0` for text, `tsize` for data,
`tsize + dsize` for bss.

Reloc tags default to `DABS` (0) for every two-byte text/data word.
Each 32-bit ABS fixup at byte offset `O` produces:

* `LUPPER` (5) at tag-stream offset `O`
* `TRELOC`/`DRELOC`/`BRELOC` (2/1/3) at `O+2`, picked from
  `targetSection`.

The net effect: `reloc -b<addr> foo.REL foo.68K` is byte-identical to
`mkdri -b<addr> foo.dyn foo.68K`, so Phase 5 output composes with the
existing DRI re-base tool without surprises.

### 5.6 Symbol-Map Writer (Phase 6)

A third, optional, writer emits a plain-text symbol map alongside
the main output when `-y` is given.  The same file is consumed by
sid68k's `Y` command and is auto-loaded when sid68k opens an
executable whose sibling `<basename>.sym` exists.

File format (one symbol per line, two-line comment header):

```
# mkdri symbol map
# format: <hex_addr> <T|D|B|A> <name>
00000500 T _start
0000050E D _data
00000512 D _data2
```

Rules:

* Address column is 8-hex zero-padded, biased to match the **output**
  layout: for absolute output it carries the same per-segment biases
  the writer applied; for `.REL` output it reflects the contiguous
  layout linked at text base 0.
* Type letter: `T` text, `D` data, `B` bss, `A` absolute.
* Filtered out: undefined symbols, empty names, and names starting
  with `$` (asm68K local labels).

The writer never fails the build on its own — a sym-file write error
is reported with exit 5 the same way any other output failure is.

---

## 6. Phasing

The tool delivers value incrementally. Each phase is a self-contained
landing.

### Phase 1 — ELF input (highest priority)

Why first: the worm68k `bas68k` build chain emits ELF today (`asm68K
/elf` + modern `ld`). This phase unblocks running compiled BASIC under
the CCP.

Scope:

- Implement the ELF reader. Required ELF features:
  - `ELFCLASS32`, big-endian (`ELFDATA2MSB`), machine type `EM_68K`
    (`= 4`). Reject other classes/endiannesses/machines (exit 2).
  - Single executable file (`ET_EXEC`). Reject `ET_REL` (caller must
    run the linker first) and `ET_DYN`.
  - **Program headers.** Walk `PT_LOAD` segments to fill `text` and
    `data`. Use the `p_vaddr`/`p_filesz`/`p_memsz` triple to detect
    `.bss` (the bytes from `p_filesz` up to `p_memsz`).
  - **Section headers.** Used only when no `PT_LOAD` segments exist
    (relocatable file — should never happen at this stage, but if it
    does, fall back to `.text`/`.data`/`.bss` by name).
  - **Symbol table.** Read `.symtab`. Any `STT_NOTYPE`/`STT_FUNC`/
    `STT_OBJECT` symbol with `st_shndx == SHN_UNDEF` and a non-empty
    name is a violation → exit 3 with the list.
  - **Relocations.** Read every `SHT_RELA` (and `SHT_REL`, just in
    case) section. Map the m68k ELF relocation types to the canonical
    enum:

    | ELF type | name | maps to |
    |---|---|---|
    | `0`  | `R_68K_NONE` | (skip) |
    | `1`  | `R_68K_32`   | `ABS32` |
    | `2`  | `R_68K_16`   | `ABS16` |
    | `3`  | `R_68K_8`    | (error: cannot relocate a single byte for slide ≠ 0; if slide == 0 it's a no-op) |
    | `4`  | `R_68K_PC32` | `PC32` |
    | `5`  | `R_68K_PC16` | `PC16` |
    | `6`  | `R_68K_PC8`  | (error: see R_68K_8) |
    | `7`  | `R_68K_GOT32`/onwards | error: GOT, PLT, TLS — not supported |

  - **Cookbook for the merged text VA.** If the linked ELF has `.text`
    at VA `0x80000000` (a typical hosted-ld default for "high enough
    not to alias"), then `slide = textBase - 0x80000000`; every
    `R_68K_32` referencing an address inside `.text` gets that slide
    added.

Deliverable: `mkdri foo.elf` produces `foo.68K` that boots from CCP.

### Phase 2 — COFF input

Why second: `asm68K` already supports COFF output. We extend `mkdri` so
that pipelines using COFF (legacy port targets, ROM builds) work too.

Scope:

- 68K COFF magic detection: `0x0150` is the m68k COFF file magic.
- Read the COFF file header, optional header, and section headers
  (`.text`, `.data`, `.bss`). Same single-text / single-data
  consolidation as ELF.
- Symbol table: standard COFF 18-byte symbol entries with the
  external string table. `n_scnum == 0` (= UNDEF) → unresolved.
- Relocations: per-section reloc lists. m68k COFF reloc types:

    | COFF | name | maps to |
    |---|---|---|
    | `0x14` | `RELOC_LONG`       | `ABS32` |
    | `0x10` | `RELOC_SHORT`      | `ABS16` |
    | `0x17` | `RELOC_RELLONG`    | `PC32` |
    | `0x13` | `RELOC_PCRSHORT`   | `PC16` |
    | `0x15` | `RELOC_RELBYTE`    | (error if slide ≠ 0) |
    | others | TBD                | error |

  (The exact constants live in
  [68kTools/asm68K/coff/](https://github.com/dougdu/worm68k/tree/main/68kTools/asm68K/coff/) — extract them from the
  existing emitter rather than guessing.)

Deliverable: `mkdri foo.coff` produces `foo.68K`.

### Phase 3 — DRI input (replacement for `reloc`)

Why last: we already have `reloc.exe` for this case, so it's the
lowest-priority delta. Doing it inside `mkdri` makes the tool a
drop-in replacement and lets future build pipelines pick *any* of the
three formats without changing the build script.

Scope:

- Detect magic `0x601A` (or `0x601B`).
- Read header per
  [DRI_OBJECT_FORMAT.md § "File Header"](https://github.com/dougdu/worm68k/blob/main/68kTools/asm68K/DRI_OBJECT_FORMAT.md).
- Reject `ch_rlbflg != 0` (an already-stripped DRI file has no relocs,
  so there is nothing to do — print a polite "input is already
  absolute, nothing to relocate" and exit 0 with a no-op? **Decision:
  copy input to output unchanged with `ch_entry` and `ch_dstart` /
  `ch_bstart` patched to the requested bases**, so the user can still
  re-base an already-absolute image. Flag with `-v` warning.).
- Read symbol table; reject any entry with `SYXR` (external reference)
  set and `SYDF` clear → unresolved external (exit 3).
- Read text relocation bits + data relocation bits. Convert each per
  [DRI_OBJECT_FORMAT.md § "Relocation Types"](https://github.com/dougdu/worm68k/blob/main/68kTools/asm68K/DRI_OBJECT_FORMAT.md):

    | DRI type | name | maps to |
    |---|---|---|
    | `0` | `DABS`   | (no-op) |
    | `1` | `DRELOC` | `ABS32_LO16` (paired with previous `LUPPER`) or `ABS16` (data-relative low word) |
    | `2` | `TRELOC` | same, text-relative |
    | `3` | `BRELOC` | same, bss-relative |
    | `4` | `EXTVAR` | external — must have been resolved by linker; if it appears here, "internal error: unresolved EXTVAR in input" exit 4 |
    | `5` | `LUPPER` | pair-start; consumed when handling the next entry |
    | `6` | `EXTREL` | same as EXTVAR, but PC-relative |
    | `7` | `INSABS` | (no-op — disassembler hint) |

  This **is** the algorithm `reloc.c` already implements (see
  `c:\git\worm68k\68kTools\reloc\reloc.c` lines 340–360); reuse / port
  the relocate loop verbatim.

Deliverable: `mkdri foo.rel` produces `foo.68K`; output is
byte-identical to `reloc -b<addr> foo.rel foo.68K`.

### Phase 4 — ET_DYN → absolute DRI (`R_68K_RELATIVE`)

Why: modern `m68k-elf-ld` builds carry their relocations in the
relocatable-shared-object (`ET_DYN`) flavor with `R_68K_RELATIVE`
entries.  This is the canonical ELF analog of DRI's `.REL` format and
lets the user pick a final load base without re-linking.

Scope:

- Accept `e_type == ET_DYN` (in addition to `ET_EXEC`).  The reader
  switches to **section-based** extraction (not `PT_LOAD`-based),
  because ET_DYN packs dynamic-loader metadata (`.dynamic`, `.got`,
  `.dynsym`, `.dynstr`, `.hash`, `.rela.dyn`) into the same `PT_LOAD`s
  as program code and data.  Only `.text` / `.rodata` / `.data` /
  `.bss` are pulled into the canonical image.
- Walk every `SHT_RELA` section.  Accept only `R_68K_RELATIVE`
  (`type = 22`) and `R_68K_NONE`.  Reject anything else with exit 4.
  Per the SysV m68k ABI, an `R_68K_RELATIVE` entry says
  *"the 32-bit slot at `r_offset` should hold `load_base + r_addend`
  at run time"* — the addend already encodes the link-time target VA.
- Each accepted entry is converted to a canonical
  `Reloc{kind=ABS32, inSection, offset, targetSection, addend}`.
  `inSection`/`targetSection` are derived by VA range against the
  reader's extracted `.text`/`.data`/`.bss` bounds; sites outside
  those ranges (e.g., GOT slots) are silently skipped.
- The writer computes three per-segment biases
  (`textBias`, `dataBias`, `bssBias`) and patches each fixup site
  with `addend + bias[targetSection]`.  Header is emitted with
  `ch_rlbflg = -1` (no relocation info), just like Phase 1.

Deliverable: `mkdri -b<addr> foo.dyn` produces a CCP-loadable
`foo.68K`.  The toolchain command to produce the input is:

```
m68k-elf-ld -shared -Bsymbolic --no-undefined -e _start foo.o -o foo.dyn
```

### Phase 5 — ET_DYN → DRI `.REL` (`-r`)

Scope:

- New CLI flag `-r` / `--rel`.
- Output layout (per `DRI_OBJECT_FORMAT.md` and `reloc.c` semantics):

  ```
  +--- 28-byte hdr2 (EX_MAGIC, ch_rlbflg=0, ch_entry=0) ---+
  |  text  (ch_tsize, even-padded)                         |
  |  data  (ch_dsize, even-padded)                         |
  |  text reloc-bit stream (one 16-bit tag per text word)  |
  |  data reloc-bit stream                                 |
  +---------------------------------------------------------+
  ```
- Content is rewritten so the file behaves as if **linked at text
  base = 0** with a single contiguous layout
  (`text @ 0`, `data @ tsize`, `bss @ tsize+dsize`).  For each
  canonical `Reloc`, the patch site receives
  `(r.addend - target_VA) + new_target_link_base`.
- Reloc tags default to `DABS` (0).  Each 32-bit ABS fixup at
  byte offset `O` gets `LUPPER` (5) at tag-stream offset `O` and
  `TRELOC`/`DRELOC`/`BRELOC` (2/1/3) at `O+2`, classified by
  `targetSection`.
- Because `ch_entry = 0`, a downstream
  `reloc -b<addr> foo.REL foo.68K` is byte-identical to
  `mkdri -b<addr> foo.dyn foo.68K`.

Deliverable: `mkdri -r foo.dyn` produces `foo.REL` (default extension
becomes `.REL` when `-r` is set).

### Phase 6 — `-y[<symfile>]` symbol-map dump (opt-in)

Useful for sid68k symbolic debugging.  **Off by default.**  When
`-y` is given:

- Path defaults to `<output_basename>.sym`.
- `-y<path>` (glued) or `-y <path>` (space-separated) overrides it.
- File is line-oriented plain text:

  ```
  # mkdri symbol map
  # format: <hex_addr> <T|D|B|A> <name>
  00000500 T _start
  0000050E D _data
  00000512 D _data2
  ```

  Type letter: `T` text, `D` data, `B` bss, `A` absolute.  Addresses
  reflect the **output** layout: for absolute output they are biased
  by the same per-segment biases the writer applied; for `.REL`
  output they reflect the contiguous-at-0 link layout.
- Undefined symbols, empty names, and names starting with `$`
  (asm68K local labels) are filtered out.

The same format is loaded by `sid68k`'s `Y <file>` command, and
automatically when an `E` command (or command-line file argument) is
used and a sibling `<basename>.sym` exists.

---

## 7. Error Conditions and Diagnostics

All diagnostics go to **stderr**. stdout is reserved for `-v` verbose
output so the tool composes with shell redirection.

| Condition | Message | Exit |
|---|---|---|
| No input file given | `mkdri: missing input file (use -h for help)` | 1 |
| Two or more inputs | `mkdri: only one input file is supported` | 1 |
| Unknown switch | `mkdri: unknown option '-X'` | 1 |
| Cannot open input | `mkdri: cannot open '%s': %s` (perror text) | 5 |
| Cannot create output | `mkdri: cannot create '%s': %s` | 5 |
| Unknown magic | `mkdri: unsupported input format (magic = 0xXXXX)` | 2 |
| ELFCLASS not 32-bit | `mkdri: only 32-bit ELF is supported` | 2 |
| Endianness not BE | `mkdri: only big-endian ELF is supported` | 2 |
| Machine not m68k | `mkdri: input ELF is for machine %d, expected EM_68K (4)` | 2 |
| ELF/COFF/DRI not fully linked | `mkdri: unresolved symbol(s): foo, bar, baz` | 3 |
| Unsupported reloc | `mkdri: unsupported relocation type %d at .text+0x%X` | 4 |
| Base address odd | `mkdri: base address 0x%X must be even` | 1 |
| Base address would overlap reserved CP/M page zero | `mkdri: base address 0x%X is below the first byte of the CP/M-68K TPA (0x500); 0x000\u20130x3FF are 68000 exception vectors and 0x400\u20130x4FF is the CP/M-68K zero page` (warn only with `-v`, default: silent \u2014 let the user override) | 0 |
| Image too large for 64-KB CP/M-68K TPA | `mkdri: image text+data+bss = 0x%X exceeds typical TPA; CCP load will fail` (warning) | 0 |
| `-r` with COFF or DRI input | `mkdri: -r requires an ELF ET_DYN input that retained relocations (...). COFF F_EXEC and DRI c.out inputs are already absolute / pre-resolved and cannot be converted to a DRI .REL by this tool.` | 4 |

The unresolved-symbol message MUST list **every** unresolved symbol,
not just the first one — historically `lo68` lists just the first and
this has caused hours of grief.

---

## 8. Implementation Notes

### 8.1 Language and toolchain

- **C++17**, MSVC + clang-cl + g++ portable. Match the style of
  [68kTools/asm68K/](https://github.com/dougdu/worm68k/tree/main/68kTools/asm68K/) — that project is also C++17.
- Use the existing **`alclib`** big-endian I/O helpers (`lgetw`,
  `lgetl`, `lputw`, `lputl` — see
  [alclib/libget.c](alclib/libget.c) /
  [alclib/libput.c](alclib/libput.c)) for DRI I/O.
- Use the existing **`DRIFilehdr.h`**, **`DRISymbol.h`**, etc. from
  [68kTools/asm68K/](https://github.com/dougdu/worm68k/tree/main/68kTools/asm68K/) for header structures; do NOT
  redefine them locally.
- No third-party ELF/COFF library — write the parsers in ~300 lines
  each, against the LSB / `coff.h` documentation that already lives
  in [68kTools/asm68K/elf/](https://github.com/dougdu/worm68k/tree/main/68kTools/asm68K/elf/) and
  [68kTools/asm68K/coff/](https://github.com/dougdu/worm68k/tree/main/68kTools/asm68K/coff/). The emitters in those
  folders are direct documentation of the on-disk layout.
- CMake target named `mkdri`, built by the standalone
  [CMakeLists.txt](CMakeLists.txt) with the bundled `alclib` dependency.

### 8.2 Project structure

```
c:\git\mkdri\
    CMakeLists.txt
    alclib\                        bundled DRI I/O library
    Include\                       shared alclib headers
    main.cpp                       CLI parsing, format dispatch
    Image.h                        canonical internal representation
    ElfReader.cpp / .h             phase 1
    CoffReader.cpp / .h            phase 2
    DriReader.cpp / .h             phase 3
    DriWriter.cpp / .h             always present
    test\
        test_elf.cpp               link an ELF fixture, mkdri it, verify
        test_coff.cpp              same for COFF
        test_dri.cpp               compare with reloc.exe output
        fixtures\
            hello.s                 trivial program, sources only
            ...
```

### 8.3 Testing

- **Bit-identical phase 3 test.** For every DRI fixture, compare
  `mkdri -b500 in.rel out.68K` with `reloc -b500 in.rel out2.68K`. The
  files must match byte-for-byte. Set this as the acceptance gate for
  phase 3.
- **CCP launch test.** For phase 1: build a tiny program
  (`MOVEQ #0,D1 ; MOVE.W #2,D0 ; TRAP #2 ; MOVEQ #0,D0 ; TRAP #2`) that
  prints a `\0` and exits. Run it through `mkdri` and load it under
  `sim68k` booting `cpm.sys`; CCP `A>HELLO` must produce the expected
  output and return to the prompt.
- **Negative test.** Feed `mkdri` an ELF `.o` (relocatable, with
  unresolved symbols) and confirm exit code 3 plus the symbol list.

### 8.4 Style hooks for the BAS68K pipeline

The eventual makefile target in
[68kTools/bas68k/samples/](https://github.com/dougdu/worm68k/tree/main/68kTools/bas68k/samples/) becomes:

```makefile
%.68K: %.elf
    $(MKDRI) -b500 -o $@ $<

# or, when phase 3 + DRI tooling is in place:
%.68K: %.rel
    $(MKDRI) -b500 -o $@ $<
```

No special build-system support is required beyond a `$(MKDRI)` variable
pointing at the installed binary.

---

## 9. Open Questions

1. **Should `mkdri` also accept *relocatable* DRI / ELF / COFF and
   invoke a linker internally?** No — design rejects unresolved
   symbols. Composability: keep `mkdri` a converter and let
   `ld`/`lo68`/`link68` do their job. Rationale: a one-tool-does-one-thing
   philosophy matches DRI's tradition (`as68`, `lo68`, `reloc` are
   three separate binaries for three separate steps).
2. **Do we need an `-r` switch to *keep* relocation bits in the output
   so that the result is still relocatable?** Not in scope for the
   stated need (the CCP wants absolute images). If a downstream tool
   ever needs relocatable DRI output, add it then.
3. **Compatibility with the future "shortened symbols" runtime
   work.** `mkdri` strips symbols entirely, so DRI's 8-char limit
   doesn't directly affect this tool — but if `-y <symfile>` is added
   in phase 4, the dumped names will be whatever the input had.
   Document, don't truncate.
4. **Big-endian / little-endian writer.** DRI's c.out is always
   big-endian. Spec mandates the writer always writes BE. No host
   dependency.
5. **Stack size derivation.** Some ELF inputs encode a `PT_GNU_STACK`
   segment with `p_memsz` indicating a stack-size hint. Phase 1 will
   **ignore** that and rely on `-S<hex>` or the OS default. Reason: we
   can't trust GNU's heuristics here, and the user knows the TPA
   shape better than the linker does.

---

## 10. Acceptance Criteria

- Phase 1 lands when:
  - `mkdri hello.elf` produces a 28-byte-header `.68K` file with
    `ch_rlbflg = -1`.
  - The output runs to completion from inside CP/M-68K's CCP under
    sim68k.
  - `mkdri --version` prints the supported formats list.
  - Unresolved-symbol input is rejected with exit 3 and a full list.
- Phase 2 lands when the same suite passes for COFF input.
- Phase 3 lands when `mkdri -b500 X.rel out.68K` is byte-identical to
  `reloc -b500 X.rel out.68K` for the `cpm.sys` build (the largest
  DRI executable currently in the workspace).

---

*End of spec.*
