// mkdri/Image.h
//
// Canonical, format-agnostic representation of a linked 68K executable.
// Every reader (ELF / COFF / DRI) populates one of these; the DRI writer
// consumes it.
//
// See MKDRI_SPEC.md §5 (co-located in this folder).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mkdri {

enum class SecId : uint8_t {
    Undef = 0,
    Text  = 1,
    Data  = 2,
    Bss   = 3,
    Abs   = 4,   // SHN_ABS-style absolute symbol
};

struct Symbol {
    std::string name;
    uint32_t    value       = 0;     // linked virtual address
    SecId       section     = SecId::Undef;
    bool        isUndefined = false;
    bool        isWeak      = false;
};

// Relocation record carried through the pipeline.
// Phase 1 currently does not emit relocations (the ELF passes through
// at its linked VA) — Phase 1.5 will fill these in when the input ELF
// was produced with `--emit-relocs`.
struct Reloc {
    enum Kind : uint8_t {
        ABS32,
        ABS16,
        PC32,
        PC16,
        ABS32_HI16,
        ABS32_LO16,
    };
    Kind     kind          = ABS32;
    SecId    inSection     = SecId::Text;  // where the patch site lives
    uint32_t offset        = 0;            // byte offset within inSection
    SecId    targetSection = SecId::Text;  // which segment is being referenced
    int32_t  addend        = 0;            // explicit addend (RELA) if any
};

struct Image {
    // Linked virtual addresses (as the input file declares them).
    uint32_t  textVA  = 0;
    uint32_t  dataVA  = 0;
    uint32_t  bssVA   = 0;

    // Raw segment bytes.  text/data are file-backed; bss is sizeBss zeros.
    std::vector<uint8_t> text;
    std::vector<uint8_t> data;
    uint32_t  bssSize = 0;

    // Program entry point as linked.
    uint32_t  entry   = 0;

    // Optional, filled when the reader saw a symbol table.
    std::vector<Symbol> symbols;

    // Optional, filled only when input retained relocation records.
    std::vector<Reloc>  relocs;

    // True when the reader retained per-site relocation information,
    // i.e. the writer is permitted to slide the segments.
    bool      hasRelocs = false;
};

} // namespace mkdri
