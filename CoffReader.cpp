// mkdri/CoffReader.cpp
//
// Phase 2 COFF reader.  Reads a fully linked, big-endian, 68K COFF
// executable (m68k magic 0x0150/0x0151/0x0152) into a canonical Image.
// See MKDRI_SPEC.md §6 "Phase 2".

#include "CoffReader.h"

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>

namespace mkdri {

namespace {

// ---------------------- COFF on-disk constants ----------------------
// (Local copies; we parse field-by-field for endianness safety rather
// than mapping packed structs over the file buffer.)

constexpr uint16_t MC68KWRMAGIC = 0x0150;  // writable text segments
constexpr uint16_t MC68KROMAGIC = 0x0151;  // read-only shareable text
constexpr uint16_t MC68KPGMAGIC = 0x0152;  // demand-paged text

constexpr uint16_t F_RELFLG = 0x0001;      // no relocation info
constexpr uint16_t F_EXEC   = 0x0002;      // file is executable
// F_LNNO 0x0004, F_LSYMS 0x0008 — informational only

constexpr uint16_t AOUT_MAGIC_EXEC = 0x10B; // F_NORMAL_EXECUTABLE
constexpr uint16_t AOUT_MAGIC_ROM  = 0x107; // F_ROM_IMAGE

// Section flags we care about.
constexpr uint32_t STYP_TEXT = 0x00000020;
constexpr uint32_t STYP_DATA = 0x00000040;
constexpr uint32_t STYP_BSS  = 0x00000080;

// Section special numbers in symbol entries.
constexpr int16_t  N_UNDEF = 0;
constexpr int16_t  N_ABS   = -1;
constexpr int16_t  N_DEBUG = -2;

// Storage classes we recognise as "real" runtime symbols.
constexpr uint8_t  C_EXT     = 2;
constexpr uint8_t  C_STATIC  = 3;
constexpr uint8_t  C_LABEL   = 6;
constexpr uint8_t  C_WEAKEXT = 105;
constexpr uint8_t  C_FILE    = 103;
constexpr uint8_t  C_SECTION = 104;

// Sizes on disk.
constexpr size_t SZ_FILHDR = 20;
constexpr size_t SZ_AOUTHD = 28;
constexpr size_t SZ_SCNHDR = 40;
constexpr size_t SZ_SYMENT = 18;
constexpr size_t SZ_RELOC  = 10;

// FILHDR field offsets.
constexpr size_t OFF_F_MAGIC   =  0;
constexpr size_t OFF_F_NSCNS   =  2;
constexpr size_t OFF_F_TIMDAT  =  4;
constexpr size_t OFF_F_SYMPTR  =  8;
constexpr size_t OFF_F_NSYMS   = 12;
constexpr size_t OFF_F_OPTHDR  = 16;
constexpr size_t OFF_F_FLAGS   = 18;

// AOUTHDR field offsets.
constexpr size_t OFF_A_MAGIC      =  0;
constexpr size_t OFF_A_VSTAMP     =  2;
constexpr size_t OFF_A_TSIZE      =  4;
constexpr size_t OFF_A_DSIZE      =  8;
constexpr size_t OFF_A_BSIZE      = 12;
constexpr size_t OFF_A_ENTRY      = 16;
constexpr size_t OFF_A_TEXT_START = 20;
constexpr size_t OFF_A_DATA_START = 24;

// SCNHDR field offsets.
constexpr size_t OFF_S_NAME    =  0;     // 8 bytes
constexpr size_t OFF_S_PADDR   =  8;
constexpr size_t OFF_S_VADDR   = 12;
constexpr size_t OFF_S_SIZE    = 16;
constexpr size_t OFF_S_SCNPTR  = 20;
constexpr size_t OFF_S_RELPTR  = 24;
constexpr size_t OFF_S_LNNOPTR = 28;
constexpr size_t OFF_S_NRELOC  = 32;
constexpr size_t OFF_S_NLNNO   = 34;
constexpr size_t OFF_S_FLAGS   = 36;

// SYMENT field offsets.
constexpr size_t OFF_N_NAME    =  0;     // 8 bytes (or zeroes+offset)
constexpr size_t OFF_N_VALUE   =  8;
constexpr size_t OFF_N_SCNUM   = 12;
constexpr size_t OFF_N_TYPE    = 14;
constexpr size_t OFF_N_SCLASS  = 16;
constexpr size_t OFF_N_NUMAUX  = 17;


// ---------------------- BE accessors ----------------------

inline uint16_t be16(const uint8_t* p) {
    return uint16_t((uint16_t(p[0]) << 8) | p[1]);
}
inline uint32_t be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) <<  8) |  uint32_t(p[3]);
}


