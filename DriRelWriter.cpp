// mkdri/DriRelWriter.cpp
//
// Phase 5: write a DRI relocatable load module (.REL).
//
// Output layout (per DRI_OBJECT_FORMAT.md §3 and reloc.c semantics):
//
//   +-------------------------+
//   | 28-byte hdr2            |  EX_MAGIC (0x601A), ch_rlbflg = 0,
//   |                         |  ch_entry = 0 (link-time text base)
//   +-------------------------+
//   | text  (tsize, even-pad) |
//   +-------------------------+
//   | data  (dsize, even-pad) |
//   +-------------------------+
//   | text reloc-bit stream   |  tsize/2 BE 16-bit words
//   +-------------------------+
//   | data reloc-bit stream   |  dsize/2 BE 16-bit words
//   +-------------------------+
//
// Each word of text/data has one corresponding 16-bit reloc tag:
//
//      DABS   = 0   no fixup, treat as 16-bit data
//      DRELOC = 1   add-bias, target = .data
//      TRELOC = 2   add-bias, target = .text
//      BRELOC = 3   add-bias, target = .bss
//      LUPPER = 5   upper word of long pair; next word holds the
//                   T/D/BRELOC classification and the low half of
//                   the 32-bit value.
//
// We normalize all stored pointer values to the "link-time-at-0"
// contiguous layout:
//
//      new_text_link_base = 0
//      new_data_link_base = textSize
//      new_bss_link_base  = textSize + dataSize
//
// so that `reloc -b<addr> in.REL out.68K` is byte-identical to
// `mkdri -b<addr> in.dyn out.68K` (Phase 4).

#include "DriRelWriter.h"

