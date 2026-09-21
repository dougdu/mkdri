// mkdri/DriWriter.cpp
//
// Writes an absolute (non-relocatable) DRI c.out load module.
//
// Output layout, per MKDRI_SPEC.md §5.3 and DRI_OBJECT_FORMAT.md:
//
//   +-----------+
//   | header    | 28 bytes (EX_MAGIC) or 36 bytes (EX_ABMAGIC)
//   +-----------+
//   | text      | rounded up to even
//   +-----------+
//   | data      | rounded up to even
//   +-----------+
//
//   ch_rlbflg = -1  (relocation suppressed; symbol table absent)

#include "DriWriter.h"

extern "C" {
#include <alclib.h>            // _lputw, _lputl, putchd, struct hdr2
                               // (also pulls cout.h with EX_MAGIC etc.)
}

#include <cstdio>
#include <cstring>
#include <string>

namespace mkdri {

namespace {

inline uint32_t roundUpEven(uint32_t v) {
    return (v + 1u) & ~1u;
}

inline std::string hex(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%X", v);
    return buf;
}

} // namespace

ReadResult writeDri(const std::string& outPath,
                    const Image& image,
                    const DriWriteOptions& opts,
                    std::string& diag)
{
    // --- Determine bases ----------------------------------------------

    // Default text base:
    //   * pass-through (no relocs): linker's entry address
    //   * relocatable input  (Phase 4): default to 0x500 (CP/M TPA) unless
    //     the user picked a base via `-b`.
    uint32_t textBase = image.hasRelocs ? 0x500u : image.entry;

    if (opts.textBase) {
        if (!image.hasRelocs && *opts.textBase != image.entry) {
            diag = "input was linked at " + hex(image.entry) +
                   " but -b was given as " + hex(*opts.textBase) +
                   ".  Re-link with -Ttext " + hex(*opts.textBase) +
                   " or with -shared -Bsymbolic to allow slide.";
            return ReadResult::UnsupportedFeature;
        }
        textBase = *opts.textBase;
    }

    // Text base must be even (68k word alignment).
    if (textBase & 1u) {
        diag = "text base address " + hex(textBase) + " is odd; must be even";
        return ReadResult::UnsupportedFeature;
    }

    std::vector<uint8_t> textBytes = image.text;
    std::vector<uint8_t> dataBytes = image.data;

    // Decide on contiguous vs split layout.
    const bool splitLayout = opts.dataBase.has_value() || opts.bssBase.has_value();

    // A contiguous DRI image has no address fields for ordinary 0x601A
    // text/data/bss segments. Preserve pass-through ET_EXEC layout gaps by
    // materializing them at the end of the preceding file-backed segment.
    if (!splitLayout && !image.hasRelocs) {
        uint32_t nextVA = textBase + roundUpEven(uint32_t(textBytes.size()));
        if (image.dataVA != 0 && !dataBytes.empty()) {
            if (image.dataVA < nextVA) {
                diag = "ELF data VA " + hex(image.dataVA) +
                       " overlaps text ending at " + hex(nextVA);
                return ReadResult::UnsupportedFeature;
            }
            textBytes.resize(textBytes.size() + (image.dataVA - nextVA), 0);
        }
        nextVA = textBase + roundUpEven(uint32_t(textBytes.size())) +
                 roundUpEven(uint32_t(dataBytes.size()));
        if (image.bssVA != 0 && image.bssSize != 0) {
            if (image.bssVA < nextVA) {
                diag = "ELF bss VA " + hex(image.bssVA) +
                       " overlaps data ending at " + hex(nextVA);
                return ReadResult::UnsupportedFeature;
            }
            dataBytes.resize(dataBytes.size() + (image.bssVA - nextVA), 0);
        }
    }

    uint32_t textSize = roundUpEven(uint32_t(textBytes.size()));
    uint32_t dataSize = roundUpEven(uint32_t(dataBytes.size()));
    uint32_t bssSize  = roundUpEven(image.bssSize);

    uint32_t dataBase = 0;
    uint32_t bssBase  = 0;
    if (splitLayout) {
        dataBase = opts.dataBase.value_or(textBase + textSize);
        bssBase  = opts.bssBase .value_or(dataBase + dataSize);
        if ((dataBase & 1u) || (bssBase & 1u)) {
            diag = "data/bss base addresses must be even";
            return ReadResult::UnsupportedFeature;
        }
        // Pass-through inputs cannot be slid without relocs.
        if (!image.hasRelocs) {
            if (image.dataVA != 0 && dataBase != image.dataVA) {
                diag = "cannot slide .data from VA " +
                       hex(image.dataVA) + " to " + hex(dataBase) +
                       " without relocation info";
                return ReadResult::UnsupportedFeature;
            }
            if (image.bssVA != 0 && bssSize != 0 && bssBase != image.bssVA) {
                diag = "cannot slide .bss from VA " +
                       hex(image.bssVA) + " to " + hex(bssBase) +
                       " without relocation info";
                return ReadResult::UnsupportedFeature;
            }
        }
    } else {
        dataBase = textBase + textSize;
        bssBase  = dataBase + dataSize;
    }

    // --- Phase 4: apply per-segment bias to relocatable input --------
    if (image.hasRelocs && !image.relocs.empty()) {
        const int64_t textBias = int64_t(textBase) - int64_t(image.textVA);
        const int64_t dataBias = int64_t(dataBase) - int64_t(image.dataVA);
        const int64_t bssBias  = int64_t(bssBase)  - int64_t(image.bssVA);

        auto storeBE32 = [](std::vector<uint8_t>& buf, uint32_t off,
                            uint32_t v) {
            buf[off + 0] = uint8_t(v >> 24);
            buf[off + 1] = uint8_t(v >> 16);
            buf[off + 2] = uint8_t(v >>  8);
            buf[off + 3] = uint8_t(v      );
        };

        for (const Reloc& r : image.relocs) {
            std::vector<uint8_t>* siteBuf = nullptr;
            if      (r.inSection == SecId::Text) siteBuf = &textBytes;
            else if (r.inSection == SecId::Data) siteBuf = &dataBytes;
            else {
                diag = "relocation site is not in .text/.data "
                       "(internal error)";
                return ReadResult::UnsupportedFeature;
            }

            if (r.kind != Reloc::ABS32) {
                diag = "Phase 4 supports only ABS32-style relocations; "
                       "got kind " + std::to_string(int(r.kind));
                return ReadResult::UnsupportedFeature;
            }
            if (size_t(r.offset) + 4 > siteBuf->size()) {
                diag = "relocation offset 0x" +
                       hex(r.offset) + " is outside its section";
                return ReadResult::UnsupportedFeature;
            }

            int64_t bias = 0;
            switch (r.targetSection) {
                case SecId::Text: bias = textBias; break;
                case SecId::Data: bias = dataBias; break;
                case SecId::Bss:  bias = bssBias;  break;
                case SecId::Abs:  bias = 0;        break;
                default:
                    diag = "relocation target is not text/data/bss/abs";
                    return ReadResult::UnsupportedFeature;
            }
            uint32_t patched = uint32_t(int64_t(uint32_t(r.addend)) + bias);
            storeBE32(*siteBuf, r.offset, patched);
        }
    }

    // --- Open output --------------------------------------------------

    FILE* ofp = nullptr;
    if (fopen_s(&ofp, outPath.c_str(), "wb") != 0 || !ofp) {
        diag = "cannot create '" + outPath + "'";
        return ReadResult::IoError;
    }

    // --- Header -------------------------------------------------------

    hdr2 hdr;
    std::memset(&hdr, 0, sizeof(hdr));
    hdr.ch_magic   = uint16_t(splitLayout ? EX_ABMAGIC : EX_MAGIC);
    hdr.ch_tsize   = textSize;
    hdr.ch_dsize   = dataSize;
    hdr.ch_bsize   = bssSize;
    hdr.ch_ssize   = 0;                  // symbols stripped
    hdr.ch_stksize = opts.stackSize;
    // For pass-through, the entry already equals image.entry == textBase.
    // For relocatable input, the entry's link-time VA must be rebased
    // by the text-segment bias.
    if (image.hasRelocs) {
        int64_t textBias = int64_t(textBase) - int64_t(image.textVA);
        hdr.ch_entry = uint32_t(int64_t(image.entry) + textBias);
    } else {
        hdr.ch_entry = textBase;
    }
    hdr.ch_rlbflg  = uint16_t(0xFFFFu);  // -1 → relocation suppressed
    hdr.ch_dstart  = dataBase;
    hdr.ch_bstart  = bssBase;

    if (putchd(ofp, &hdr) == ERROR) {
        std::fclose(ofp);
        diag = "write error on '" + outPath + "' (header)";
        return ReadResult::IoError;
    }

    // --- Text + zero-pad to even -------------------------------------

    if (!textBytes.empty()) {
        if (std::fwrite(textBytes.data(), 1, textBytes.size(), ofp) !=
            textBytes.size()) {
            std::fclose(ofp);
            diag = "write error on '" + outPath + "' (text)";
            return ReadResult::IoError;
        }
    }
    if (textBytes.size() & 1u) {
        uint8_t pad = 0;
        std::fwrite(&pad, 1, 1, ofp);
    }

    // --- Data + zero-pad ---------------------------------------------

    if (!dataBytes.empty()) {
        if (std::fwrite(dataBytes.data(), 1, dataBytes.size(), ofp) !=
            dataBytes.size()) {
            std::fclose(ofp);
            diag = "write error on '" + outPath + "' (data)";
            return ReadResult::IoError;
        }
    }
    if (dataBytes.size() & 1u) {
        uint8_t pad = 0;
        std::fwrite(&pad, 1, 1, ofp);
    }

    if (std::fclose(ofp) != 0) {
        diag = "close failed on '" + outPath + "'";
        return ReadResult::IoError;
    }

    if (opts.verbose) {
        std::fprintf(stderr,
            "mkdri: wrote %s: %s magic, text 0x%X@0x%X, data 0x%X@0x%X, "
            "bss 0x%X@0x%X, entry=0x%X, ch_rlbflg=-1, relocs_applied=%zu\n",
            outPath.c_str(),
            splitLayout ? "EX_ABMAGIC (0x601B)" : "EX_MAGIC (0x601A)",
            textSize, textBase,
            dataSize, dataBase,
            bssSize,  bssBase,
            unsigned(hdr.ch_entry),
            image.hasRelocs ? image.relocs.size() : size_t(0));
    }

    return ReadResult::Ok;
}

} // namespace mkdri