// ---------------------- Helpers ----------------------

bool slurpFile(const std::string& path, std::vector<uint8_t>& out,
               std::string& err) {
    FILE* fp = nullptr;
    if (fopen_s(&fp, path.c_str(), "rb") != 0 || !fp) {
        err = "cannot open '" + path + "'";
        return false;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        err = "seek failed on '" + path + "'";
        return false;
    }
    long sz = ftell(fp);
    if (sz < 0) {
        fclose(fp);
        err = "ftell failed on '" + path + "'";
        return false;
    }
    rewind(fp);
    out.resize(size_t(sz));
    if (sz > 0 && fread(out.data(), 1, size_t(sz), fp) != size_t(sz)) {
        fclose(fp);
        err = "read failed on '" + path + "'";
        return false;
    }
    fclose(fp);
    return true;
}

// COFF section name: first 8 bytes of the SCNHDR are either an
// inline ASCII name (NUL-padded, no terminator if exactly 8 bytes)
// or — when the first byte is '/' — a decimal offset into the
// string table.  We accept both forms.
std::string sectionNameFromHeader(const uint8_t* shName,
                                  const std::vector<uint8_t>& buf,
                                  uint32_t strtabOff,
                                  uint32_t strtabSize)
{
    if (shName[0] == '/') {
        // Decimal offset into string table.
        std::string num;
        for (int i = 1; i < 8 && shName[i] != 0; ++i) {
            num.push_back(char(shName[i]));
        }
        uint32_t off = uint32_t(std::atoi(num.c_str()));
        if (strtabOff != 0 && off < strtabSize) {
            const char* p = reinterpret_cast<const char*>(
                buf.data() + strtabOff + off);
            return std::string(p);
        }
        return "";
    }
    // Inline name; up to 8 bytes, may not be NUL-terminated.
    char tmp[9];
    std::memcpy(tmp, shName, 8);
    tmp[8] = 0;
    return std::string(tmp);
}

// Classify a COFF section by name as a hint (used when STYP flags
// are zero or ambiguous).
SecId classifyByName(const std::string& name) {
    if (name == ".text" || name.rfind(".text.", 0) == 0 ||
        name == ".rodata") {
        return SecId::Text;
    }
    if (name == ".data" || name.rfind(".data.", 0) == 0 ||
        name == ".rdata") {
        return SecId::Data;
    }
    if (name == ".bss" || name.rfind(".bss.", 0) == 0) {
        return SecId::Bss;
    }
    return SecId::Undef;
}

SecId classifyByFlagsThenName(uint32_t flags, const std::string& name) {
    if (flags & STYP_TEXT) return SecId::Text;
    if (flags & STYP_DATA) return SecId::Data;
    if (flags & STYP_BSS)  return SecId::Bss;
    return classifyByName(name);
}

// Symbol name: either inline in n_name[8] or — when the first 4
// bytes are zero — at (strtabOff + n_offset) in the string table.
std::string symbolNameFromEntry(const uint8_t* sym,
                                const std::vector<uint8_t>& buf,
                                uint32_t strtabOff,
                                uint32_t strtabSize)
{
    uint32_t zeroes = be32(sym + OFF_N_NAME);
    if (zeroes == 0) {
        uint32_t off = be32(sym + OFF_N_NAME + 4);
        if (strtabOff != 0 && off < strtabSize) {
            const char* p = reinterpret_cast<const char*>(
                buf.data() + strtabOff + off);
            return std::string(p);
        }
        return "";
    }
    char tmp[9];
    std::memcpy(tmp, sym + OFF_N_NAME, 8);
    tmp[8] = 0;
    return std::string(tmp);
}

} // namespace


