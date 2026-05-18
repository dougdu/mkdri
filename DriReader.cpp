// mkdri/DriReader.cpp
//
// Phase 3 DRI reader.  Reads a relocatable DRI c.out file
// (magic 0x601A, `ch_rlbflg == 0`), applies the requested text
// base address by walking the parallel text/data-byte and
// relocation-bit streams, and produces a canonical mkdri::Image
// whose `text`/`data`/`bssSize`/`entry` are already in their
// final, absolute layout.  The DriWriter then emits a 28-byte
// `EX_MAGIC` header with `ch_rlbflg = -1`.
//
// This is a direct port of `c:\git\worm68k\68kTools\reloc\reloc.c`
// — same trailing-zero-data compression, same 16-bit-overflow check,
// same illegal-reloc handling — so that `mkdri -b<addr> in.rel out`
// is byte-identical to `reloc -b<addr> in.rel out`.
//
// Already-stripped DRI files (`ch_rlbflg != 0`) are accepted as a
// pass-through; only the header's `ch_entry` (and the EX_ABMAGIC
// extended `ch_dstart`/`ch_bstart` if relevant) reflect the requested
// base, since there is no relocation information left to walk.

#include "DriReader.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace mkdri {

namespace {

// ---------------------- On-disk constants ----------------------

constexpr uint16_t EX_MAGIC   = 0x601A;  // contiguous
constexpr uint16_t EX_ABMAGIC = 0x601B;  // split (not supported)

constexpr size_t   HDR_SZ      = 28;

// Per reloc.c (`r & ~7` validity mask): the four low bits of the
// relocation word encode the type.  Names match `reloc.c`.
constexpr uint16_t DABS    = 0;          // data absolute (no-op)
constexpr uint16_t DRELOC  = 1;          // data-segment-relative
constexpr uint16_t TRELOC  = 2;          // text-segment-relative
constexpr uint16_t BRELOC  = 3;          // bss-segment-relative
constexpr uint16_t EXTVAL  = 4;          // external value (unresolved)
constexpr uint16_t LUPPER  = 5;          // upper word of long pair
constexpr uint16_t EXTREL  = 6;          // PC-relative external (unresolved)
constexpr uint16_t INSABS  = 7;          // 1st word of instruction (no-op)

// ---------------------- BE accessors ----------------------

inline uint16_t be16(const uint8_t* p) {
    return uint16_t((uint16_t(p[0]) << 8) | p[1]);
}
inline uint32_t be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) <<  8) |  uint32_t(p[3]);
}

bool slurpFile(const std::string& path, std::vector<uint8_t>& out,
               std::string& err)
{
    FILE* fp = nullptr;
    if (fopen_s(&fp, path.c_str(), "rb") != 0 || !fp) {
        err = "cannot open '" + path + "'";
        return false;
    }
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); err = "seek failed"; return false; }
    long sz = ftell(fp);
    if (sz < 0) { fclose(fp); err = "ftell failed"; return false; }
    rewind(fp);
    out.resize(size_t(sz));
    if (sz > 0 && fread(out.data(), 1, size_t(sz), fp) != size_t(sz)) {
        fclose(fp);
        err = "read failed";
        return false;
    }
    fclose(fp);
    return true;
}

std::string hex(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%X", unsigned(v));
    return std::string(buf);
}

} // namespace


