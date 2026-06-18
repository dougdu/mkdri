// mkdri/SymFile.cpp
//
// Phase 6: dump a plain-text symbol map.

#include "SymFile.h"

#include <cstdio>
#include <string>

namespace mkdri {

ReadResult writeSymFile(const std::string& path,
                        const Image& image,
                        const SymFileOptions& opts,
                        std::string& diag)
{
    FILE* fp = nullptr;
    if (fopen_s(&fp, path.c_str(), "w") != 0 || !fp) {
        diag = "cannot create symbol file '" + path + "'";
        return ReadResult::IoError;
    }

    std::fprintf(fp, "# mkdri symbol map\n");
    std::fprintf(fp, "# format: <hex_addr> <T|D|B|A> <name>\n");

    size_t emitted = 0;
    for (const Symbol& s : image.symbols) {
        if (s.isUndefined)            continue;
        if (s.name.empty())           continue;
        if (s.name[0] == '$')         continue;   // local labels
        char tletter = 0;
        int64_t bias = 0;
        switch (s.section) {
            case SecId::Text: tletter = 'T'; bias = opts.textBias; break;
            case SecId::Data: tletter = 'D'; bias = opts.dataBias; break;
            case SecId::Bss:  tletter = 'B'; bias = opts.bssBias;  break;
            case SecId::Abs:  tletter = 'A'; bias = 0;             break;
            default:                                  // Undef etc.
                continue;
        }
        uint32_t addr = uint32_t(int64_t(s.value) + bias);
        std::fprintf(fp, "%08X %c %s\n", addr, tletter, s.name.c_str());
        ++emitted;
    }

    if (std::fclose(fp) != 0) {
        diag = "close failed on '" + path + "'";
        return ReadResult::IoError;
    }

    if (opts.verbose) {
        std::fprintf(stderr,
            "mkdri: wrote %s: %zu symbols\n",
            path.c_str(), emitted);
    }
    return ReadResult::Ok;
}

} // namespace mkdri
