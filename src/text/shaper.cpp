#include "iv/text/shaper.hpp"

// HarfBuzz is reached only here (ADR-0004); <hb.h> is a SYSTEM include from the
// vendored harfbuzz_core, so its own headers don't trip our -Werror set.
#include <hb.h>

#include <cmath>
#include <cstdio>

namespace iv::text {

namespace {

// HarfBuzz reports positions in (scale / upem) of font design units; we set the
// scale to pixelSize * 64 (26.6 fixed point of pixels), so pixels = value / 64.
constexpr float kPosToPixels = 1.0f / 64.0f;

} // namespace

struct Shaper::Impl {
    hb_blob_t* blob{nullptr};
    hb_face_t* face{nullptr};
    hb_font_t* font{nullptr};
    float pixelSize{0.0f};
    unsigned upem{0};

    ~Impl() {
        // Safe on nullptr (HarfBuzz destroy functions ignore null).
        hb_font_destroy(font);
        hb_face_destroy(face);
        hb_blob_destroy(blob);
    }
};

Shaper::Shaper(Shaper&&) noexcept = default;
Shaper& Shaper::operator=(Shaper&&) noexcept = default;
Shaper::~Shaper() = default;

Result<Shaper> Shaper::create(std::span<const std::byte> fontBytes, float pixelSizePx) {
    if (!(pixelSizePx > 0.0f)) {
        return make_error(Errc::invalid_argument, "Shaper: pixel size must be > 0");
    }
    if (fontBytes.empty()) {
        return make_error(Errc::invalid_argument, "Shaper: empty font data");
    }

    auto impl = std::make_unique<Impl>();
    impl->pixelSize = pixelSizePx;

    // Reference (not copy) the caller's bytes — documented to outlive the Shaper.
    impl->blob = hb_blob_create(reinterpret_cast<const char*>(fontBytes.data()),
                                static_cast<unsigned>(fontBytes.size()),
                                HB_MEMORY_MODE_READONLY, nullptr, nullptr);
    impl->face = hb_face_create(impl->blob, 0);
    // An unparseable blob yields HarfBuzz's empty face (zero glyphs).
    if (hb_face_get_glyph_count(impl->face) == 0) {
        return make_error(Errc::invalid_argument, "Shaper: font has no glyphs (parse failed?)");
    }
    impl->upem = hb_face_get_upem(impl->face);
    impl->font = hb_font_create(impl->face);

    const int scale = static_cast<int>(std::lround(pixelSizePx * 64.0f));
    hb_font_set_scale(impl->font, scale, scale);

    Shaper shaper;
    shaper.impl_ = std::move(impl);
    return shaper;
}

Result<Shaper> Shaper::createFromFile(const std::string& path, float pixelSizePx) {
    if (!(pixelSizePx > 0.0f)) {
        return make_error(Errc::invalid_argument, "Shaper: pixel size must be > 0");
    }
    // hb_blob_create_from_file_or_fail mmaps/reads the file and owns the bytes for
    // the blob's lifetime, so file-loaded fonts need no external byte buffer.
    hb_blob_t* blob = hb_blob_create_from_file_or_fail(path.c_str());
    if (blob == nullptr) {
        return make_error(Errc::invalid_argument, "Shaper: cannot read font file: " + path);
    }
    auto impl = std::make_unique<Impl>();
    impl->pixelSize = pixelSizePx;
    impl->blob = blob;
    impl->face = hb_face_create(impl->blob, 0);
    if (hb_face_get_glyph_count(impl->face) == 0) {
        return make_error(Errc::invalid_argument, "Shaper: font has no glyphs: " + path);
    }
    impl->upem = hb_face_get_upem(impl->face);
    impl->font = hb_font_create(impl->face);
    const int scale = static_cast<int>(std::lround(pixelSizePx * 64.0f));
    hb_font_set_scale(impl->font, scale, scale);

    Shaper shaper;
    shaper.impl_ = std::move(impl);
    return shaper;
}

std::vector<ShapedGlyph> Shaper::shape(std::string_view utf8) const {
    std::vector<ShapedGlyph> out;
    if (utf8.empty()) {
        return out;
    }

    hb_buffer_t* buf = hb_buffer_create();
    const int len = static_cast<int>(utf8.size());
    hb_buffer_add_utf8(buf, utf8.data(), len, 0u, len);
    // Auto-detect script, language, and direction from the content.
    hb_buffer_guess_segment_properties(buf);

    hb_shape(impl_->font, buf, nullptr, 0);

    unsigned count = 0;
    const hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buf, &count);
    const hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buf, &count);

    out.reserve(count);
    for (unsigned i = 0; i < count; ++i) {
        ShapedGlyph g;
        g.glyphId = infos[i].codepoint; // post-shaping: a glyph index, not a codepoint
        g.cluster = infos[i].cluster;
        g.xAdvance = static_cast<float>(positions[i].x_advance) * kPosToPixels;
        g.yAdvance = static_cast<float>(positions[i].y_advance) * kPosToPixels;
        g.xOffset = static_cast<float>(positions[i].x_offset) * kPosToPixels;
        g.yOffset = static_cast<float>(positions[i].y_offset) * kPosToPixels;
        out.push_back(g);
    }

    hb_buffer_destroy(buf);
    return out;
}

float Shaper::pixelSize() const noexcept { return impl_->pixelSize; }

std::uint32_t Shaper::unitsPerEm() const noexcept { return impl_->upem; }

} // namespace iv::text