ReadResult readCoff(const std::string& path,
                    const CoffReadOptions& opts,
                    Image& out,
                    std::string& diag)
{
    std::vector<uint8_t> buf;
    if (!slurpFile(path, buf, diag)) {
        return ReadResult::IoError;
    }

    // ---------- File header ----------
    if (buf.size() < SZ_FILHDR) {
        diag = "file too small to be a COFF (" +
               std::to_string(buf.size()) + " bytes)";
        return ReadResult::UnsupportedFormat;
    }
    const uint8_t* fh = buf.data();
    uint16_t f_magic  = be16(fh + OFF_F_MAGIC);
    uint16_t f_nscns  = be16(fh + OFF_F_NSCNS);
    uint32_t f_symptr = be32(fh + OFF_F_SYMPTR);
    uint32_t f_nsyms  = be32(fh + OFF_F_NSYMS);
    uint16_t f_opthdr = be16(fh + OFF_F_OPTHDR);
    uint16_t f_flags  = be16(fh + OFF_F_FLAGS);

    if (f_magic != MC68KWRMAGIC && f_magic != MC68KROMAGIC &&
        f_magic != MC68KPGMAGIC) {
        diag = "not a 68K COFF file (f_magic=0x" +
               std::to_string(f_magic) + ")";
        return ReadResult::UnsupportedFormat;
    }

    // Phase 2 scope: linked executable only.  Reject relocatable
    // object files (no optional header, F_EXEC not set).
    if (f_opthdr == 0) {
        diag = "input is a COFF object file (no optional header); "
               "link first or feed mkdri the linked executable";
        return ReadResult::UnsupportedFormat;
    }
    if (f_opthdr != SZ_AOUTHD) {
        diag = "unexpected optional header size " +
               std::to_string(f_opthdr) + " (expected " +
               std::to_string(SZ_AOUTHD) + ")";
        return ReadResult::UnsupportedFormat;
    }
    if (!(f_flags & F_EXEC)) {
        diag = "COFF input is not marked F_EXEC (f_flags=0x" +
               std::to_string(f_flags) +
               "); link first or feed mkdri the linked executable";
        return ReadResult::UnsupportedFormat;
    }

    // ---------- Optional header ----------
    if (buf.size() < SZ_FILHDR + size_t(f_opthdr)) {
        diag = "optional header extends past end of file";
        return ReadResult::UnsupportedFormat;
    }
    const uint8_t* ah = buf.data() + SZ_FILHDR;
    uint16_t a_magic     = be16(ah + OFF_A_MAGIC);
    uint32_t a_entry     = be32(ah + OFF_A_ENTRY);
    uint32_t a_text_start = be32(ah + OFF_A_TEXT_START);
    uint32_t a_data_start = be32(ah + OFF_A_DATA_START);

    if (a_magic != AOUT_MAGIC_EXEC && a_magic != AOUT_MAGIC_ROM) {
        // Some toolchains stamp other a.out magics; don't reject the
        // file but flag a warning so the user knows.
        if (opts.verbose) {
            std::fprintf(stderr,
                "mkdri: warning: unusual a.out magic 0x%X in optional "
                "header (proceeding anyway)\n",
                unsigned(a_magic));
        }
    }

    out.entry = a_entry;

    // ---------- Section headers ----------
    size_t scnTableOff = SZ_FILHDR + size_t(f_opthdr);
    if (scnTableOff + size_t(f_nscns) * SZ_SCNHDR > buf.size()) {
        diag = "section header table extends past end of file";
        return ReadResult::UnsupportedFormat;
    }

    // String table sits immediately after the symbol table.  Its
    // first 4 bytes are the total table size including those 4 bytes
    // themselves.  Compute and bounds-check up front so section-name
    // resolution can use it.
    uint32_t strtabOff  = 0;
    uint32_t strtabSize = 0;
    if (f_nsyms != 0 && f_symptr != 0) {
        uint64_t symBytes = uint64_t(f_nsyms) * SZ_SYMENT;
        if (uint64_t(f_symptr) + symBytes + 4 <= buf.size()) {
            strtabOff = uint32_t(f_symptr + symBytes);
            strtabSize = be32(buf.data() + strtabOff);
            if (strtabSize < 4) strtabSize = 4;
            if (uint64_t(strtabOff) + strtabSize > buf.size()) {
                // String table runs off the end — clip to what we have.
                strtabSize = uint32_t(buf.size() - strtabOff);
            }
        }
    }

    struct SecInfo {
        std::string name;
        SecId       cls;
        uint32_t    vaddr;
        uint32_t    size;
        uint32_t    scnptr;
        uint32_t    relptr;
        uint16_t    nreloc;
        uint32_t    flags;
    };
    std::vector<SecInfo> sections;
    sections.reserve(f_nscns);

    bool anyRelocs = false;
    for (uint16_t i = 0; i < f_nscns; ++i) {
        const uint8_t* sh = buf.data() + scnTableOff + size_t(i) * SZ_SCNHDR;
        SecInfo s;
        s.name   = sectionNameFromHeader(sh + OFF_S_NAME, buf,
                                         strtabOff, strtabSize);
        s.vaddr  = be32(sh + OFF_S_VADDR);
        s.size   = be32(sh + OFF_S_SIZE);
        s.scnptr = be32(sh + OFF_S_SCNPTR);
        s.relptr = be32(sh + OFF_S_RELPTR);
        s.nreloc = be16(sh + OFF_S_NRELOC);
        s.flags  = be32(sh + OFF_S_FLAGS);
        s.cls    = classifyByFlagsThenName(s.flags, s.name);
        if (s.nreloc > 0) anyRelocs = true;
        sections.push_back(std::move(s));
    }

    if (anyRelocs) {
        // The linker has already applied these to the section bytes;
        // GNU coff-ld preserves the records in F_EXEC files as
        // post-link informational data (objdump consumes them).
        // Phase 2 is pass-through: trust the linker's resolved
        // bytes, mirror the policy used by the Phase 1 ELF reader
        // for ET_EXEC, and surface the fact only when verbose.
        if (opts.verbose) {
            std::fprintf(stderr,
                "mkdri: note: COFF input retains post-link "
                "relocation records (informational; bytes were "
                "already resolved by the linker)\n");
        }
    }
    out.hasRelocs = anyRelocs;

    // ---------- Concatenate text / data / bss in vaddr order ----------
    auto extractGroup = [&](SecId cls, std::vector<uint8_t>& dataOut,
                            uint32_t& vaOut, uint32_t& sizeOut)
        -> ReadResult
    {
        std::vector<const SecInfo*> grp;
        for (const auto& s : sections) {
            if (s.cls == cls && s.size > 0) grp.push_back(&s);
        }
        if (grp.empty()) return ReadResult::Ok;
        std::sort(grp.begin(), grp.end(),
                  [](const SecInfo* a, const SecInfo* b) {
                      return a->vaddr < b->vaddr;
                  });
        vaOut = grp.front()->vaddr;
        uint32_t cursor = vaOut;
        for (const SecInfo* s : grp) {
            if (s->vaddr < cursor) {
                diag = "overlapping sections in COFF input";
                return ReadResult::UnsupportedFormat;
            }
            // Insert any vaddr-gap as zero padding.
            uint32_t gap = s->vaddr - cursor;
            if (gap != 0) {
                if (cls == SecId::Bss) {
                    // Bss has no on-disk bytes — just grow the size.
                } else {
                    dataOut.insert(dataOut.end(), gap, uint8_t(0));
                }
            }
            if (cls != SecId::Bss) {
                if (uint64_t(s->scnptr) + s->size > buf.size()) {
                    diag = "section '" + s->name +
                           "' raw data extends past end of file";
                    return ReadResult::UnsupportedFormat;
                }
                dataOut.insert(dataOut.end(),
                               buf.begin() + s->scnptr,
                               buf.begin() + s->scnptr + s->size);
            }
            cursor = s->vaddr + s->size;
        }
        sizeOut = cursor - vaOut;
        return ReadResult::Ok;
    };

    uint32_t textSize = 0;
    uint32_t dataSize = 0;
    if (auto r = extractGroup(SecId::Text, out.text, out.textVA, textSize);
        r != ReadResult::Ok) {
        return r;
    }
    if (auto r = extractGroup(SecId::Data, out.data, out.dataVA, dataSize);
        r != ReadResult::Ok) {
        return r;
    }
    // BSS sections — only the cumulative size and starting VA matter.
    {
        std::vector<uint8_t> dummy;
        uint32_t bssSize = 0;
        if (auto r = extractGroup(SecId::Bss, dummy, out.bssVA, bssSize);
            r != ReadResult::Ok) {
            return r;
        }
        out.bssSize = bssSize;
        (void)dummy;
    }

    if (out.text.empty()) {
        diag = "no .text section with non-zero size in COFF input";
        return ReadResult::UnsupportedFormat;
    }

    // If the optional header advertised text_start/data_start,
    // sanity-check that they match what the section headers say.
    // Some linkers leave a_text_start = 0 even on linked exec; only
    // complain when both are non-zero and inconsistent.
    if (a_text_start != 0 && out.textVA != 0 &&
        a_text_start != out.textVA && opts.verbose) {
        std::fprintf(stderr,
            "mkdri: warning: COFF aouthdr text_start=0x%X disagrees "
            "with .text vaddr=0x%X; trusting .text\n",
            unsigned(a_text_start), unsigned(out.textVA));
    }
    if (a_data_start != 0 && out.dataVA != 0 &&
        a_data_start != out.dataVA && opts.verbose) {
        std::fprintf(stderr,
            "mkdri: warning: COFF aouthdr data_start=0x%X disagrees "
            "with .data vaddr=0x%X; trusting .data\n",
            unsigned(a_data_start), unsigned(out.dataVA));
    }

    // ---------- Symbol table ----------
    if (f_nsyms != 0 && f_symptr != 0) {
        uint64_t symBytes = uint64_t(f_nsyms) * SZ_SYMENT;
        if (uint64_t(f_symptr) + symBytes > buf.size()) {
            diag = "symbol table extends past end of file";
            return ReadResult::UnsupportedFormat;
        }

        // Build a section-class lookup keyed by 1-based scnum.
        // (n_scnum == 1 → first section header, == 2 → second, etc.)
        std::vector<SecId> scnumToCls(size_t(f_nscns) + 1, SecId::Undef);
        for (size_t i = 0; i < sections.size(); ++i) {
            scnumToCls[i + 1] = sections[i].cls;
        }

        std::vector<std::string> unresolved;
        uint32_t i = 0;
        while (i < f_nsyms) {
            const uint8_t* sym = buf.data() + f_symptr + i * SZ_SYMENT;
            int16_t  n_scnum  = int16_t(be16(sym + OFF_N_SCNUM));
            uint8_t  n_sclass = sym[OFF_N_SCLASS];
            uint8_t  n_numaux = sym[OFF_N_NUMAUX];
            uint32_t n_value  = be32(sym + OFF_N_VALUE);

            // Aux entries follow the primary entry; skip them en bloc.
            uint32_t advance = 1u + n_numaux;

            // Skip filenames, section definitions, and debug.
            if (n_sclass == C_FILE || n_sclass == C_SECTION ||
                n_scnum  == N_DEBUG) {
                i += advance;
                continue;
            }

            std::string nm = symbolNameFromEntry(sym, buf,
                                                 strtabOff, strtabSize);

            // GNU coff-ld emits the section markers `.text`, `.data`,
            // `.bss` (and friends like `.stab`, `.line`) as
            // C_STATIC entries with one aux record holding scnlen/
            // nreloc/nlnno.  They're not runtime symbols.
            if (n_sclass == C_STATIC && n_numaux >= 1 &&
                !nm.empty() && nm.front() == '.') {
                i += advance;
                continue;
            }

            // asm68K-style COFF objects bake `@`-prefixed assembler
            // predefines into the symbol table (`@Cpu`, `@CodeSize`,
            // `@Version`, ...).  They're metadata, not real symbols.
            if (!nm.empty() && nm.front() == '@') {
                i += advance;
                continue;
            }

            Symbol s;
            s.name        = nm;
            s.value       = n_value;
            s.isWeak      = (n_sclass == C_WEAKEXT);
            s.isUndefined = false;
            s.section     = SecId::Undef;

            if (n_scnum == N_UNDEF) {
                s.isUndefined = true;
                if (!nm.empty() && !s.isWeak &&
                    (n_sclass == C_EXT)) {
                    unresolved.push_back(nm);
                }
            } else if (n_scnum == N_ABS) {
                s.section = SecId::Abs;
            } else if (n_scnum > 0 && size_t(n_scnum) <= sections.size()) {
                s.section = scnumToCls[size_t(n_scnum)];
            }

            if (!nm.empty()) out.symbols.push_back(std::move(s));
            i += advance;
        }

        // Same policy as the ELF reader: undef entries in a linked
        // executable are historical metadata — the linker already
        // accepted the image as complete.  Phase 2 doesn't apply
        // relocations, so undef symbols cannot fail at runtime.
        // Surface them only in verbose mode.
        if (!unresolved.empty() && opts.verbose) {
            std::ostringstream oss;
            oss << "mkdri: note: " << unresolved.size()
                << " undefined symbol(s) in COFF symtab (benign for "
                   "F_EXEC without relocations): ";
            for (size_t k = 0; k < unresolved.size() && k < 8; ++k) {
                if (k) oss << ", ";
                oss << unresolved[k];
            }
            if (unresolved.size() > 8) oss << ", ...";
            std::fprintf(stderr, "%s\n", oss.str().c_str());
        }
    }

    if (opts.verbose) {
        std::fprintf(stderr,
            "mkdri: COFF: magic=0x%04X entry=0x%08X "
            "text=[VA 0x%08X, %u bytes] "
            "data=[VA 0x%08X, %u bytes] "
            "bss=[VA 0x%08X, %u bytes] "
            "symbols=%zu hasRelocs=%d\n",
            unsigned(f_magic), out.entry,
            out.textVA, unsigned(out.text.size()),
            out.dataVA, unsigned(out.data.size()),
            out.bssVA,  unsigned(out.bssSize),
            out.symbols.size(),
            int(out.hasRelocs));
    }

    return ReadResult::Ok;
}

} // namespace mkdri
