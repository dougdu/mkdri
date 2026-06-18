// mkdri/DriWriter.h
//
// Writes a canonical Image out as an absolute (non-relocatable) DRI
// c.out executable file.  See MKDRI_SPEC.md §5.3.

#pragma once

#include "Image.h"
#include "ElfReader.h"   // for ReadResult
#include <string>
#include <optional>

namespace mkdri {

struct DriWriteOptions {
    // Override for the text-segment load base.  When unset, the writer
    // honors image.entry (= the linker's choice) for the contiguous
    // (EX_MAGIC) layout, or each segment's linked VA otherwise.
    std::optional<uint32_t> textBase;
    std::optional<uint32_t> dataBase;
    std::optional<uint32_t> bssBase;
    uint32_t                stackSize = 0;
    bool                    verbose   = false;
};

// Write `image` to `outPath` as a DRI c.out absolute load module.
// Returns ReadResult::Ok on success; ReadResult::IoError or
// ReadResult::UnsupportedFeature otherwise.  On failure, diag is set.
ReadResult writeDri(const std::string& outPath,
                    const Image& image,
                    const DriWriteOptions& opts,
                    std::string& diag);

} // namespace mkdri
