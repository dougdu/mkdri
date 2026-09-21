// mkdri/main.cpp
//
// CLI front-end for the ELF/COFF/DRI → absolute-DRI converter.
//
// See MKDRI_SPEC.md (co-located in this folder) for the
// full specification.  Phases:
//   1: ELF (ET_EXEC) pass-through
//   2: COFF (m68k F_EXEC) pass-through
//   3: DRI .REL → absolute (byte-identical to reloc.exe)
//   4: ELF (ET_DYN, -shared -Bsymbolic) → absolute DRI (apply bias)
//   5: ELF (ET_DYN) → DRI .REL relocatable (-r)
//   6: optional symbol-map dump (-y[<file>])

#include "Image.h"
#include "ElfReader.h"
#include "CoffReader.h"
#include "DriReader.h"
#include "DriWriter.h"
#include "DriRelWriter.h"
#include "SymFile.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// --------------------------------------------------------------------

namespace {

constexpr const char* MKDRI_VERSION = "1.3.1 (ELF/COFF/DRI to absolute/relocatable DRI + symbol map)";

enum class InputFormat { Auto, Elf, Coff, Dri };

struct Cli {
    std::optional<uint32_t> textBase;
    std::optional<uint32_t> dataBase;
    std::optional<uint32_t> bssBase;
    uint32_t                stackSize = 0;
    std::string             output;
    InputFormat             format = InputFormat::Auto;
    bool                    verbose = false;
    bool                    quiet   = false;
    bool                    relocatable = false;   // -r: emit DRI .REL
    bool                    emitSymFile = false;   // -y[<file>] present
    std::optional<std::string> symFile;            // -y<file> override
    std::string             input;
};

[[noreturn]] void usage(int rc) {
    std::FILE* out = (rc == 0) ? stdout : stderr;
    std::fprintf(out,
        "mkdri %s - ELF/COFF/DRI to DRI converter\n"
        "Usage:\n"
        "    mkdri [-b<HEX>] [-o <output>] [-f auto|elf|coff|dri] "
        "[-D<HEX>] [-B<HEX>]\n"
        "          [-S<HEX>] [-r] [-y[<symfile>]] [-q|-v] <input>\n"
        "\n"
        "  -b<HEX>      Base load address for the text segment "
        "(default: input's linked address)\n"
        "  -o <output>  Output filename (default: <input>.68K, or .REL with -r)\n"
        "  -f <format>  Force input format; default = auto-detect "
        "by magic number\n"
        "  -D<HEX>      Data segment load address (forces split layout, "
        "EX_ABMAGIC)\n"
        "  -B<HEX>      BSS  segment load address (forces split layout, "
        "EX_ABMAGIC)\n"
        "  -S<HEX>      Initial stack-size hint to record in header "
        "(default: 0)\n"
        "  -r           Emit a DRI relocatable .REL module (Phase 5) "
        "instead of an absolute file\n"
        "  -y[<file>]   Also write a symbol-map file.  If <file> is "
        "omitted, defaults to <output>.sym\n"
        "  -q           Quiet\n"
        "  -v           Verbose\n"
        "  -h           This help\n"
        "  --version    Print version and supported input formats\n"
        "\n"
        "Exit codes:\n"
        "   0 = success\n"
        "   1 = bad command line\n"
        "   2 = unsupported input format\n"
        "   3 = unresolved symbols in input\n"
        "   4 = unsupported relocation type or other internal error\n"
        "   5 = I/O error (cannot read input, cannot write output)\n",
        MKDRI_VERSION);
    std::exit(rc);
}

[[noreturn]] void versionAndExit() {
    std::printf("mkdri %s\n", MKDRI_VERSION);
    std::printf("Supported input formats: ELF  (m68k, big-endian, ET_EXEC + ET_DYN)\n");
    std::printf("                         COFF (m68k, big-endian, F_EXEC)\n");
    std::printf("                         DRI  (c.out 0x601A, replaces 'reloc')\n");
    std::exit(0);
}

bool parseHex(const char* s, uint32_t& out) {
    if (!s || !*s) return false;
    // Allow optional 0x / 0X / $ prefix.
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    else if (s[0] == '$') s += 1;
    uint32_t acc = 0;
    bool any = false;
    for (; *s; ++s) {
        char c = *s;
        uint32_t d;
        if      (c >= '0' && c <= '9') d = uint32_t(c - '0');
        else if (c >= 'a' && c <= 'f') d = uint32_t(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = uint32_t(c - 'A' + 10);
        else return false;
        acc = (acc << 4) | d;
        any = true;
    }
    if (!any) return false;
    out = acc;
    return true;
}

// Take the value for a switch that supports both space-separated and
// glued forms: `-b500` or `-b 500`.  Returns nullptr if the argument
// for the switch is missing.
const char* takeValueFor(const char* flag, int argc, char* argv[], int& i) {
    const char* a = argv[i];
    size_t flagLen = std::strlen(flag);
    if (std::strncmp(a, flag, flagLen) != 0) return nullptr;
    if (a[flagLen] != '\0') {
        return a + flagLen;            // glued form: -b500
    }
    if (i + 1 >= argc) return nullptr; // missing value
    ++i;
    return argv[i];                    // space form: -b 500
}

void parseCli(int argc, char* argv[], Cli& cli) {
    if (argc < 2) usage(1);

    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (a[0] != '-') {
            if (!cli.input.empty()) {
                std::fprintf(stderr,
                    "mkdri: only one input file is supported "
                    "(got '%s' and '%s')\n",
                    cli.input.c_str(), a);
                std::exit(1);
            }
            cli.input = a;
            continue;
        }

        if (std::strcmp(a, "-h") == 0 || std::strcmp(a, "--help") == 0) {
            usage(0);
        }
        if (std::strcmp(a, "--version") == 0) {
            versionAndExit();
        }

        // -b / -T  (text base)
        if (a[0] == '-' && (a[1] == 'b' || a[1] == 'T')) {
            char flag[3] = { '-', a[1], 0 };
            const char* v = takeValueFor(flag, argc, argv, i);
            if (!v) {
                std::fprintf(stderr, "mkdri: %s requires a hex address\n", flag);
                std::exit(1);
            }
            uint32_t x;
            if (!parseHex(v, x)) {
                std::fprintf(stderr,
                    "mkdri: invalid hex address '%s' for %s\n", v, flag);
                std::exit(1);
            }
            cli.textBase = x;
            continue;
        }

        // -base <hex>
        if (std::strcmp(a, "-base") == 0) {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "mkdri: -base requires a hex address\n");
                std::exit(1);
            }
            uint32_t x;
            if (!parseHex(argv[++i], x)) {
                std::fprintf(stderr,
                    "mkdri: invalid hex address '%s' for -base\n", argv[i]);
                std::exit(1);
            }
            cli.textBase = x;
            continue;
        }

