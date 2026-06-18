// mkdri/DriReader.h
//
// Phase 3 DRI reader for relocatable absolute load modules
// (magic 0x601A `EX_MAGIC`).  Direct port of `reloc.c`'s relocation
// algorithm so `mkdri -b<addr> in.rel out.68K` is byte-identical
// to `reloc -b<addr> in.rel out.68K`.  See MKDRI_SPEC.md §6 "Phase 3".
#pragma once

#include "Image.h"
#include "ElfReader.h"   // for ReadResult
#include <cstdint>
#include <string>

namespace mkdri {

struct DriReadOptions {
    // Target text-segment base address applied to the relocated
    // output.  Default matches reloc.c (`-b 500` on CP/M-68K).
    uint32_t targetBase = 0x500;
    bool     verbose    = false;
};

ReadResult readDri(const std::string& path,
                   const DriReadOptions& opts,
                   Image& out,
                   std::string& diag);

} // namespace mkdri
