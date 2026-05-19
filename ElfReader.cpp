// mkdri/ElfReader.cpp
//
// ELF reader.  Reads a fully linked, big-endian, 32-bit, m68k ELF
// (`ET_EXEC` or `ET_DYN`) into a canonical Image.
//
//   * `ET_EXEC` path  (MKDRI_SPEC.md §6 "Phase 1"):
//       PT_LOAD-based extraction.  Bytes pass through at the linked
//       VA; relocations are ignored.
//
//   * `ET_DYN` path   (MKDRI_SPEC.md §6 "Phase 4 / Phase 5"):
//       Section-based extraction.  Reads `.text`, `.rodata`, `.data`,
//       `.bss` (the *program's* sections — not the dynamic-loader
//       infrastructure that lives inside the same PT_LOADs) and
//       collects the `R_68K_RELATIVE` entries from `.rela.dyn`.
//       Sets `Image::hasRelocs = true` so the writer can rebase.

#include "ElfReader.h"

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>
#include <string>
#include <sstream>

namespace mkdri {

namespace {

// ---------------------- ELF on-disk constants ----------------------
// (Local copies; we do not pull asm68K/ELFBase.h's packed structs in
// because we parse field-by-field for endianness safety.)

constexpr uint8_t  ELFMAG0      = 0x7F;
constexpr uint8_t  ELFMAG1      = 'E';
constexpr uint8_t  ELFMAG2      = 'L';
constexpr uint8_t  ELFMAG3      = 'F';

constexpr uint8_t  ELFCLASS32   = 1;
constexpr uint8_t  ELFDATA2MSB  = 2;

constexpr uint16_t ET_EXEC      = 2;
constexpr uint16_t ET_DYN       = 3;
constexpr uint16_t EM_68K       = 4;

constexpr uint16_t SHN_UNDEF    = 0;
constexpr uint16_t SHN_ABS      = 0xFFF1;
constexpr uint16_t SHN_COMMON   = 0xFFF2;

constexpr uint32_t SHT_NULL     = 0;
constexpr uint32_t SHT_PROGBITS = 1;
constexpr uint32_t SHT_SYMTAB   = 2;
constexpr uint32_t SHT_STRTAB   = 3;
constexpr uint32_t SHT_RELA     = 4;
constexpr uint32_t SHT_NOBITS   = 8;
constexpr uint32_t SHT_REL      = 9;

constexpr uint32_t SHF_ALLOC    = 0x2;
constexpr uint32_t SHF_EXECINSTR= 0x4;
constexpr uint32_t SHF_WRITE    = 0x1;

constexpr uint32_t PT_LOAD      = 1;

constexpr uint32_t PF_X         = 1;
constexpr uint32_t PF_W         = 2;
constexpr uint32_t PF_R         = 4;

constexpr uint8_t  STB_LOCAL    = 0;
constexpr uint8_t  STB_GLOBAL   = 1;
constexpr uint8_t  STB_WEAK     = 2;

constexpr uint8_t  STT_NOTYPE   = 0;
constexpr uint8_t  STT_OBJECT   = 1;
constexpr uint8_t  STT_FUNC     = 2;
constexpr uint8_t  STT_SECTION  = 3;
constexpr uint8_t  STT_FILE     = 4;

// m68k SysV ABI relocation types (subset used by Phase 4/5).
constexpr uint8_t  R_68K_NONE     =  0;
constexpr uint8_t  R_68K_32       =  1;
constexpr uint8_t  R_68K_16       =  2;
constexpr uint8_t  R_68K_8        =  3;
constexpr uint8_t  R_68K_PC32     =  4;
constexpr uint8_t  R_68K_PC16     =  5;
constexpr uint8_t  R_68K_PC8      =  6;
constexpr uint8_t  R_68K_RELATIVE = 22;

// ELF header field offsets (after e_ident[16]).
constexpr size_t EI_NIDENT = 16;
// e_type@16, e_machine@18, e_version@20, e_entry@24, e_phoff@28,
// e_shoff@32, e_flags@36, e_ehsize@40, e_phentsize@42, e_phnum@44,
// e_shentsize@46, e_shnum@48, e_shstrndx@50.  Total Ehdr32 = 52 bytes.
constexpr size_t OFF_E_TYPE      = 16;
constexpr size_t OFF_E_MACHINE   = 18;
constexpr size_t OFF_E_ENTRY     = 24;
constexpr size_t OFF_E_PHOFF     = 28;
constexpr size_t OFF_E_SHOFF     = 32;
constexpr size_t OFF_E_PHENTSIZE = 42;
constexpr size_t OFF_E_PHNUM     = 44;
constexpr size_t OFF_E_SHENTSIZE = 46;
constexpr size_t OFF_E_SHNUM     = 48;
constexpr size_t OFF_E_SHSTRNDX  = 50;
constexpr size_t SZ_EHDR32       = 52;

// Section header (40 bytes).
constexpr size_t OFF_SH_NAME      =  0;
constexpr size_t OFF_SH_TYPE      =  4;
constexpr size_t OFF_SH_FLAGS     =  8;
constexpr size_t OFF_SH_ADDR      = 12;
constexpr size_t OFF_SH_OFFSET    = 16;
constexpr size_t OFF_SH_SIZE      = 20;
constexpr size_t OFF_SH_LINK      = 24;
constexpr size_t OFF_SH_INFO      = 28;
constexpr size_t OFF_SH_ADDRALIGN = 32;
constexpr size_t OFF_SH_ENTSIZE   = 36;

// Program header (32 bytes).
constexpr size_t OFF_P_TYPE   =  0;
constexpr size_t OFF_P_OFFSET =  4;
constexpr size_t OFF_P_VADDR  =  8;
constexpr size_t OFF_P_PADDR  = 12;
constexpr size_t OFF_P_FILESZ = 16;
constexpr size_t OFF_P_MEMSZ  = 20;
constexpr size_t OFF_P_FLAGS  = 24;

// Symbol table entry (16 bytes).
constexpr size_t OFF_ST_NAME  =  0;
constexpr size_t OFF_ST_VALUE =  4;
constexpr size_t OFF_ST_SIZE  =  8;
constexpr size_t OFF_ST_INFO  = 12;
constexpr size_t OFF_ST_OTHER = 13;
constexpr size_t OFF_ST_SHNDX = 14;
constexpr size_t SZ_SYM       = 16;


// ---------------------- BE accessors ----------------------

inline uint16_t be16(const uint8_t* p) {
    return uint16_t((uint16_t(p[0]) << 8) | p[1]);
}
inline uint32_t be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) <<  8) |  uint32_t(p[3]);
}