ReadResult readDri(const std::string& path,
                   const DriReadOptions& opts,
                   Image& out,
                   std::string& diag)
{
    std::vector<uint8_t> buf;
    if (!slurpFile(path, buf, diag)) {
        return ReadResult::IoError;
    }
    if (buf.size() < HDR_SZ) {
        diag = "file too small to be a DRI c.out (" +
               std::to_string(buf.size()) + " bytes)";
        return ReadResult::UnsupportedFormat;
    }

    // ---------- Header ----------
    uint16_t magic = be16(buf.data() + 0);
    if (magic == EX_ABMAGIC) {
        diag = "EX_ABMAGIC (split-segment, 36-byte header) DRI input is "
               "not supported in Phase 3; reloc.c does not support it "
               "either.  Re-link contiguously (drop -T/-D/-B) to get "
               "0x601A.";
        return ReadResult::UnsupportedFormat;
    }
    if (magic != EX_MAGIC) {
        diag = "not a DRI c.out file (magic=" + hex(magic) + ")";
        return ReadResult::UnsupportedFormat;
    }
    uint32_t ch_tsize   = be32(buf.data() +  2);
    uint32_t ch_dsize   = be32(buf.data() +  6);
    uint32_t ch_bsize   = be32(buf.data() + 10);
    uint32_t ch_ssize   = be32(buf.data() + 14);
    // uint32_t ch_stksize = be32(buf.data() + 18);  // ignored by reloc.c
    uint32_t ch_entry   = be32(buf.data() + 22);
    uint16_t ch_rlbflg  = be16(buf.data() + 26);

    if ((ch_tsize | ch_dsize) & 1u) {
        diag = "DRI text/data sizes must be even (got tsize=" +
               hex(ch_tsize) + ", dsize=" + hex(ch_dsize) + ")";
        return ReadResult::UnsupportedFormat;
    }

    if (uint64_t(HDR_SZ) + ch_tsize + ch_dsize > buf.size()) {
        diag = "DRI text+data extends past end of file";
        return ReadResult::UnsupportedFormat;
    }
    const uint8_t* textPtr = buf.data() + HDR_SZ;
    const uint8_t* dataPtr = textPtr + ch_tsize;

    // ---------- Trailing-zero compression of the data segment ----------
    // Match reloc.c verbatim: scan 16-bit words from the start of the
    // data segment; record the position immediately after the last
    // non-zero word.  Anything beyond that is collapsed into BSS so
    // the on-disk image shrinks but the runtime layout is unchanged.
    uint32_t effectiveDataLen = 0;
    for (uint32_t i = 0; i + 1 < ch_dsize; i += 2) {
        uint16_t w = be16(dataPtr + i);
        if (w != 0) {
            effectiveDataLen = i + 2;
        }
    }
    uint32_t trimmedBytes = ch_dsize - effectiveDataLen;

    // ---------- Establish canonical image addresses ----------
    out.entry   = opts.targetBase;
    out.textVA  = opts.targetBase;
    out.dataVA  = uint32_t(uint64_t(opts.targetBase) + ch_tsize);
    out.bssVA   = uint32_t(uint64_t(out.dataVA) + effectiveDataLen);
    out.bssSize = ch_bsize + trimmedBytes;

    // ---------- Pass-through path for already-absolute inputs ----------
    if (ch_rlbflg != 0) {
        // There are no relocation bits to walk.  Copy verbatim and let
        // the writer patch only the header.  This goes beyond reloc.c
        // (which does no such check) but lines up with the spec's
        // "rebase by header-patch" behavior.
        out.text.assign(textPtr, textPtr + ch_tsize);
        out.data.assign(dataPtr, dataPtr + effectiveDataLen);
        out.hasRelocs = false;
        if (opts.verbose && opts.targetBase != ch_entry) {
            std::fprintf(stderr,
                "mkdri: note: input already absolute (ch_rlbflg=%04X); "
                "rebasing from %s to %s by header patch only — pointers "
                "inside text/data are NOT re-resolved\n",
                unsigned(ch_rlbflg),
                hex(ch_entry).c_str(),
                hex(opts.targetBase).c_str());
        }
        if (opts.verbose) {
            std::fprintf(stderr,
                "mkdri: DRI(pass-through): tsize=%s dsize=%s "
                "bsize=%s entry=%s (was %s)  trimmed_zeros=%u\n",
                hex(ch_tsize).c_str(),
                hex(effectiveDataLen).c_str(),
                hex(out.bssSize).c_str(),
                hex(out.entry).c_str(),
                hex(ch_entry).c_str(),
                unsigned(trimmedBytes));
        }
        return ReadResult::Ok;
    }

    // ---------- Locate the relocation-bit stream ----------
    // The symbol table sits immediately after the data segment; reloc
    // bits follow.  reloc.c skips the symbol table by seek (we do the
    // same with pointer arithmetic).
    uint64_t relocOff  = uint64_t(HDR_SZ) + ch_tsize + ch_dsize + ch_ssize;
    uint64_t relocSize = uint64_t(ch_tsize) + ch_dsize;  // 1 reloc word per text/data word
    if (relocOff + relocSize > buf.size()) {
        diag = "DRI relocation bits extend past end of file "
               "(expected " + std::to_string(relocSize) +
               " bytes at offset " + std::to_string(relocOff) + ")";
        return ReadResult::UnsupportedFormat;
    }
    const uint8_t* relocBase = buf.data() + size_t(relocOff);

    // ---------- Output buffers ----------
    out.text.resize(ch_tsize);
    out.data.resize(effectiveDataLen);
    out.hasRelocs = false;  // we are absolutising — output has none.

    const int64_t bias = int64_t(opts.targetBase) - int64_t(ch_entry);

    auto storeWord = [&](uint32_t bytePos, uint16_t w) -> bool {
        if (bytePos + 1 >= ch_tsize + effectiveDataLen) {
            // The second byte of the word would land outside both
            // output buffers; this only happens if a LUPPER pair
            // straddles a section boundary at exactly the trim point.
            // Refuse rather than silently truncate.
            return false;
        }
        if (bytePos < ch_tsize) {
            out.text[bytePos]     = uint8_t(w >> 8);
            out.text[bytePos + 1] = uint8_t(w & 0xFF);
        } else {
            uint32_t off = bytePos - ch_tsize;
            out.data[off]     = uint8_t(w >> 8);
            out.data[off + 1] = uint8_t(w & 0xFF);
        }
        return true;
    };

    // ---------- Main reloc loop (port of reloc.c lines 320-380) ----------
    const uint32_t totalBytes = ch_tsize + effectiveDataLen;
    uint32_t pos = 0;
    while (pos < totalBytes) {
        uint16_t k = be16(buf.data() + HDR_SZ + pos);
        uint16_t r = be16(relocBase + pos);

        // Per reloc.c: any high bits set is fatal.
        if ((r & ~uint16_t(7)) != 0) {
            diag = "illegal relocation word " + hex(r) +
                   " at offset " + hex(pos);
            return ReadResult::UnsupportedFormat;
        }

        uint32_t j;
        int longf = 0;
        if (r == LUPPER) {
            if (pos + 4 > totalBytes) {
                diag = "LUPPER at offset " + hex(pos) +
                       " has no second word";
                return ReadResult::UnsupportedFormat;
            }
            uint16_t k2 = be16(buf.data() + HDR_SZ + pos + 2);
            uint16_t r2 = be16(relocBase + pos + 2);
            j = (uint32_t(k) << 16) | uint32_t(k2);
            r = r2;
            longf = 2;
            if ((r & ~uint16_t(7)) != 0) {
                diag = "illegal relocation word " + hex(r) +
                       " at offset " + hex(pos + 2) +
                       " (lower half of LUPPER pair)";
                return ReadResult::UnsupportedFormat;
            }
        } else {
            j = k;
        }

        switch (r) {
            case TRELOC:
            case DRELOC:
            case BRELOC:
                j = uint32_t(int64_t(j) + bias);
                break;
            case DABS:
            case INSABS:
                break;
            case EXTVAL:
            case EXTREL:
                diag = "DRI input has unresolved external (" +
                       std::string(r == EXTVAL ? "EXTVAL" : "EXTREL") +
                       ") at offset " + hex(pos) +
                       "; link the input first";
                return ReadResult::UnresolvedSymbols;
            case LUPPER:
                diag = "illegal LUPPER in lower half of pair at offset " +
                       hex(pos + 2);
                return ReadResult::UnsupportedFormat;
            default:
                // Unreachable: r & ~7 already screened.
                diag = "illegal relocation type " + std::to_string(r) +
                       " at offset " + hex(pos);
                return ReadResult::UnsupportedFormat;
        }

        // 16-bit overflow check for short fixups, matches reloc.c.
        if (longf == 0 && (j & 0xFFFF0000u) != 0) {
            diag = "16-bit relocation overflow at offset " + hex(pos) +
                   " (value " + hex(j) + " does not fit in 16 bits)";
            return ReadResult::UnsupportedFeature;
        }

        if (longf) {
            if (!storeWord(pos,     uint16_t((j >> 16) & 0xFFFFu)) ||
                !storeWord(pos + 2, uint16_t(j         & 0xFFFFu))) {
                diag = "long fixup at offset " + hex(pos) +
                       " straddles the text/data trim boundary";
                return ReadResult::UnsupportedFormat;
            }
            pos += 4;
        } else {
            if (!storeWord(pos, uint16_t(j & 0xFFFFu))) {
                diag = "short fixup at offset " + hex(pos) +
                       " straddles the text/data trim boundary";
                return ReadResult::UnsupportedFormat;
            }
            pos += 2;
        }
    }

    if (opts.verbose) {
        std::fprintf(stderr,
            "mkdri: DRI: tsize=%s dsize=%s (orig %s, trimmed %u) "
            "bsize=%s entry=%s (was %s, bias=%lld)\n",
            hex(ch_tsize).c_str(),
            hex(effectiveDataLen).c_str(),
            hex(ch_dsize).c_str(),
            unsigned(trimmedBytes),
            hex(out.bssSize).c_str(),
            hex(out.entry).c_str(),
            hex(ch_entry).c_str(),
            static_cast<long long>(bias));
    }

    return ReadResult::Ok;
}

} // namespace mkdri