extern "C" {
#include <alclib.h>            // putchd, struct hdr2, EX_MAGIC
}

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace mkdri {

namespace {

constexpr uint16_t DABS    = 0;
constexpr uint16_t DRELOC  = 1;
constexpr uint16_t TRELOC  = 2;
constexpr uint16_t BRELOC  = 3;
constexpr uint16_t LUPPER  = 5;

inline uint32_t roundUpEven(uint32_t v) {
    return (v + 1u) & ~1u;
}

inline std::string hex(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%X", v);
    return buf;
}

inline void storeBE32(std::vector<uint8_t>& buf, uint32_t off, uint32_t v) {
    buf[off + 0] = uint8_t(v >> 24);
    buf[off + 1] = uint8_t(v >> 16);
    buf[off + 2] = uint8_t(v >>  8);
    buf[off + 3] = uint8_t(v      );
}

inline void storeBE16(std::vector<uint8_t>& buf, uint32_t off, uint16_t v) {
    buf[off + 0] = uint8_t(v >> 8);
    buf[off + 1] = uint8_t(v     );
}

} // namespace

ReadResult writeDriRel(const std::string& outPath,
                       const Image& image,
                       const DriRelWriteOptions& opts,
                       std::string& diag)
{
    if (!image.hasRelocs) {
        diag = "cannot emit relocatable DRI from a pass-through image "
               "(input was not built with -shared -Bsymbolic)";
        return ReadResult::UnsupportedFeature;
    }

    const uint32_t textSize = roundUpEven(uint32_t(image.text.size()));
    const uint32_t dataSize = roundUpEven(uint32_t(image.data.size()));
    const uint32_t bssSize  = roundUpEven(image.bssSize);

    // ---- Pad text / data to even and prepare reloc-bit streams ------

    std::vector<uint8_t> textBytes = image.text;
    if (textBytes.size() & 1u) textBytes.push_back(0);
    textBytes.resize(textSize, 0);

    std::vector<uint8_t> dataBytes = image.data;
    if (dataBytes.size() & 1u) dataBytes.push_back(0);
    dataBytes.resize(dataSize, 0);

    // One 16-bit reloc tag per 16-bit word.  Default DABS (0).
    std::vector<uint8_t> textRel(textSize, 0);  // initialized to DABS
    std::vector<uint8_t> dataRel(dataSize, 0);

    // ---- Compute "as-if-linked-at-0" target bases ------------------
    //
    //  In the .REL file, content reads as if text starts at 0, data
    //  starts at textSize, bss starts at textSize+dataSize.  Then
    //  reloc.exe adds a single uniform bias (= new_base - ch_entry)
    //  to every fixup, which yields the correct absolute layout.

    const int64_t new_text_base = 0;
    const int64_t new_data_base = int64_t(textSize);
    const int64_t new_bss_base  = int64_t(textSize) + int64_t(dataSize);

    // ---- Patch each fixup site and emit reloc tags ------------------

    for (const Reloc& r : image.relocs) {
        if (r.kind != Reloc::ABS32) {
            diag = "Phase 5 supports only ABS32 relocations; got kind " +
                   std::to_string(int(r.kind));
            return ReadResult::UnsupportedFeature;
        }
        if (r.offset & 1u) {
            diag = "Phase 5: odd relocation offset 0x" + hex(r.offset) +
                   " — not 16-bit aligned";
            return ReadResult::UnsupportedFeature;
        }

        std::vector<uint8_t>* siteBuf = nullptr;
        std::vector<uint8_t>* siteRel = nullptr;
        if (r.inSection == SecId::Text) {
            siteBuf = &textBytes;  siteRel = &textRel;
        } else if (r.inSection == SecId::Data) {
            siteBuf = &dataBytes;  siteRel = &dataRel;
        } else {
            diag = "Phase 5: relocation site not in .text/.data";
            return ReadResult::UnsupportedFeature;
        }

        if (size_t(r.offset) + 4 > siteBuf->size()) {
            diag = "Phase 5: relocation offset 0x" + hex(r.offset) +
                   " extends past section end";
            return ReadResult::UnsupportedFeature;
        }

        // Stored value (as-if-linked-at-0 layout).
        int64_t targetBase = 0;
        int64_t targetVA   = 0;
        uint16_t tag       = 0;
        switch (r.targetSection) {
            case SecId::Text:
                targetBase = new_text_base; targetVA = image.textVA;
                tag = TRELOC; break;
            case SecId::Data:
                targetBase = new_data_base; targetVA = image.dataVA;
                tag = DRELOC; break;
            case SecId::Bss:
                targetBase = new_bss_base;  targetVA = image.bssVA;
                tag = BRELOC; break;
            case SecId::Abs:
                // Absolute literal — no fixup, store verbatim.
                storeBE32(*siteBuf, r.offset, uint32_t(r.addend));
                continue;
            default:
                diag = "Phase 5: bad relocation target classification";
                return ReadResult::UnsupportedFeature;
        }
        int64_t stored = int64_t(uint32_t(r.addend)) - targetVA + targetBase;
        storeBE32(*siteBuf, r.offset, uint32_t(stored));

        // Tag the two 16-bit slots: upper = LUPPER, lower = T/D/BRELOC.
        storeBE16(*siteRel, r.offset,     LUPPER);
        storeBE16(*siteRel, r.offset + 2, tag);
    }

    // ---- Open output -----------------------------------------------

    FILE* ofp = nullptr;
    if (fopen_s(&ofp, outPath.c_str(), "wb") != 0 || !ofp) {
        diag = "cannot create '" + outPath + "'";
        return ReadResult::IoError;
    }

    // ---- Header ----------------------------------------------------

    hdr2 hdr;
    std::memset(&hdr, 0, sizeof(hdr));
    hdr.ch_magic   = uint16_t(EX_MAGIC);
    hdr.ch_tsize   = textSize;
    hdr.ch_dsize   = dataSize;
    hdr.ch_bsize   = bssSize;
    hdr.ch_ssize   = 0;
    hdr.ch_stksize = 0;
    hdr.ch_entry   = 0;                  // link-time text base
    hdr.ch_rlbflg  = 0;                  // relocation present

    if (putchd(ofp, &hdr) == ERROR) {
        std::fclose(ofp);
        diag = "write error on '" + outPath + "' (header)";
        return ReadResult::IoError;
    }

    // ---- Text / Data bytes -----------------------------------------

    if (!textBytes.empty() &&
        std::fwrite(textBytes.data(), 1, textBytes.size(), ofp) !=
            textBytes.size()) {
        std::fclose(ofp);
        diag = "write error on '" + outPath + "' (text)";
        return ReadResult::IoError;
    }
    if (!dataBytes.empty() &&
        std::fwrite(dataBytes.data(), 1, dataBytes.size(), ofp) !=
            dataBytes.size()) {
        std::fclose(ofp);
        diag = "write error on '" + outPath + "' (data)";
        return ReadResult::IoError;
    }

    // ---- Reloc-bit streams -----------------------------------------

    if (!textRel.empty() &&
        std::fwrite(textRel.data(), 1, textRel.size(), ofp) !=
            textRel.size()) {
        std::fclose(ofp);
        diag = "write error on '" + outPath + "' (text reloc bits)";
        return ReadResult::IoError;
    }
    if (!dataRel.empty() &&
        std::fwrite(dataRel.data(), 1, dataRel.size(), ofp) !=
            dataRel.size()) {
        std::fclose(ofp);
        diag = "write error on '" + outPath + "' (data reloc bits)";
        return ReadResult::IoError;
    }

    if (std::fclose(ofp) != 0) {
        diag = "close failed on '" + outPath + "'";
        return ReadResult::IoError;
    }

    if (opts.verbose) {
        std::fprintf(stderr,
            "mkdri: wrote %s: DRI .REL, text 0x%X, data 0x%X, bss 0x%X, "
            "ch_entry=0, ch_rlbflg=0, fixups=%zu\n",
            outPath.c_str(),
            textSize, dataSize, bssSize,
            image.relocs.size());
    }

    return ReadResult::Ok;
}

} // namespace mkdri