// ---------------------- Helpers ----------------------

bool slurpFile(const std::string& path, std::vector<uint8_t>& out, std::string& err) {
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

// Decide which logical segment a section name belongs to.
SecId classifyBySectionName(const std::string& name) {
    if (name == ".text" || name.rfind(".text.", 0) == 0 ||
        name == ".rodata" || name.rfind(".rodata.", 0) == 0 ||
        name == ".init" || name == ".fini") {
        return SecId::Text;
    }
    if (name == ".data" || name.rfind(".data.", 0) == 0 ||
        name == ".sdata" || name.rfind(".sdata.", 0) == 0) {
        return SecId::Data;
    }
    if (name == ".bss" || name.rfind(".bss.", 0) == 0 ||
        name == ".sbss" || name.rfind(".sbss.", 0) == 0 ||
        name == "COMMON") {
        return SecId::Bss;
    }
    return SecId::Undef;
}

const char* cstrAtOffset(const std::vector<uint8_t>& buf, size_t base, uint32_t off) {
    if (base + off >= buf.size()) {
        return "";
    }
    return reinterpret_cast<const char*>(buf.data() + base + off);
}

// Forward decl for the ET_DYN ("ELF .REL") section-based path defined
// at the bottom of this file.  Used by Phase 4 (absolute output) and
// Phase 5 (relocatable DRI output).
ReadResult readElfDyn_(const std::vector<uint8_t>& buf,
                       const ElfReadOptions& opts,
                       uint32_t e_shoff, uint16_t e_shnum, uint16_t e_shstrndx,
                       uint32_t e_phoff, uint16_t e_phnum,
                       uint32_t e_entry,
                       Image& out,
                       std::string& diag);

} // namespace


