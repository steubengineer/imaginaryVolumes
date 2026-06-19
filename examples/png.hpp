#ifndef IV_EXAMPLES_PNG_HPP
#define IV_EXAMPLES_PNG_HPP

// Minimal, dependency-free PNG writer: 8-bit RGBA (color type 6), using
// uncompressed DEFLATE "stored" blocks. In the project's own-the-boilerplate /
// minimal-deps spirit (no libpng / zlib / stb). Files are larger than
// zlib-compressed PNGs but are valid and open anywhere. Example/demo code only —
// not part of libiv.

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace iv_demo {

inline std::uint32_t crc32_of(const std::vector<std::uint8_t>& data) {
    static const auto table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            t[n] = c;
        }
        return t;
    }();
    std::uint32_t c = 0xFFFFFFFFu;
    for (std::uint8_t b : data) {
        c = table[(c ^ static_cast<std::uint32_t>(b)) & 0xFFu] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

inline std::uint32_t adler32_of(const std::vector<std::uint8_t>& data) {
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (std::uint8_t x : data) {
        a = (a + static_cast<std::uint32_t>(x)) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

inline void putBe32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    v.push_back(static_cast<std::uint8_t>(x >> 24));
    v.push_back(static_cast<std::uint8_t>(x >> 16));
    v.push_back(static_cast<std::uint8_t>(x >> 8));
    v.push_back(static_cast<std::uint8_t>(x));
}

inline void putLe16(std::vector<std::uint8_t>& v, std::uint16_t x) {
    v.push_back(static_cast<std::uint8_t>(x & 0xFFu));
    v.push_back(static_cast<std::uint8_t>(x >> 8));
}

inline void writeChunk(std::vector<std::uint8_t>& out, const char (&type)[5],
                       const std::vector<std::uint8_t>& data) {
    putBe32(out, static_cast<std::uint32_t>(data.size()));
    std::vector<std::uint8_t> typed{static_cast<std::uint8_t>(type[0]),
                                    static_cast<std::uint8_t>(type[1]),
                                    static_cast<std::uint8_t>(type[2]),
                                    static_cast<std::uint8_t>(type[3])};
    typed.insert(typed.end(), data.begin(), data.end());
    out.insert(out.end(), typed.begin(), typed.end());
    putBe32(out, crc32_of(typed));
}

// Write `rgba` (width*height*4 bytes, row-major, top-left origin) as a PNG.
inline bool writePng(const std::string& path, std::uint32_t width, std::uint32_t height,
                     const std::vector<std::uint8_t>& rgba) {
    if (rgba.size() != static_cast<std::size_t>(width) * height * 4u) {
        return false;
    }

    std::vector<std::uint8_t> png{0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

    std::vector<std::uint8_t> ihdr;
    putBe32(ihdr, width);
    putBe32(ihdr, height);
    ihdr.push_back(8);  // bit depth
    ihdr.push_back(6);  // color type RGBA
    ihdr.push_back(0);  // compression
    ihdr.push_back(0);  // filter
    ihdr.push_back(0);  // interlace
    writeChunk(png, "IHDR", ihdr);

    // Filtered scanlines: each row prefixed with filter byte 0 (None).
    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(height) * (1u + static_cast<std::size_t>(width) * 4u));
    for (std::uint32_t y = 0; y < height; ++y) {
        raw.push_back(0);
        const std::size_t off = static_cast<std::size_t>(y) * width * 4u;
        raw.insert(raw.end(), rgba.begin() + static_cast<std::ptrdiff_t>(off),
                   rgba.begin() + static_cast<std::ptrdiff_t>(off + static_cast<std::size_t>(width) * 4u));
    }

    // zlib stream: 2-byte header + stored DEFLATE blocks + Adler-32.
    std::vector<std::uint8_t> zlib{0x78, 0x01};
    std::size_t pos = 0;
    while (pos < raw.size()) {
        const std::size_t n = std::min<std::size_t>(65535u, raw.size() - pos);
        const bool last = (pos + n >= raw.size());
        zlib.push_back(last ? 1u : 0u); // BFINAL, BTYPE=00 (stored)
        const auto len16 = static_cast<std::uint16_t>(n);
        putLe16(zlib, len16);
        putLe16(zlib, static_cast<std::uint16_t>(~len16));
        zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos),
                    raw.begin() + static_cast<std::ptrdiff_t>(pos + n));
        pos += n;
    }
    putBe32(zlib, adler32_of(raw));
    writeChunk(png, "IDAT", zlib);
    writeChunk(png, "IEND", {});

    std::ofstream f(path, std::ios::binary);
    if (!f) {
        return false;
    }
    f.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    return static_cast<bool>(f);
}

} // namespace iv_demo

#endif // IV_EXAMPLES_PNG_HPP
