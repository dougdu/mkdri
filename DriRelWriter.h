// mkdri/DriRelWriter.h
//
// Phase 5 writer: takes a canonical mkdri::Image (typically produced
// by ElfReader's ET_DYN path) and emits a DRI relocatable load
// module (.REL) — the same format produced by m68k-elf's linker for
// the Alcyon toolchain.  The output is byte-compatible with
// `reloc.exe -b<addr>` for round-trip absolute conversion.
//
// See MKDRI_SPEC.md §6 (Phase 5).

#pragma once

#include "Image.h"
#include "ElfReader.h"   // for ReadResult
#include <string>

namespace mkdri {

struct DriRelWriteOptions {
    bool verbose = false;
};

// Write `image` to `outPath` as a DRI .REL relocatable load module.
// Requires image.hasRelocs == true.  The output is laid out as if
// linked at text base 0 (ch_entry = 0); a downstream invocation of
// `reloc -b<addr>` will produce the absolute file.
ReadResult writeDriRel(const std::string& outPath,
                       const Image& image,
                       const DriRelWriteOptions& opts,
                       std::string& diag);

} // namespace mkdri