        // -D / -B  (data / bss base)
        if (a[0] == '-' && (a[1] == 'D' || a[1] == 'B')) {
            char flag[3] = { '-', a[1], 0 };
            const char* v = takeValueFor(flag, argc, argv, i);
            if (!v) {
                std::fprintf(stderr, "mkdri: %s requires a hex address\n", flag);
                std::exit(1);
            }
            uint32_t x;
            if (!parseHex(v, x)) {
                std::fprintf(stderr,
                    "mkdri: invalid hex address '%s' for %s\n", v, flag);
                std::exit(1);
            }
            if (a[1] == 'D') cli.dataBase = x;
            else             cli.bssBase  = x;
            continue;
        }

        // -S<hex>  stack size
        if (a[0] == '-' && a[1] == 'S') {
            const char* v = takeValueFor("-S", argc, argv, i);
            if (!v) {
                std::fprintf(stderr, "mkdri: -S requires a hex size\n");
                std::exit(1);
            }
            uint32_t x;
            if (!parseHex(v, x)) {
                std::fprintf(stderr,
                    "mkdri: invalid hex size '%s' for -S\n", v);
                std::exit(1);
            }
            cli.stackSize = x;
            continue;
        }

        // -o <path>
        if (a[0] == '-' && a[1] == 'o') {
            const char* v = takeValueFor("-o", argc, argv, i);
            if (!v) {
                std::fprintf(stderr, "mkdri: -o requires a path\n");
                std::exit(1);
            }
            cli.output = v;
            continue;
        }

        // -f <fmt>
        if (a[0] == '-' && a[1] == 'f') {
            const char* v = takeValueFor("-f", argc, argv, i);
            if (!v) {
                std::fprintf(stderr,
                    "mkdri: -f requires one of: auto, elf, coff, dri\n");
                std::exit(1);
            }
            if      (std::strcmp(v, "auto") == 0) cli.format = InputFormat::Auto;
            else if (std::strcmp(v, "elf")  == 0) cli.format = InputFormat::Elf;
            else if (std::strcmp(v, "coff") == 0) cli.format = InputFormat::Coff;
            else if (std::strcmp(v, "dri")  == 0) cli.format = InputFormat::Dri;
            else {
                std::fprintf(stderr,
                    "mkdri: -f: unknown format '%s' "
                    "(expected auto, elf, coff, dri)\n", v);
                std::exit(1);
            }
            continue;
        }

