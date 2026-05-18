// mkdri/SymFile.h
//
// Phase 6: write a plain-text symbol map file alongside the DRI
// output.  Format is line-oriented, with one symbol per line:
//
//      <hex8> <T|D|B|A> <name>
//
// Lines starting with '#' are comments.  Address is the symbol's
// final (post-rebase) virtual address.  Type letter:
//
//      T = .text symbol
//      D = .data symbol
//      B = .bss  symbol
//      A = absolute (SHN_ABS-style)
//
// sid68k's `Y` command and its auto-load path on `E` consume this
// format.  See MKDRI_SPEC.md §6 (Phase 6).

#pragma once

#include "Image.h"
#include "ElfReader.h"  // for ReadResult
#include <string>

namespace mkdri {

struct SymFileOptions {
    // Per-segment bias to apply to each symbol's value before emit.
    // For Phase 4 these are non-zero (slide from link-time to load VA);
    // for Phase 5 and pass-through they are zero.
    int64_t textBias = 0;
    int64_t dataBias = 0;
    int64_t bssBias  = 0;
    bool    verbose  = false;
};

// Write a symbol map for `image` to `path`.  Returns Ok on success;
// IoError on write failure (diag populated).
ReadResult writeSymFile(const std::string& path,
                        const Image& image,
                        const SymFileOptions& opts,
                        std::string& diag);

} // namespace mkdri
