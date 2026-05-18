// mkdri/ElfReader.h
//
// Reads a 32-bit big-endian m68k ELF executable into a canonical Image.
//
// Phase 1 scope (see MKDRI_SPEC.md §6 "Phase 1"):
//   - Input must be ET_EXEC, ELFCLASS32, ELFDATA2MSB, EM_68K.
//   - Walks PT_LOAD program headers to populate text/data/bss.
//   - Walks the .symtab to detect unresolved externals.
//   - Does NOT walk relocation sections yet; Image::hasRelocs stays false
//     and the writer will pass the segments through at their linked VA.

#pragma once

#include "Image.h"
#include <string>

namespace mkdri {

struct ElfReadOptions {
    bool verbose = false;
};

// Result codes match the spec's exit-code table (§7).
enum class ReadResult : int {
    Ok                  = 0,
    UnsupportedFormat   = 2,
    UnresolvedSymbols   = 3,
    UnsupportedFeature  = 4,
    IoError             = 5,
};

ReadResult readElf(const std::string& path,
                   const ElfReadOptions& opts,
                   Image& out,
                   std::string& diag);

} // namespace mkdri
