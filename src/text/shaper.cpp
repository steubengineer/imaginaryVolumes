#include "iv/text/shaper.hpp"

// HarfBuzz is reached only here (ADR-0004); <hb.h>/<hb-gpu.h> are SYSTEM includes
// from the vendored harfbuzz_core/_gpu, so their headers don't trip our -Werror.
#include <hb.h>
#include <hb-gpu.h>
#include <hb-ot.h> // hb_ot_math_* (ADR-0033; OpenType MATH table)

#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace iv::text {

namespace {

// HarfBuzz reports positions in (scale / upem) of font design units; we set the
// scale to pixelSize * 64 (26.6 fixed point of pixels), so pixels = value / 64.
constexpr float kPosToPixels = 1.0f / 64.0f;

} // namespace

struct Shaper::Impl {
    hb_blob_t* blob{nullptr};
    hb_face_t* face{nullptr};
    hb_font_t* font{nullptr};       // scaled to pixelSize (for shaping)
    hb_font_t* encodeFont{nullptr}; // at default upem scale (for glyph outlines)
    float pixelSize{0.0f};
    unsigned upem{0};

    // ADR-0023 Slug glyph atlas (lazily created encoder + packed RGBA16I texels).
    hb_gpu_draw_t* gpu{nullptr};
    std::vector<std::int16_t> atlas;
    std::unordered_map<std::uint32_t, EncodedGlyph> glyphCache;