        if (std::strcmp(a, "-q") == 0) { cli.quiet = true;  continue; }
        if (std::strcmp(a, "-v") == 0) { cli.verbose = true; continue; }

        // -r / --rel : emit DRI .REL relocatable instead of absolute.
        if (std::strcmp(a, "-r") == 0 || std::strcmp(a, "--rel") == 0) {
            cli.relocatable = true;
            continue;
        }

        // -y[<file>]  emit symbol-map file.  Bare -y means "use
        // default name <output>.sym"; -y<path> overrides the path.
        if (a[0] == '-' && a[1] == 'y') {
            cli.emitSymFile = true;
            if (a[2] != '\0') {
                cli.symFile = std::string(a + 2);
            } else if (i + 1 < argc && argv[i + 1][0] != '-') {
                // Optional space-separated arg, but only consume it
                // if it doesn't look like another switch.  Filenames
                // beginning with '-' must use the glued form.
                cli.symFile = std::string(argv[++i]);
            }
            continue;
        }

        std::fprintf(stderr, "mkdri: unknown option '%s'\n", a);
        std::exit(1);
    }

    if (cli.input.empty()) {
        std::fprintf(stderr, "mkdri: missing input file (use -h for help)\n");
        std::exit(1);
    }

    // Default output: replace extension with .68K (or .REL for -r),
    // uppercase basename on Windows per CP/M-68K 8.3 convention.
    if (cli.output.empty()) {
        cli.output = cli.input;
        auto slash = cli.output.find_last_of("\\/");
        auto dot   = cli.output.find_last_of('.');
        if (dot != std::string::npos &&
            (slash == std::string::npos || dot > slash)) {
            cli.output.erase(dot);
        }
        cli.output += cli.relocatable ? ".REL" : ".68K";
    }
}

InputFormat detectFormat(const std::string& path) {
    FILE* fp = nullptr;
    if (fopen_s(&fp, path.c_str(), "rb") != 0 || !fp) return InputFormat::Auto;
    uint8_t magic[4] = {0,0,0,0};
    std::fread(magic, 1, 4, fp);
    std::fclose(fp);
    if (magic[0] == 0x7F && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F') {
        return InputFormat::Elf;
    }
    uint16_t w = uint16_t((uint16_t(magic[0]) << 8) | magic[1]);
    if (w == 0x0150 || w == 0x0151 || w == 0x0152 || w == 0x0156) {
        return InputFormat::Coff;
    }
    if (w == 0x601A || w == 0x601B || w == 0x601C ||
        w == 0x601D || w == 0x601E) {
        return InputFormat::Dri;
    }
    return InputFormat::Auto;
}

const char* formatName(InputFormat f) {
    switch (f) {
        case InputFormat::Auto: return "auto";
        case InputFormat::Elf:  return "elf";
        case InputFormat::Coff: return "coff";
        case InputFormat::Dri:  return "dri";
    }
    return "?";
}

} // namespace


