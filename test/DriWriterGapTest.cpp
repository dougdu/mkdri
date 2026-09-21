#include "DriWriter.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

uint32_t be32(const std::vector<uint8_t>& bytes, size_t offset)
{
    return (uint32_t(bytes[offset]) << 24) |
           (uint32_t(bytes[offset + 1]) << 16) |
           (uint32_t(bytes[offset + 2]) << 8) |
           uint32_t(bytes[offset + 3]);
}

} // namespace

int main()
{
    mkdri::Image image;
    image.entry = 0x500;
    image.textVA = 0x500;
    image.text.assign(10, 0x4E);
    image.dataVA = 0x50C;
    image.data = {0x12, 0x34, 0x56, 0x78};
    image.bssVA = 0x512;
    image.bssSize = 4;

    const auto output = std::filesystem::temp_directory_path() /
                        "mkdri-et-exec-gap-test.68K";
    mkdri::DriWriteOptions options;
    std::string diagnostic;
    const auto result = mkdri::writeDri(output.string(), image, options,
                                        diagnostic);
    if (result != mkdri::ReadResult::Ok) {
        std::cerr << diagnostic << '\n';
        return 1;
    }

    std::ifstream input(output, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
    std::remove(output.string().c_str());
    if (bytes.size() != 28 + 12 + 6) {
        std::cerr << "unexpected output size\n";
        return 1;
    }
    if (be32(bytes, 2) != 12 || be32(bytes, 6) != 6 ||
        be32(bytes, 10) != 4) {
        std::cerr << "header does not include segment gaps\n";
        return 1;
    }
    if (bytes[28 + 10] != 0 || bytes[28 + 11] != 0) {
        std::cerr << "text/data gap is not zero-filled\n";
        return 1;
    }
    const std::vector<uint8_t> expected = {0x12, 0x34, 0x56, 0x78};
    if (!std::equal(expected.begin(), expected.end(), bytes.begin() + 40)) {
        std::cerr << "data payload changed\n";
        return 1;
    }
    if (bytes[44] != 0 || bytes[45] != 0) {
        std::cerr << "data/bss gap is not zero-filled\n";
        return 1;
    }
    return 0;
}