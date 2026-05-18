// mkdri/CoffReader.h
//
// Phase 2 COFF reader for fully linked, big-endian m68k COFF
// executables (magic 0x0150 / 0x0151 / 0x0152).  Populates a
// canonical mkdri::Image; the writer pass is shared with the
// ELF reader.  See MKDRI_SPEC.md §6 "Phase 2".
#pragma once

#include "Image.h"
#include "ElfReader.h"   // for ReadResult
#include <string>

namespace mkdri {

struct CoffReadOptions {
    bool verbose = false;
};

ReadResult readCoff(const std::string& path,
                    const CoffReadOptions& opts,
                    Image& out,
                    std::string& diag);

} // namespace mkdri