ReadResult readElf(const std::string& path,
                   const ElfReadOptions& opts,
                   Image& out,
                   std::string& diag)
{
    std::vector<uint8_t> buf;
    if (!slurpFile(path, buf, diag)) {
        return ReadResult::IoError;
    }

    // ---------- ELF header ----------
    if (buf.size() < SZ_EHDR32) {
        diag = "file too small to be an ELF (" + std::to_string(buf.size()) + " bytes)";
        return ReadResult::UnsupportedFormat;
    }
    const uint8_t* hdr = buf.data();
    if (hdr[0] != ELFMAG0 || hdr[1] != ELFMAG1 || hdr[2] != ELFMAG2 || hdr[3] != ELFMAG3) {
        diag = "not an ELF file";
        return ReadResult::UnsupportedFormat;
    }
    if (hdr[4] != ELFCLASS32) {
        diag = "only 32-bit ELF is supported (EI_CLASS=" +
               std::to_string(int(hdr[4])) + ")";
        return ReadResult::UnsupportedFormat;
    }
    if (hdr[5] != ELFDATA2MSB) {
        diag = "only big-endian ELF is supported (EI_DATA=" +
               std::to_string(int(hdr[5])) + ")";
        return ReadResult::UnsupportedFormat;
    }
    uint16_t e_type    = be16(hdr + OFF_E_TYPE);
    uint16_t e_machine = be16(hdr + OFF_E_MACHINE);
    uint32_t e_entry   = be32(hdr + OFF_E_ENTRY);
    uint32_t e_phoff   = be32(hdr + OFF_E_PHOFF);
    uint32_t e_shoff   = be32(hdr + OFF_E_SHOFF);
    uint16_t e_phentsize = be16(hdr + OFF_E_PHENTSIZE);
    uint16_t e_phnum     = be16(hdr + OFF_E_PHNUM);
    uint16_t e_shentsize = be16(hdr + OFF_E_SHENTSIZE);
    uint16_t e_shnum     = be16(hdr + OFF_E_SHNUM);
    uint16_t e_shstrndx  = be16(hdr + OFF_E_SHSTRNDX);

    if (e_type != ET_EXEC && e_type != ET_DYN) {
        diag = "input is not a loadable ELF (e_type=" +
               std::to_string(e_type) +
               ", expected ET_EXEC=2 or ET_DYN=3)";
        return ReadResult::UnsupportedFormat;
    }
    if (e_machine != EM_68K) {
        diag = "input is for machine " + std::to_string(e_machine) +
               ", expected EM_68K (4)";
        return ReadResult::UnsupportedFormat;
    }
    if (e_phentsize != 32 || e_shentsize != 40) {
        diag = "unexpected ELF entry sizes (phentsize=" +
               std::to_string(e_phentsize) + " shentsize=" +
               std::to_string(e_shentsize) + ")";
        return ReadResult::UnsupportedFormat;
    }
    if (e_phnum == 0) {
        diag = "no program headers — input is not loadable";
        return ReadResult::UnsupportedFormat;
    }

    out.entry = e_entry;

    // ----- ET_DYN ("ELF .REL") branch ---------------------------------
    // Section-based extraction + R_68K_RELATIVE collection.  The rest
    // of this function (PT_LOAD-based extraction) is for ET_EXEC only.
    if (e_type == ET_DYN) {
        return readElfDyn_(buf, opts, e_shoff, e_shnum, e_shstrndx,
                           e_phoff, e_phnum, e_entry, out, diag);
    }

    // ---------- Program headers → text / data / bss ----------
    int loadIdx = 0;
    for (uint16_t i = 0; i < e_phnum; ++i) {
        size_t base = size_t(e_phoff) + size_t(i) * 32;
        if (base + 32 > buf.size()) {
            diag = "program header " + std::to_string(i) + " out of range";
            return ReadResult::UnsupportedFormat;
        }
        const uint8_t* ph = buf.data() + base;
        uint32_t p_type   = be32(ph + OFF_P_TYPE);
        uint32_t p_offset = be32(ph + OFF_P_OFFSET);
        uint32_t p_vaddr  = be32(ph + OFF_P_VADDR);
        uint32_t p_filesz = be32(ph + OFF_P_FILESZ);
        uint32_t p_memsz  = be32(ph + OFF_P_MEMSZ);
        uint32_t p_flags  = be32(ph + OFF_P_FLAGS);

        if (p_type != PT_LOAD) {
            continue;
        }
        if (size_t(p_offset) + size_t(p_filesz) > buf.size()) {
            diag = "PT_LOAD segment " + std::to_string(i) +
                   " extends past end of file";
            return ReadResult::UnsupportedFormat;
        }

        // GNU ld's default for `-Ttext NNN` with no linker script puts
        // the ELF header at vaddr 0 and lets the text section start at
        // NNN inside a single combined PT_LOAD with vaddr 0.  To match
        // DRI c.out semantics — where ch_entry IS the text load address
        // — trim the leading header/padding bytes when the PT_LOAD
        // starts below e_entry but spans it.
        uint32_t segVA      = p_vaddr;
        uint32_t segOff     = p_offset;
        uint32_t segFilesz  = p_filesz;
        uint32_t segMemsz   = p_memsz;
        if (segVA < e_entry && e_entry < segVA + segMemsz) {
            uint32_t skip = e_entry - segVA;
            // Verify the skipped region is pure padding (zero) and
            // does not encode anything the program might reference.
            // Bytes that belong to the ELF header + program-header
            // table are always legitimately non-zero — exclude them
            // from the "is this padding?" sniff.
            uint32_t skipInFile = (skip < segFilesz) ? skip : segFilesz;
            uint32_t hdrEnd = 0;
            if (segOff == 0) {
                hdrEnd = uint32_t(SZ_EHDR32) +
                         uint32_t(e_phnum) * uint32_t(e_phentsize);
            }
            bool padOnly = true;
            for (uint32_t k = hdrEnd; k < skipInFile; ++k) {
                if (buf[segOff + k] != 0) { padOnly = false; break; }
            }
            if (!padOnly && opts.verbose) {
                std::fprintf(stderr,
                    "mkdri: warning: PT_LOAD %u contains non-zero bytes "
                    "between offset 0x%X and 0x%X (before e_entry); "
                    "trimming anyway — re-link with -Ttext == load address "
                    "to avoid this\n",
                    unsigned(i), unsigned(hdrEnd), unsigned(skipInFile));
            }
            segVA      = e_entry;
            segOff    += skipInFile;
            if (segFilesz > skipInFile) segFilesz -= skipInFile;
            else                        segFilesz  = 0;
            if (segMemsz  > skip)       segMemsz  -= skip;
            else                        segMemsz   = 0;
        }

        // First load segment with X flag → text.  Second load segment
        // (typically W) → data + bss.
        const bool isExec = (p_flags & PF_X) != 0;

        if (isExec && out.text.empty()) {
            out.textVA = segVA;
            out.text.assign(buf.begin() + segOff,
                            buf.begin() + segOff + segFilesz);
            if (segMemsz > segFilesz) {
                // Tail zero-fill — treat as bss following text.
                out.bssVA   = segVA + segFilesz;
                out.bssSize = segMemsz - segFilesz;
            }
        } else if (!isExec && out.data.empty()) {
            out.dataVA = segVA;
            out.data.assign(buf.begin() + segOff,
                            buf.begin() + segOff + segFilesz);
            if (segMemsz > segFilesz) {
                out.bssVA   = segVA + segFilesz;
                out.bssSize = segMemsz - segFilesz;
            }
        } else {
            diag = "more than two PT_LOAD segments — mkdri Phase 1 "
                   "supports only a single text + single data layout";
            return ReadResult::UnsupportedFeature;
        }
        ++loadIdx;
    }

    if (out.text.empty()) {
        diag = "no executable PT_LOAD segment found";
        return ReadResult::UnsupportedFormat;
    }

    // ---------- Section headers ----------
    // Used (a) to validate section-name → segment mapping for symbols,
    // (b) to detect any unresolved externals, and (c) to recover the
    // text/.data boundary when GNU ld fused .text and .data into a
    // single combined RWX PT_LOAD (see "Combined-PT_LOAD split" below).
    std::vector<SecId>       sectionClass(e_shnum, SecId::Undef);
    std::vector<std::string> sectionName (e_shnum);
    std::vector<uint32_t>    sectionAddr (e_shnum, 0);
    std::vector<uint32_t>    sectionSize (e_shnum, 0);
    std::vector<uint32_t>    sectionType_(e_shnum, 0);
    uint32_t shstrOff = 0;
    uint32_t shstrSize = 0;

    if (e_shnum > 0) {
        if (e_shstrndx >= e_shnum) {
            diag = "section-header string-table index out of range";
            return ReadResult::UnsupportedFormat;
        }
        size_t shstrShBase = size_t(e_shoff) + size_t(e_shstrndx) * 40;
        if (shstrShBase + 40 > buf.size()) {
            diag = "section header string table descriptor out of range";
            return ReadResult::UnsupportedFormat;
        }
        shstrOff  = be32(buf.data() + shstrShBase + OFF_SH_OFFSET);
        shstrSize = be32(buf.data() + shstrShBase + OFF_SH_SIZE);
        if (size_t(shstrOff) + size_t(shstrSize) > buf.size()) {
            diag = "section-header string table extends past end of file";
            return ReadResult::UnsupportedFormat;
        }
    }

    // First pass: classify every section by its name and capture
    // VA/size/type fields used by the combined-PT_LOAD split below.
    for (uint16_t i = 0; i < e_shnum; ++i) {
        size_t shBase = size_t(e_shoff) + size_t(i) * 40;
        if (shBase + 40 > buf.size()) {
            diag = "section header " + std::to_string(i) + " out of range";
            return ReadResult::UnsupportedFormat;
        }
        const uint8_t* sh = buf.data() + shBase;
        uint32_t sh_name  = be32(sh + OFF_SH_NAME);
        std::string name = cstrAtOffset(buf, shstrOff, sh_name);
        sectionName[i]  = name;
        sectionClass[i] = classifyBySectionName(name);
        sectionType_[i] = be32(sh + OFF_SH_TYPE);
        sectionAddr[i]  = be32(sh + OFF_SH_ADDR);
        sectionSize[i]  = be32(sh + OFF_SH_SIZE);
    }

    // ---------- Combined-PT_LOAD split ----------
    // GNU ld with our default linker script emits a single combined
    // RWX PT_LOAD spanning .text + .data + .bss (and warns
    // "LOAD segment with RWX permissions").  When that happens the
    // PT_LOAD walk above stuffs the text *and* data bytes into
    // out.text, leaving out.data empty — the .68K header would then
    // report data length = 0, which misleads debuggers like sid68k
    // and any tool that consumes the DRI section layout.
    //
    // CP/M-68K loads text and data contiguously into the TPA, so the
    // bytes themselves end up in the right places either way, but we
    // still want the DRI header to faithfully reflect the linked
    // boundary.  Recover it by finding the .data section (by name,
    // SHT_PROGBITS, non-zero size) whose linked VA falls inside
    // out.text's VA range, and splitting the byte vector at that
    // offset.  bssVA / bssSize were already computed correctly from
    // the PT_LOAD's (segMemsz - segFilesz) tail, so we leave them
    // alone.
    if (out.data.empty() && !out.text.empty()) {
        const uint32_t textEnd = out.textVA + uint32_t(out.text.size());
        for (uint16_t i = 0; i < e_shnum; ++i) {
            const std::string& nm = sectionName[i];
            const bool isDataName =
                (nm == ".data" || nm.rfind(".data.", 0) == 0 ||
                 nm == ".sdata" || nm.rfind(".sdata.", 0) == 0);
            if (!isDataName) continue;
            if (sectionType_[i] != SHT_PROGBITS) continue;
            if (sectionSize[i] == 0) continue;
            const uint32_t dva = sectionAddr[i];
            if (dva < out.textVA || dva >= textEnd) continue;

            const uint32_t splitOff = dva - out.textVA;
            const uint32_t available = uint32_t(out.text.size()) - splitOff;
            const uint32_t dsize = (sectionSize[i] < available)
                                       ? sectionSize[i] : available;
            out.dataVA = dva;
            out.data.assign(out.text.begin() + splitOff,
                            out.text.begin() + splitOff + dsize);
            out.text.resize(splitOff);

            if (opts.verbose) {
                std::fprintf(stderr,
                    "mkdri: ELF: split combined RWX PT_LOAD using .data "
                    "section header (text=0x%X bytes, data=0x%X bytes at "
                    "VA 0x%08X)\n",
                    unsigned(out.text.size()),
                    unsigned(out.data.size()),
                    unsigned(out.dataVA));
            }
            break;
        }
    }

    // Second pass: find .symtab + .strtab and walk it.
    int symtabIdx = -1;
    int strtabIdx = -1;
    uint32_t symtabOff = 0, symtabSize = 0, symtabEntsize = 0;
    for (uint16_t i = 0; i < e_shnum; ++i) {
        const uint8_t* sh = buf.data() + size_t(e_shoff) + size_t(i) * 40;
        uint32_t sh_type = be32(sh + OFF_SH_TYPE);
        if (sh_type == SHT_SYMTAB) {
            symtabIdx     = int(i);
            symtabOff     = be32(sh + OFF_SH_OFFSET);
            symtabSize    = be32(sh + OFF_SH_SIZE);
            symtabEntsize = be32(sh + OFF_SH_ENTSIZE);
            strtabIdx     = int(be32(sh + OFF_SH_LINK));
            break;
        }
    }

    if (symtabIdx >= 0) {
        if (symtabEntsize != SZ_SYM) {
            diag = "unexpected symbol table entsize " +
                   std::to_string(symtabEntsize) + " (expected 16)";
            return ReadResult::UnsupportedFormat;
        }
        if (size_t(symtabOff) + size_t(symtabSize) > buf.size()) {
            diag = ".symtab extends past end of file";
            return ReadResult::UnsupportedFormat;
        }
        if (strtabIdx < 0 || strtabIdx >= int(e_shnum)) {
            diag = ".symtab sh_link does not point to a valid string table";
            return ReadResult::UnsupportedFormat;
        }
        const uint8_t* stsh = buf.data() + size_t(e_shoff) +
                              size_t(strtabIdx) * 40;
        uint32_t strtabOff  = be32(stsh + OFF_SH_OFFSET);
        uint32_t strtabSize = be32(stsh + OFF_SH_SIZE);
        if (size_t(strtabOff) + size_t(strtabSize) > buf.size()) {
            diag = ".strtab extends past end of file";
            return ReadResult::UnsupportedFormat;
        }

        std::vector<std::string> unresolved;
        uint32_t nSyms = symtabSize / SZ_SYM;
        out.symbols.reserve(nSyms);
        for (uint32_t i = 0; i < nSyms; ++i) {
            const uint8_t* sym = buf.data() + symtabOff + i * SZ_SYM;
            uint32_t st_name  = be32(sym + OFF_ST_NAME);
            uint32_t st_value = be32(sym + OFF_ST_VALUE);
            uint8_t  st_info  = sym[OFF_ST_INFO];
            uint16_t st_shndx = be16(sym + OFF_ST_SHNDX);
            uint8_t  bind = uint8_t(st_info >> 4);
            uint8_t  type = uint8_t(st_info & 0xF);

            std::string nm = cstrAtOffset(buf, strtabOff, st_name);

            // Skip the mandatory STN_UNDEF zero-entry and section/file
            // pseudo-symbols.
            if (i == 0) continue;
            if (type == STT_SECTION || type == STT_FILE) continue;

            Symbol s;
            s.name    = nm;
            s.value   = st_value;
            s.isWeak  = (bind == STB_WEAK);

            if (st_shndx == SHN_UNDEF) {
                s.isUndefined = true;
                s.section     = SecId::Undef;
                if (!nm.empty() && bind != STB_WEAK) {
                    unresolved.push_back(nm);
                }
            } else if (st_shndx == SHN_ABS) {
                s.section = SecId::Abs;
            } else if (st_shndx == SHN_COMMON) {
                // Linker should have allocated; flag as unresolved
                // because COMMON storage was not laid down.
                s.section = SecId::Bss;
                if (!nm.empty() && bind != STB_WEAK) {
                    unresolved.push_back(nm + " (SHN_COMMON)");
                }
            } else if (st_shndx < e_shnum) {
                s.section = sectionClass[st_shndx];
            }

            out.symbols.push_back(std::move(s));
        }

        // The linker has already accepted ET_EXEC inputs as complete,
        // so any undef entries here are historical references the
        // linker resolved through other means (e.g. e_entry coming
        // from `-Ttext` rather than from a `_start` definition).
        // Phase 1 does not consume relocations, so undef symbols
        // cannot actually fail at runtime — surface them only in
        // verbose mode.  Phase 1.5 will reject undef symbols only
        // when a relocation actually references them.
        if (!unresolved.empty() && opts.verbose) {
            std::ostringstream oss;
            oss << "mkdri: note: " << unresolved.size()
                << " undefined symbol(s) in symtab (benign for ET_EXEC "
                   "without relocations): ";
            for (size_t i = 0; i < unresolved.size() && i < 8; ++i) {
                if (i) oss << ", ";
                oss << unresolved[i];
            }
            if (unresolved.size() > 8) oss << ", ...";
            std::fprintf(stderr, "%s\n", oss.str().c_str());
        }
    }

    // Note: ET_EXEC inputs may carry SHT_RELA/SHT_REL sections when
    // linked with `-q` / `--emit-relocs`.  Those are informational
    // and the ET_EXEC path does not consume them — bytes pass through
    // at the linker's VA.  For relocation-aware input use the ET_DYN
    // ("ELF .REL") path (Phase 4 / Phase 5).

    if (opts.verbose) {
        std::fprintf(stderr,
            "mkdri: ELF: entry=0x%08X text=[VA 0x%08X, %u bytes] "
            "data=[VA 0x%08X, %u bytes] bss=[VA 0x%08X, %u bytes] "
            "symbols=%zu hasRelocs=%d\n",
            out.entry,
            out.textVA, unsigned(out.text.size()),
            out.dataVA, unsigned(out.data.size()),
            out.bssVA,  unsigned(out.bssSize),
            out.symbols.size(),
            int(out.hasRelocs));
    }

    return ReadResult::Ok;
}