int main(int argc, char* argv[]) {
    Cli cli;
    parseCli(argc, argv, cli);

    // ---------- Format dispatch ----------
    InputFormat fmt = cli.format;
    if (fmt == InputFormat::Auto) {
        fmt = detectFormat(cli.input);
        if (fmt == InputFormat::Auto) {
            std::fprintf(stderr,
                "mkdri: unsupported input format for '%s' "
                "(could not detect magic number)\n",
                cli.input.c_str());
            return 2;
        }
    }
    if (cli.verbose) {
        std::fprintf(stderr, "mkdri: input='%s' format=%s\n",
                     cli.input.c_str(), formatName(fmt));
    }

    mkdri::Image image;
    std::string diag;
    mkdri::ReadResult rr;

    switch (fmt) {
        case InputFormat::Elf: {
            mkdri::ElfReadOptions ro;
            ro.verbose = cli.verbose;
            rr = mkdri::readElf(cli.input, ro, image, diag);
            break;
        }
        case InputFormat::Coff: {
            mkdri::CoffReadOptions ro;
            ro.verbose = cli.verbose;
            rr = mkdri::readCoff(cli.input, ro, image, diag);
            break;
        }
        case InputFormat::Dri: {
            mkdri::DriReadOptions ro;
            ro.verbose = cli.verbose;
            // Default DRI target base mirrors reloc.c: 0x500 unless
            // the user passed -b<hex>.
            ro.targetBase = cli.textBase.value_or(0x500u);
            rr = mkdri::readDri(cli.input, ro, image, diag);
            break;
        }
        default:
            std::fprintf(stderr, "mkdri: internal: format dispatch fell through\n");
            return 4;
    }

    if (rr != mkdri::ReadResult::Ok) {
        std::fprintf(stderr, "mkdri: %s\n", diag.c_str());
        return int(rr);
    }

    // ---------- Write ----------
    diag.clear();
    if (cli.relocatable) {
        // Phase 5: emit DRI .REL.  This only works from a reader that
        // produced canonical Reloc entries with real per-site fixup
        // info: today that is the ELF ET_DYN path.  COFF F_EXEC and
        // DRI inputs are pass-through / absolutising readers that
        // never populate image.relocs, so reject them up front
        // rather than silently emitting a .REL with all-zero
        // relocation-bit streams.
        if (!image.hasRelocs || image.relocs.empty()) {
            std::fprintf(stderr,
                "mkdri: -r requires an ELF ET_DYN input that retained "
                "relocations (use 'm68k-elf-ld -shared -Bsymbolic "
                "--no-undefined ...').  COFF F_EXEC and DRI c.out "
                "inputs are already absolute / pre-resolved and cannot "
                "be converted to a DRI .REL by this tool.\n");
            return 4;
        }
        if (cli.textBase || cli.dataBase || cli.bssBase) {
            std::fprintf(stderr,
                "mkdri: warning: -b/-D/-B have no effect with -r "
                "(.REL output is linked at 0; use reloc.exe to slide)\n");
        }
        mkdri::DriRelWriteOptions wo;
        wo.verbose = cli.verbose;
        rr = mkdri::writeDriRel(cli.output, image, wo, diag);
    } else {
        // Phases 1\u20134: emit absolute DRI c.out.
        mkdri::DriWriteOptions wo;
        wo.textBase  = cli.textBase;
        wo.dataBase  = cli.dataBase;
        wo.bssBase   = cli.bssBase;
        wo.stackSize = cli.stackSize;
        wo.verbose   = cli.verbose;
        rr = mkdri::writeDri(cli.output, image, wo, diag);
    }

    if (rr != mkdri::ReadResult::Ok) {
        std::fprintf(stderr, "mkdri: %s\n", diag.c_str());
        return int(rr);
    }

    // ---------- Phase 6: optional symbol-map dump ----------
    if (cli.emitSymFile) {
        std::string symPath = cli.symFile.value_or("");
        if (symPath.empty()) {
            symPath = cli.output;
            auto slash = symPath.find_last_of("\\/");
            auto dot   = symPath.find_last_of('.');
            if (dot != std::string::npos &&
                (slash == std::string::npos || dot > slash)) {
                symPath.erase(dot);
            }
            symPath += ".sym";
        }

        mkdri::SymFileOptions so;
        so.verbose = cli.verbose;
        // Bias the symbol values to match the output's load layout.
        if (cli.relocatable) {
            // .REL: stored as if linked at 0.
            so.textBias = -int64_t(image.textVA);
            so.dataBias = -int64_t(image.dataVA) +
                          int64_t((image.text.size() + 1u) & ~1u);
            so.bssBias  = -int64_t(image.bssVA) +
                          int64_t((image.text.size() + 1u) & ~1u) +
                          int64_t((image.data.size() + 1u) & ~1u);
        } else if (image.hasRelocs) {
            // Phase 4: textBase / dataBase / bssBase chosen by writer.
            uint32_t tBase = cli.textBase.value_or(0x500u);
            uint32_t tSize = uint32_t((image.text.size() + 1u) & ~1u);
            uint32_t dSize = uint32_t((image.data.size() + 1u) & ~1u);
            uint32_t dBase = cli.dataBase.value_or(tBase + tSize);
            uint32_t bBase = cli.bssBase .value_or(dBase + dSize);
            so.textBias = int64_t(tBase) - int64_t(image.textVA);
            so.dataBias = int64_t(dBase) - int64_t(image.dataVA);
            so.bssBias  = int64_t(bBase) - int64_t(image.bssVA);
        }
        // else (pass-through): biases stay 0.

        std::string sdiag;
        auto sr = mkdri::writeSymFile(symPath, image, so, sdiag);
        if (sr != mkdri::ReadResult::Ok) {
            std::fprintf(stderr, "mkdri: %s\n", sdiag.c_str());
            return int(sr);
        }
        if (!cli.quiet) {
            std::fprintf(stdout, "mkdri: wrote symbol map '%s'\n",
                         symPath.c_str());
        }
    }

    if (!cli.quiet) {
        std::fprintf(stdout,
            "mkdri: '%s' -> '%s'  (text=%u, data=%u, bss=%u, entry=0x%X)\n",
            cli.input.c_str(), cli.output.c_str(),
            unsigned(image.text.size()),
            unsigned(image.data.size()),
            unsigned(image.bssSize),
            unsigned(image.entry));
    }
    return 0;
}