    ~Impl() {
        // Safe on nullptr (HarfBuzz destroy functions ignore null).
        hb_gpu_draw_destroy(gpu);
        hb_font_destroy(encodeFont);
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
    // A second font at the face's default (upem) scale: glyph outlines are encoded
    // in font units for Slug (ADR-0023), independent of the pixel shaping scale.
    impl->encodeFont = hb_font_create(impl->face);

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
    // A second font at the face's default (upem) scale: glyph outlines are encoded
    // in font units for Slug (ADR-0023), independent of the pixel shaping scale.
    impl->encodeFont = hb_font_create(impl->face);

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

const EncodedGlyph& Shaper::encodeGlyph(std::uint32_t glyphId) {
    Impl& impl = *impl_;
    if (auto it = impl.glyphCache.find(glyphId); it != impl.glyphCache.end()) {
        return it->second;
    }

    if (impl.gpu == nullptr) {
        impl.gpu = hb_gpu_draw_create_or_fail();
    }

    EncodedGlyph e;
    if (impl.gpu != nullptr) {
        // texel offset (RGBA16I = 4 int16 per texel) where this glyph's data starts
        e.atlasOffset = static_cast<std::uint32_t>(impl.atlas.size() / 4);

        hb_gpu_draw_reset(impl.gpu); // drop any previously accumulated outline
        hb_gpu_draw_glyph(impl.gpu, impl.encodeFont, glyphId);

        hb_glyph_extents_t ext{};
        hb_blob_t* blob = hb_gpu_draw_encode(impl.gpu, &ext);
        unsigned lenBytes = 0;
        const char* data = hb_blob_get_data(blob, &lenBytes);
        // A whitespace / outline-less glyph encodes to nothing -> stays blank.
        if (data != nullptr && lenBytes >= sizeof(std::int16_t)) {
            const auto* shorts = reinterpret_cast<const std::int16_t*>(data);
            const std::size_t n = lenBytes / sizeof(std::int16_t);
            impl.atlas.insert(impl.atlas.end(), shorts, shorts + n);
            e.blank = false;
            // hb_glyph_extents_t: x_bearing (left), y_bearing (top), width (>=0),
            // height (<=0, downward). Convert to a y-up min/max box (font units).
            e.extents.minX = static_cast<float>(ext.x_bearing);
            e.extents.maxX = static_cast<float>(ext.x_bearing + ext.width);
            e.extents.maxY = static_cast<float>(ext.y_bearing);
            e.extents.minY = static_cast<float>(ext.y_bearing + ext.height);
        }
        hb_blob_destroy(blob);
    }

    auto [it, inserted] = impl.glyphCache.emplace(glyphId, e);
    (void) inserted;
    return it->second;
}

std::span<const std::int16_t> Shaper::glyphAtlas() const noexcept {
    return std::span<const std::int16_t>(impl_->atlas.data(), impl_->atlas.size());
}

std::uint32_t Shaper::glyphForCodepoint(char32_t cp) const noexcept {
    hb_codepoint_t g = 0;
    // Nominal cmap mapping (no shaping); encodeFont is at the face default scale.
    hb_font_get_nominal_glyph(impl_->encodeFont, static_cast<hb_codepoint_t>(cp), &g);
    return g;
}

float Shaper::glyphAdvance(std::uint32_t glyphId) const noexcept {
    // encodeFont is at the upem (font-unit) scale, so the advance is already in font units.
    return static_cast<float>(
        hb_font_get_glyph_h_advance(impl_->encodeFont, static_cast<hb_codepoint_t>(glyphId)));
}

bool Shaper::hasMathTable() const noexcept {
    return hb_ot_math_has_data(impl_->face) != 0;
}

namespace {
hb_ot_math_constant_t toHbConstant(Shaper::MathConstant c) {
    using MC = Shaper::MathConstant;
    switch (c) {
    case MC::axisHeight:
        return HB_OT_MATH_CONSTANT_AXIS_HEIGHT;
    case MC::fractionRuleThickness:
        return HB_OT_MATH_CONSTANT_FRACTION_RULE_THICKNESS;
    case MC::fractionNumeratorShiftUp:
        return HB_OT_MATH_CONSTANT_FRACTION_NUMERATOR_SHIFT_UP;
    case MC::fractionDenominatorShiftDown:
        return HB_OT_MATH_CONSTANT_FRACTION_DENOMINATOR_SHIFT_DOWN;
    case MC::fractionNumeratorGapMin:
        return HB_OT_MATH_CONSTANT_FRACTION_NUMERATOR_GAP_MIN;
    case MC::fractionDenominatorGapMin:
        return HB_OT_MATH_CONSTANT_FRACTION_DENOMINATOR_GAP_MIN;
    case MC::superscriptShiftUp:
        return HB_OT_MATH_CONSTANT_SUPERSCRIPT_SHIFT_UP;
    case MC::subscriptShiftDown:
        return HB_OT_MATH_CONSTANT_SUBSCRIPT_SHIFT_DOWN;
    case MC::superscriptBottomMin:
        return HB_OT_MATH_CONSTANT_SUPERSCRIPT_BOTTOM_MIN;
    case MC::subscriptTopMax:
        return HB_OT_MATH_CONSTANT_SUBSCRIPT_TOP_MAX;
    case MC::subSuperscriptGapMin:
        return HB_OT_MATH_CONSTANT_SUB_SUPERSCRIPT_GAP_MIN;
    case MC::superscriptBaselineDropMax:
        return HB_OT_MATH_CONSTANT_SUPERSCRIPT_BASELINE_DROP_MAX;
    case MC::subscriptBaselineDropMin:
        return HB_OT_MATH_CONSTANT_SUBSCRIPT_BASELINE_DROP_MIN;
    case MC::radicalRuleThickness:
        return HB_OT_MATH_CONSTANT_RADICAL_RULE_THICKNESS;
    case MC::radicalVerticalGap:
        return HB_OT_MATH_CONSTANT_RADICAL_VERTICAL_GAP;
    case MC::radicalExtraAscender:
        return HB_OT_MATH_CONSTANT_RADICAL_EXTRA_ASCENDER;
    case MC::radicalKernBeforeDegree:
        return HB_OT_MATH_CONSTANT_RADICAL_KERN_BEFORE_DEGREE;
    case MC::radicalKernAfterDegree:
        return HB_OT_MATH_CONSTANT_RADICAL_KERN_AFTER_DEGREE;
    case MC::radicalDegreeBottomRaisePercent:
        return HB_OT_MATH_CONSTANT_RADICAL_DEGREE_BOTTOM_RAISE_PERCENT;
    case MC::accentBaseHeight:
        return HB_OT_MATH_CONSTANT_ACCENT_BASE_HEIGHT;
    case MC::overbarVerticalGap:
        return HB_OT_MATH_CONSTANT_OVERBAR_VERTICAL_GAP;
    case MC::overbarRuleThickness:
        return HB_OT_MATH_CONSTANT_OVERBAR_RULE_THICKNESS;
    case MC::overbarExtraAscender:
        return HB_OT_MATH_CONSTANT_OVERBAR_EXTRA_ASCENDER;
    }
    return HB_OT_MATH_CONSTANT_AXIS_HEIGHT;
}
} // namespace

float Shaper::mathConstant(MathConstant c) const noexcept {
    return static_cast<float>(hb_ot_math_get_constant(impl_->encodeFont, toHbConstant(c)));
}

float Shaper::scriptScaleDown(bool scriptScript) const noexcept {
    const auto which = scriptScript ? HB_OT_MATH_CONSTANT_SCRIPT_SCRIPT_PERCENT_SCALE_DOWN
                                    : HB_OT_MATH_CONSTANT_SCRIPT_PERCENT_SCALE_DOWN;
    const int pct = hb_ot_math_get_constant(impl_->encodeFont, which);
    if (pct > 0) {
        return static_cast<float>(pct) / 100.0f;
    }
    return scriptScript ? 0.5f : 0.7f; // CM defaults when the table is absent
}

float Shaper::mathItalicCorrection(std::uint32_t glyphId) const noexcept {
    return static_cast<float>(hb_ot_math_get_glyph_italics_correction(
        impl_->encodeFont, static_cast<hb_codepoint_t>(glyphId)));
}

} // namespace iv::text