// =====================================================================
// ET_DYN ("ELF .REL") section-based reader for Phase 4 / Phase 5.
//
// Input is a fully linked ET_DYN produced by:
//     m68k-elf-ld -shared -Bsymbolic --no-undefined ...
//
// We extract ONLY the program's .text, .rodata, .data, .bss sections
// (skipping dynamic-loader infrastructure: .dynamic, .got, .dynsym,
// .dynstr, .hash, .rela.dyn).  Each R_68K_RELATIVE entry from
// .rela.dyn is reclassified into a canonical Reloc record keyed by:
//   - inSection    : which output buffer holds the patch site
//   - offset       : byte offset within that buffer
//   - targetSection: which output buffer the addend points into
//   - addend       : the linker-recorded VA of the target
// hasRelocs is set so the writer can either apply per-segment bias
// (Phase 4) or re-emit a relocation-bit stream (Phase 5).
// =====================================================================

namespace {

// Pick the section that best matches a logical role.  Picks the largest
// SHF_ALLOC section whose name matches, ignoring NOBITS for non-bss
// roles.
struct SecRef {
    uint16_t idx     = 0xFFFF;
    uint32_t va      = 0;
    uint32_t offset  = 0;
    uint32_t size    = 0;
    bool     present = false;
};

ReadResult readElfDyn_(const std::vector<uint8_t>& buf,
                       const ElfReadOptions& opts,
                       uint32_t e_shoff, uint16_t e_shnum, uint16_t e_shstrndx,
                       uint32_t /*e_phoff*/, uint16_t /*e_phnum*/,
                       uint32_t e_entry,
                       Image& out,
                       std::string& diag)
{
    if (e_shnum == 0) {
        diag = "ET_DYN input has no section headers — cannot extract "
               ".text/.data/.bss";
        return ReadResult::UnsupportedFormat;
    }
    if (e_shstrndx >= e_shnum) {
        diag = "section-header string-table index out of range";
        return ReadResult::UnsupportedFormat;
    }

    // Locate .shstrtab.
    size_t shstrShBase = size_t(e_shoff) + size_t(e_shstrndx) * 40;
    if (shstrShBase + 40 > buf.size()) {
        diag = "section header string table descriptor out of range";
        return ReadResult::UnsupportedFormat;
    }
    uint32_t shstrOff  = be32(buf.data() + shstrShBase + OFF_SH_OFFSET);
    uint32_t shstrSize = be32(buf.data() + shstrShBase + OFF_SH_SIZE);
    if (size_t(shstrOff) + size_t(shstrSize) > buf.size()) {
        diag = "section-header string table extends past end of file";
        return ReadResult::UnsupportedFormat;
    }

    // Walk all sections, gather names + flags.
    std::vector<std::string> sectionName(e_shnum);
    std::vector<uint32_t>    sectionType(e_shnum);
    std::vector<uint32_t>    sectionFlags(e_shnum);
    std::vector<uint32_t>    sectionAddr(e_shnum);
    std::vector<uint32_t>    sectionOff(e_shnum);
    std::vector<uint32_t>    sectionSize(e_shnum);
    std::vector<uint32_t>    sectionLink(e_shnum);
    std::vector<uint32_t>    sectionEntsize(e_shnum);

    for (uint16_t i = 0; i < e_shnum; ++i) {
        size_t shBase = size_t(e_shoff) + size_t(i) * 40;
        if (shBase + 40 > buf.size()) {
            diag = "section header " + std::to_string(i) + " out of range";
            return ReadResult::UnsupportedFormat;
        }
        const uint8_t* sh = buf.data() + shBase;
        uint32_t sh_name = be32(sh + OFF_SH_NAME);
        sectionName[i]    = cstrAtOffset(buf, shstrOff, sh_name);
        sectionType[i]    = be32(sh + OFF_SH_TYPE);
        sectionFlags[i]   = be32(sh + OFF_SH_FLAGS);
        sectionAddr[i]    = be32(sh + OFF_SH_ADDR);
        sectionOff[i]     = be32(sh + OFF_SH_OFFSET);
        sectionSize[i]    = be32(sh + OFF_SH_SIZE);
        sectionLink[i]    = be32(sh + OFF_SH_LINK);
        sectionEntsize[i] = be32(sh + OFF_SH_ENTSIZE);
    }

    // Pick text / data / bss.  We look ONLY at the program's named
    // sections; .got / .dynamic / .dynsym etc. are ignored.
    SecRef textSec, dataSec, bssSec;
    for (uint16_t i = 0; i < e_shnum; ++i) {
        const std::string& nm = sectionName[i];
        uint32_t f  = sectionFlags[i];
        uint32_t ty = sectionType[i];
        if (!(f & SHF_ALLOC)) continue;

        const bool isText = (nm == ".text" || nm.rfind(".text.", 0) == 0 ||
                             nm == ".init" || nm == ".fini" ||
                             nm == ".rodata" || nm.rfind(".rodata.", 0) == 0);
        const bool isData = (nm == ".data" || nm.rfind(".data.", 0) == 0 ||
                             nm == ".sdata" || nm.rfind(".sdata.", 0) == 0);
        const bool isBss  = (nm == ".bss"  || nm.rfind(".bss.", 0) == 0 ||
                             nm == ".sbss" || nm.rfind(".sbss.", 0) == 0);
        if (isText && ty == SHT_PROGBITS && !textSec.present) {
            textSec = { i, sectionAddr[i], sectionOff[i], sectionSize[i], true };
        } else if (isData && ty == SHT_PROGBITS && !dataSec.present) {
            dataSec = { i, sectionAddr[i], sectionOff[i], sectionSize[i], true };
        } else if (isBss && ty == SHT_NOBITS && !bssSec.present) {
            bssSec  = { i, sectionAddr[i], 0,             sectionSize[i], true };
        }
    }

    if (!textSec.present) {
        diag = "ET_DYN input has no .text section";
        return ReadResult::UnsupportedFormat;
    }
    if (size_t(textSec.offset) + size_t(textSec.size) > buf.size()) {
        diag = ".text section extends past end of file";
        return ReadResult::UnsupportedFormat;
    }
    if (dataSec.present &&
        size_t(dataSec.offset) + size_t(dataSec.size) > buf.size()) {
        diag = ".data section extends past end of file";
        return ReadResult::UnsupportedFormat;
    }

    // Populate text / data / bss.
    out.textVA  = textSec.va;
    out.text.assign(buf.begin() + textSec.offset,
                    buf.begin() + textSec.offset + textSec.size);
    if (dataSec.present) {
        out.dataVA = dataSec.va;
        out.data.assign(buf.begin() + dataSec.offset,
                        buf.begin() + dataSec.offset + dataSec.size);
    } else {
        out.dataVA = textSec.va + textSec.size;
    }
    if (bssSec.present) {
        out.bssVA   = bssSec.va;
        out.bssSize = bssSec.size;
    } else {
        out.bssVA   = out.dataVA + uint32_t(out.data.size());
        out.bssSize = 0;
    }
    out.entry = e_entry;

    // -------- Symbol table (prefer .symtab; fall back to .dynsym) ----
    // Same logic as the ET_EXEC path, but classification keys off the
    // section we extracted (text / data / bss) by VA range, since the
    // section indices in the file refer to many "infrastructure"
    // sections we discarded.
    auto classifyByVa = [&](uint32_t va) -> SecId {
        if (va >= out.textVA && va < out.textVA + out.text.size())
            return SecId::Text;
        if (va >= out.dataVA && va < out.dataVA + out.data.size())
            return SecId::Data;
        if (va >= out.bssVA  && va < out.bssVA  + out.bssSize)
            return SecId::Bss;
        return SecId::Undef;
    };

    int symtabIdx = -1;
    for (uint16_t i = 0; i < e_shnum; ++i) {
        if (sectionType[i] == SHT_SYMTAB) { symtabIdx = i; break; }
    }
    if (symtabIdx < 0) {
        for (uint16_t i = 0; i < e_shnum; ++i) {
            // SHT_DYNSYM = 11 (m68k SysV); we use raw constant since
            // it's only referenced here.
            if (sectionType[i] == 11) { symtabIdx = i; break; }
        }
    }
    if (symtabIdx >= 0) {
        uint32_t symOff     = sectionOff[symtabIdx];
        uint32_t symSize    = sectionSize[symtabIdx];
        uint32_t symEntsize = sectionEntsize[symtabIdx];
        uint32_t strIdx     = sectionLink[symtabIdx];
        if (symEntsize == SZ_SYM && strIdx < e_shnum &&
            size_t(symOff) + size_t(symSize) <= buf.size()) {
            uint32_t strOff  = sectionOff[strIdx];
            uint32_t strSize = sectionSize[strIdx];
            if (size_t(strOff) + size_t(strSize) <= buf.size()) {
                uint32_t nSyms = symSize / SZ_SYM;
                out.symbols.reserve(nSyms);
                for (uint32_t i = 0; i < nSyms; ++i) {
                    const uint8_t* sym = buf.data() + symOff + i * SZ_SYM;
                    uint32_t st_name  = be32(sym + OFF_ST_NAME);
                    uint32_t st_value = be32(sym + OFF_ST_VALUE);
                    uint8_t  st_info  = sym[OFF_ST_INFO];
                    uint16_t st_shndx = be16(sym + OFF_ST_SHNDX);
                    uint8_t  bind = uint8_t(st_info >> 4);
                    uint8_t  type = uint8_t(st_info & 0xF);
                    if (i == 0) continue;
                    if (type == STT_SECTION || type == STT_FILE) continue;

                    Symbol s;
                    s.name   = cstrAtOffset(buf, strOff, st_name);
                    s.value  = st_value;
                    s.isWeak = (bind == STB_WEAK);
                    if (st_shndx == SHN_UNDEF) {
                        s.isUndefined = true;
                        s.section     = SecId::Undef;
                    } else if (st_shndx == SHN_ABS) {
                        s.section = SecId::Abs;
                    } else {
                        s.section = classifyByVa(st_value);
                    }
                    if (!s.name.empty())
                        out.symbols.push_back(std::move(s));
                }
            }
        }
    }

    // -------- Collect R_68K_RELATIVE relocations from .rela.dyn ------
    // (Or any SHT_RELA; we accept only R_68K_RELATIVE / R_68K_NONE,
    // since these are the only kinds -Bsymbolic should emit in an
    // ET_DYN that has no unresolved externals.)
    auto siteOffsetIn = [&](uint32_t va, SecId& sec, uint32_t& siteOff) -> bool {
        if (va >= out.textVA && va < out.textVA + out.text.size()) {
            sec = SecId::Text; siteOff = va - out.textVA;  return true;
        }
        if (va >= out.dataVA && va < out.dataVA + out.data.size()) {
            sec = SecId::Data; siteOff = va - out.dataVA;  return true;
        }
        return false;
    };

    uint32_t skippedRelocs = 0;
    for (uint16_t i = 0; i < e_shnum; ++i) {
        if (sectionType[i] != SHT_RELA) continue;
        uint32_t off  = sectionOff[i];
        uint32_t size = sectionSize[i];
        uint32_t ent  = sectionEntsize[i];
        if (ent != 12) {
            diag = "SHT_RELA section '" + sectionName[i] +
                   "' has unexpected entsize " + std::to_string(ent);
            return ReadResult::UnsupportedFormat;
        }
        if (size_t(off) + size_t(size) > buf.size()) {
            diag = "SHT_RELA section '" + sectionName[i] +
                   "' extends past end of file";
            return ReadResult::UnsupportedFormat;
        }
        uint32_t n = size / 12;
        for (uint32_t j = 0; j < n; ++j) {
            const uint8_t* re = buf.data() + off + j * 12;
            uint32_t r_offset = be32(re + 0);
            uint32_t r_info   = be32(re + 4);
            uint32_t r_addend = be32(re + 8);
            uint8_t  rtype    = uint8_t(r_info & 0xFFu);

            if (rtype == R_68K_NONE) continue;
            if (rtype != R_68K_RELATIVE) {
                diag = "ET_DYN input has unsupported relocation type R_68K_? = " +
                       std::to_string(rtype) +
                       " (mkdri Phase 4/5 only supports R_68K_RELATIVE; "
                       "re-link with -shared -Bsymbolic --no-undefined)";
                return ReadResult::UnsupportedFeature;
            }

            // Place the patch site.  Sites outside .text/.data are
            // dynamic-loader internals (GOT, DT_INIT_ARRAY, ...) and
            // are skipped — we don't carry that infrastructure into
            // the DRI image.
            SecId    inSec;
            uint32_t siteOff;
            if (!siteOffsetIn(r_offset, inSec, siteOff)) {
                ++skippedRelocs;
                continue;
            }

            // Classify the target by VA.
            SecId target = classifyByVa(r_addend);
            if (target == SecId::Undef) {
                // The addend points outside text/data/bss; treat as
                // an absolute reference (the DRI runtime will store
                // it verbatim).
                target = SecId::Abs;
            }

            Reloc rl;
            rl.kind          = Reloc::ABS32;
            rl.inSection     = inSec;
            rl.offset        = siteOff;
            rl.targetSection = target;
            rl.addend        = int32_t(r_addend);
            out.relocs.push_back(rl);
        }
    }

    out.hasRelocs = true;

    if (opts.verbose) {
        std::fprintf(stderr,
            "mkdri: ELF(ET_DYN): entry=0x%08X text=[VA 0x%08X, %u bytes] "
            "data=[VA 0x%08X, %u bytes] bss=[VA 0x%08X, %u bytes] "
            "symbols=%zu relocs=%zu skipped=%u\n",
            out.entry,
            out.textVA, unsigned(out.text.size()),
            out.dataVA, unsigned(out.data.size()),
            out.bssVA,  unsigned(out.bssSize),
            out.symbols.size(),
            out.relocs.size(),
            unsigned(skippedRelocs));
    }

    return ReadResult::Ok;
}

} // namespace

} // namespace mkdri
