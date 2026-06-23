#include "iv/text/math_layout.hpp"

#include <algorithm>
#include <cstdint>
#include <variant>
#include <vector>

// OpenType-MATH box layout (ADR-0033 §3). Builds a tree of boxes whose leaves are positioned
// glyphs + rules, taking every vertical metric from the math face's MATH table via the Shaper
// (no hardcoded TeX constants). Box-local coordinates are baseline-relative with +y UP; the
// public layout() flips to the top-left-origin framebuffer at emit. Stage-2 scope: symbols +
// horizontal lists (with inter-atom spacing), fractions, and super/subscripts, plus functional
// (non-stretch) delimiters; radicals/accents are refined in stage 3.

namespace iv::text::math {

namespace {

using MC = Shaper::MathConstant;

enum class Style : std::uint8_t { Display, Text, Script, ScriptScript };

// One glyph placed at a baseline origin (x, y) in box-local px (+y up), drawn at sizePx.
struct GlyphInst {
    Face face;
    std::uint32_t glyphId;
    float x;
    float y;
    float sizePx;
};
// A filled rule (fraction bar / vinculum): box-local rect [x0,x1] x [y0,y1] px, +y up (y1>y0).
struct RuleInst {
    float x0;
    float y0;
    float x1;
    float y1;
};

struct Box {
    float width{0.0f};
    float ascent{0.0f};
    float descent{0.0f};
    float italicCorr{0.0f};       // of the last atom (for scripts)
    AtomClass cls{AtomClass::Ord}; // for inter-atom spacing
    std::vector<GlyphInst> glyphs;
    std::vector<RuleInst> rules;
};

void translate(Box& b, float dx, float dy) {
    for (auto& g : b.glyphs) {
        g.x += dx;
        g.y += dy;
    }
    for (auto& r : b.rules) {
        r.x0 += dx;
        r.x1 += dx;
        r.y0 += dy;
        r.y1 += dy;
    }
}

void appendBox(Box& into, const Box& b) {
    into.glyphs.insert(into.glyphs.end(), b.glyphs.begin(), b.glyphs.end());
    into.rules.insert(into.rules.end(), b.rules.begin(), b.rules.end());
}

bool isAsciiLetter(char32_t cp) {
    return (cp >= U'A' && cp <= U'Z') || (cp >= U'a' && cp <= U'z');
}

// Map an ASCII letter/digit to the Mathematical Bold alphanumeric block (\mathbf).
char32_t toBoldMath(char32_t cp) {
    if (cp >= U'A' && cp <= U'Z') {
        return 0x1D400 + (cp - U'A');
    }
    if (cp >= U'a' && cp <= U'z') {
        return 0x1D41A + (cp - U'a');
    }
    if (cp >= U'0' && cp <= U'9') {
        return 0x1D7CE + (cp - U'0');
    }
    return cp;
}

float sizeFor(Style st, float base, FontSet& fonts) {
    switch (st) {
    case Style::Display:
    case Style::Text:
        return base;
    case Style::Script:
        return base * fonts.shaper(Face::Math).scriptScaleDown(false);
    case Style::ScriptScript:
        return base * fonts.shaper(Face::Math).scriptScaleDown(true);
    }
    return base;
}

Style smaller(Style st) {
    switch (st) {
    case Style::Display:
    case Style::Text:
        return Style::Script;
    case Style::Script:
    case Style::ScriptScript:
        return Style::ScriptScript;
    }
    return Style::Script;
}

Style fracChildStyle(Style st) {
    switch (st) {
    case Style::Display:
        return Style::Text;
    case Style::Text:
        return Style::Script;
    case Style::Script:
    case Style::ScriptScript:
        return Style::ScriptScript;
    }
    return Style::Script;
}

// Inter-atom spacing (TeXbook, simplified) in math units (1mu = 1/18 em); suppressed in script
// styles. Returns 0 unless a Bin/Rel/Op/Punct is involved.
int interAtomMu(AtomClass l, AtomClass r, Style style) {
    if (style == Style::Script || style == Style::ScriptScript) {
        return 0;
    }
    if (l == AtomClass::Rel || r == AtomClass::Rel) {
        return 5; // thick
    }
    if (l == AtomClass::Bin || r == AtomClass::Bin) {
        return 4; // medium
    }
    if (l == AtomClass::Op || r == AtomClass::Op) {
        return 3; // thin
    }
    if (l == AtomClass::Punct) {
        return 3;
    }
    return 0;
}

struct Resolved {
    Face face;
    std::uint32_t glyphId;
};

// Resolve (face, cp) to a glyph, falling back to the roman face when the chosen face lacks it.
Resolved resolveGlyph(FontSet& fonts, Face face, char32_t cp) {
    const std::uint32_t g = fonts.shaper(face).glyphForCodepoint(cp);
    if (g == 0 && face != Face::Roman) {
        if (const std::uint32_t rg = fonts.shaper(Face::Roman).glyphForCodepoint(cp); rg != 0) {
            return {Face::Roman, rg};
        }
    }
    return {face, g};
}

// A one-glyph box from a resolved (face, glyphId) at sizePx.
Box glyphBox(FontSet& fonts, Face face, std::uint32_t glyphId, float sizePx) {
    Shaper& sh = fonts.shaper(face);
    const float scale = sizePx / static_cast<float>(sh.unitsPerEm());
    Box b;
    b.glyphs.push_back({face, glyphId, 0.0f, 0.0f, sizePx});
    b.width = sh.glyphAdvance(glyphId) * scale;
    const EncodedGlyph& e = sh.encodeGlyph(glyphId);
    if (!e.blank) {
        b.ascent = std::max(0.0f, e.extents.maxY * scale);
        b.descent = std::max(0.0f, -e.extents.minY * scale);
        const float ic = sh.hasMathTable() ? sh.mathItalicCorrection(glyphId) * scale : 0.0f;
        const float overhang = std::max(0.0f, e.extents.maxX * scale - b.width);
        b.italicCorr = std::max(ic, overhang);
    }
    return b;
}

// Forward decls (mutual recursion).
Box layoutList(FontSet& fonts, const List& list, Style style, float basePx);
Box layoutNode(FontSet& fonts, const Node& node, Style style, float basePx);

Box layoutSymbol(FontSet& fonts, const Symbol& s, Style style, float basePx) {
    const float sizePx = sizeFor(style, basePx, fonts);
    Face face = Face::Math;
    char32_t cp = s.cp;
    switch (s.alphabet) {
    case Alphabet::MathItalic:
        face = isAsciiLetter(cp) ? Face::Italic : Face::Math;
        break;
    case Alphabet::Upright:
        face = Face::Roman;
        break;
    case Alphabet::Bold:
        face = Face::Math;
        cp = toBoldMath(cp);
        break;
    case Alphabet::MathSymbol:
        face = Face::Math;
        break;
    }
    const Resolved r = resolveGlyph(fonts, face, cp);
    Box b = glyphBox(fonts, r.face, r.glyphId, sizePx);
    b.cls = s.cls;
    return b;
}

Box layoutFraction(FontSet& fonts, const Fraction& f, Style style, float basePx) {
    const float sizePx = sizeFor(style, basePx, fonts);
    const Style cs = fracChildStyle(style);
    Box num = layoutList(fonts, f.num, cs, basePx);
    Box den = layoutList(fonts, f.den, cs, basePx);

    Shaper& m = fonts.shaper(Face::Math);
    const float u2px = sizePx / static_cast<float>(m.unitsPerEm());
    const float axis = m.mathConstant(MC::axisHeight) * u2px;
    const float t = m.mathConstant(MC::fractionRuleThickness) * u2px;
    const float numGap = m.mathConstant(MC::fractionNumeratorGapMin) * u2px;
    const float denGap = m.mathConstant(MC::fractionDenominatorGapMin) * u2px;
    const float numShift = m.mathConstant(MC::fractionNumeratorShiftUp) * u2px;
    const float denShift = m.mathConstant(MC::fractionDenominatorShiftDown) * u2px;

    // Numerator high enough that its bottom clears the rule by numGap; symmetric for denom.
    const float up = std::max(numShift, axis + t * 0.5f + numGap + num.descent);
    const float down = std::max(denShift, -axis + t * 0.5f + denGap + den.ascent);
    const float width = std::max(num.width, den.width);

    Box b;
    b.width = width;
    b.cls = AtomClass::Inner;
    translate(num, (width - num.width) * 0.5f, up);
    translate(den, (width - den.width) * 0.5f, -down);
    appendBox(b, num);
    appendBox(b, den);
    b.rules.push_back({0.0f, axis - t * 0.5f, width, axis + t * 0.5f});
    b.ascent = up + num.ascent;
    b.descent = down + den.descent;
    return b;
}

Box layoutScripted(FontSet& fonts, const Scripted& sc, Style style, float basePx) {
    Box nuc = layoutList(fonts, sc.nucleus, style, basePx);
    const float sizePx = sizeFor(style, basePx, fonts);
    Shaper& m = fonts.shaper(Face::Math);
    const float u2px = sizePx / static_cast<float>(m.unitsPerEm());
    const Style ss = smaller(style);

    Box b = nuc;
    b.cls = nuc.cls;
    const float supX = nuc.width + nuc.italicCorr;
    const float subX = nuc.width;
    float up = 0.0f;
    float down = 0.0f;
    Box sup;
    Box sub;

    if (sc.hasSup) {
        sup = layoutList(fonts, sc.sup, ss, basePx);
        const float def = m.mathConstant(MC::superscriptShiftUp) * u2px;
        const float dropMax = m.mathConstant(MC::superscriptBaselineDropMax) * u2px;
        const float botMin = m.mathConstant(MC::superscriptBottomMin) * u2px;
        up = std::max({def, nuc.ascent - dropMax, botMin + sup.descent});
    }
    if (sc.hasSub) {
        sub = layoutList(fonts, sc.sub, ss, basePx);
        const float def = m.mathConstant(MC::subscriptShiftDown) * u2px;
        const float dropMin = m.mathConstant(MC::subscriptBaselineDropMin) * u2px;
        const float topMax = m.mathConstant(MC::subscriptTopMax) * u2px;
        down = std::max({def, nuc.descent + dropMin, sub.ascent - topMax});
    }
    if (sc.hasSup && sc.hasSub) {
        const float gapMin = m.mathConstant(MC::subSuperscriptGapMin) * u2px;
        const float gap = (up - sup.descent) - (sub.ascent - down);
        if (gap < gapMin) {
            down += gapMin - gap;
        }
    }

    float width = nuc.width;
    if (sc.hasSup) {
        translate(sup, supX, up);
        appendBox(b, sup);
        width = std::max(width, supX + sup.width);
        b.ascent = std::max(b.ascent, up + sup.ascent);
    }
    if (sc.hasSub) {
        translate(sub, subX, -down);
        appendBox(b, sub);
        width = std::max(width, subX + sub.width);
        b.descent = std::max(b.descent, down + sub.descent);
    }
    b.width = width;
    b.italicCorr = 0.0f;
    return b;
}

// Stretchy delimiters (ADR-0033 §3): each delimiter glyph is replaced by the MATH size variant
// tall enough to cover the body, then centered on the math axis. The body stays on its baseline.
Box layoutDelimited(FontSet& fonts, const Delimited& d, Style style, float basePx) {
    const float sizePx = sizeFor(style, basePx, fonts);
    Box body = layoutList(fonts, d.body, style, basePx);
    Shaper& m = fonts.shaper(Face::Math);
    const float u2px = sizePx / static_cast<float>(m.unitsPerEm());
    const float axis = m.mathConstant(MC::axisHeight) * u2px;

    // Variant height needed to span the body symmetrically about the axis.
    const float halfReq = std::max(body.ascent - axis, body.descent + axis);
    const float targetFU = 2.0f * halfReq / u2px;

    auto delim = [&](char32_t cp) -> Box {
        const Resolved base = resolveGlyph(fonts, Face::Math, cp);
        const std::uint32_t vg = m.glyphVariant(base.glyphId, true, targetFU);
        Box gb = glyphBox(fonts, Face::Math, vg, sizePx);
        const float center = 0.5f * (gb.ascent - gb.descent); // ink midpoint, +y up
        const float dy = axis - center;                       // center the delimiter on the axis
        translate(gb, 0.0f, dy);
        gb.ascent += dy;
        gb.descent -= dy;
        return gb;
    };

    Box out;
    out.cls = AtomClass::Inner;
    float x = 0.0f;
    if (d.left != 0) {
        Box l = delim(d.left);
        translate(l, x, 0.0f);
        appendBox(out, l);
        x += l.width;
        out.ascent = std::max(out.ascent, l.ascent);
        out.descent = std::max(out.descent, l.descent);
    }
    translate(body, x, 0.0f);
    appendBox(out, body);
    x += body.width;
    out.ascent = std::max(out.ascent, body.ascent);
    out.descent = std::max(out.descent, body.descent);
    if (d.right != 0) {
        Box rb = delim(d.right);
        translate(rb, x, 0.0f);
        appendBox(out, rb);
        x += rb.width;
        out.ascent = std::max(out.ascent, rb.ascent);
        out.descent = std::max(out.descent, rb.descent);
    }
    out.width = x;
    return out;
}

// Radical (ADR-0033 §3): a surd variant tall enough for the radicand, a vinculum rule over the
// radicand (radical gap + rule thickness from the font), and an optional degree up-left.
Box layoutRadical(FontSet& fonts, const Radical& r, Style style, float basePx) {
    const float sizePx = sizeFor(style, basePx, fonts);
    Box rc = layoutList(fonts, r.radicand, style, basePx);
    Shaper& m = fonts.shaper(Face::Math);
    const float u2px = sizePx / static_cast<float>(m.unitsPerEm());
    const float ruleT = m.mathConstant(MC::radicalRuleThickness) * u2px;
    const float gap = m.mathConstant(MC::radicalVerticalGap) * u2px;
    const float extra = m.mathConstant(MC::radicalExtraAscender) * u2px;

    const float targetFU = (rc.ascent + rc.descent + gap + ruleT) / u2px;
    const std::uint32_t surd0 = m.glyphForCodepoint(0x221A);
    const std::uint32_t surdG = m.glyphVariant(surd0, true, targetFU);
    Box surd = glyphBox(fonts, Face::Math, surdG, sizePx);

    const float ruleY = rc.ascent + gap;                 // bottom of the vinculum (+y up)
    const float dy = (ruleY + ruleT) - surd.ascent;      // lift the surd so its top meets the rule
    translate(surd, 0.0f, dy);
    const float surdAscent = surd.ascent + dy;
    const float surdDescent = surd.descent - dy;

    Box out;
    out.cls = AtomClass::Ord;
    appendBox(out, surd);
    const float bodyX = surd.width;
    translate(rc, bodyX, 0.0f);
    appendBox(out, rc);
    out.rules.push_back({bodyX, ruleY, bodyX + rc.width, ruleY + ruleT});
    out.width = bodyX + rc.width;
    out.ascent = std::max(ruleY + ruleT + extra, surdAscent);
    out.descent = std::max(rc.descent, surdDescent);

    if (r.hasIndex && !r.index.empty()) {
        Box idx = layoutList(fonts, r.index, Style::ScriptScript, basePx);
        const float raise =
            m.mathConstant(MC::radicalDegreeBottomRaisePercent) / 100.0f * (surdAscent + surdDescent);
        const float shift = idx.width; // make room at the left for the degree
        translate(out, shift, 0.0f);
        translate(idx, 0.0f, -surdDescent + raise);
        appendBox(out, idx);
        out.width += shift;
        out.ascent = std::max(out.ascent, -surdDescent + raise + idx.ascent);
    }
    return out;
}

// Accents (ADR-0033 §3): \overline draws a rule over the base; \hat/\dot place the accent glyph
// centered over the base ink (top-accent attachment), raised just above the base.
Box layoutAccent(FontSet& fonts, const Accent& a, Style style, float basePx) {
    const float sizePx = sizeFor(style, basePx, fonts);
    Box base = layoutList(fonts, a.base, style, basePx);
    Shaper& m = fonts.shaper(Face::Math);
    const float u2px = sizePx / static_cast<float>(m.unitsPerEm());
    base.cls = AtomClass::Ord;

    if (a.overbar) {
        const float gap = m.mathConstant(MC::overbarVerticalGap) * u2px;
        const float t = m.mathConstant(MC::overbarRuleThickness) * u2px;
        const float extra = m.mathConstant(MC::overbarExtraAscender) * u2px;
        const float ruleY = base.ascent + gap;
        base.rules.push_back({0.0f, ruleY, base.width, ruleY + t});
        base.ascent = ruleY + t + extra;
        return base;
    }

    const Resolved mark = resolveGlyph(fonts, Face::Math, a.mark);
    if (mark.glyphId == 0) {
        return base; // no accent glyph available
    }
    Box mk = glyphBox(fonts, Face::Math, mark.glyphId, sizePx);
    // Combining accent glyphs carry offset, often zero-advance ink, so position by the ink box:
    // center the accent ink over the base center, with its ink bottom just above the base top.
    const EncodedGlyph& e = m.encodeGlyph(mark.glyphId);
    const float inkMinX = e.extents.minX * u2px;
    const float inkMaxX = e.extents.maxX * u2px;
    const float inkMinY = e.extents.minY * u2px;
    const float inkMaxY = e.extents.maxY * u2px;
    const float gap = 0.05f * sizePx;
    const float markX = base.width * 0.5f - 0.5f * (inkMinX + inkMaxX); // ink center over base
    const float markBaseline = base.ascent + gap - inkMinY;            // ink bottom above base top
    translate(mk, markX, markBaseline);
    appendBox(base, mk);
    base.ascent = std::max(base.ascent, markBaseline + inkMaxY);
    return base;
}

Box layoutNode(FontSet& fonts, const Node& node, Style style, float basePx) {
    if (const auto* s = std::get_if<Symbol>(&node.v)) {
        return layoutSymbol(fonts, *s, style, basePx);
    }
    if (const auto* f = std::get_if<Fraction>(&node.v)) {
        return layoutFraction(fonts, *f, style, basePx);
    }
    if (const auto* sc = std::get_if<Scripted>(&node.v)) {
        return layoutScripted(fonts, *sc, style, basePx);
    }
    if (const auto* d = std::get_if<Delimited>(&node.v)) {
        return layoutDelimited(fonts, *d, style, basePx);
    }
    if (const auto* r = std::get_if<Radical>(&node.v)) {
        return layoutRadical(fonts, *r, style, basePx);
    }
    if (const auto* ac = std::get_if<Accent>(&node.v)) {
        return layoutAccent(fonts, *ac, style, basePx);
    }
    return {}; // Space handled in layoutList
}

Box layoutList(FontSet& fonts, const List& list, Style style, float basePx) {
    Box out;
    const float sizePx = sizeFor(style, basePx, fonts);
    float x = 0.0f;
    AtomClass prev = AtomClass::Ord;
    bool havePrev = false;
    for (const Node& n : list) {
        if (const auto* sp = std::get_if<Space>(&n.v)) {
            x += sp->em * sizePx;
            havePrev = false;
            continue;
        }
        Box b = layoutNode(fonts, n, style, basePx);
        if (havePrev) {
            x += static_cast<float>(interAtomMu(prev, b.cls, style)) / 18.0f * sizePx;
        }
        translate(b, x, 0.0f);
        appendBox(out, b);
        x += b.width;
        out.ascent = std::max(out.ascent, b.ascent);
        out.descent = std::max(out.descent, b.descent);
        out.italicCorr = b.italicCorr; // the last atom's, for a trailing script
        prev = b.cls;
        havePrev = true;
    }
    out.width = x;
    out.cls = AtomClass::Ord;
    return out;
}

// Append a filled rect (top-left-origin px) as two screen-space triangles in clip space (NDC,
// y-down) to the overlay (ADR-0028 screen channel).
void pushRect(iv::vk::Overlay& ov, float x0, float yTop, float x1, float yBot, float halfW,
              float halfH, const std::array<float, 4>& color) {
    const auto v = [&](float px, float py) {
        iv::vk::OverlayVertex o;
        o.pos = {px / halfW - 1.0f, py / halfH - 1.0f, 0.0f};
        o.color = color;
        return o;
    };
    const iv::vk::OverlayVertex tl = v(x0, yTop);
    const iv::vk::OverlayVertex tr = v(x1, yTop);
    const iv::vk::OverlayVertex br = v(x1, yBot);
    const iv::vk::OverlayVertex bl = v(x0, yBot);
    ov.screenTriangles.push_back(tl);
    ov.screenTriangles.push_back(tr);
    ov.screenTriangles.push_back(br);
    ov.screenTriangles.push_back(tl);
    ov.screenTriangles.push_back(br);
    ov.screenTriangles.push_back(bl);
}

} // namespace

Metrics layout(MixedGlyphs& glyphs, iv::vk::Overlay& overlay, FontSet& fonts, const List& list,
               float penXpx, float penYpx, std::uint32_t fbWidth, std::uint32_t fbHeight,
               float basePixelSize, const std::array<float, 4>& color) {
    const Box b = layoutList(fonts, list, Style::Text, basePixelSize);
    const float halfW = static_cast<float>(fbWidth) * 0.5f;
    const float halfH = static_cast<float>(fbHeight) * 0.5f;
    for (const auto& g : b.glyphs) {
        // Box-local (+y up) baseline origin -> top-left-origin screen px.
        glyphs.appendGlyph(g.face, g.glyphId, penXpx + g.x, penYpx - g.y, g.sizePx, fbWidth,
                           fbHeight, color);
    }
    for (const auto& r : b.rules) {
        pushRect(overlay, penXpx + r.x0, penYpx - r.y1, penXpx + r.x1, penYpx - r.y0, halfW, halfH,
                 color);
    }
    return {b.width, b.ascent, b.descent};
}

float appendLabel(MixedGlyphs& glyphs, iv::vk::Overlay& overlay, std::string_view label,
                  float penXpx, float penYpx, std::uint32_t fbWidth, std::uint32_t fbHeight,
                  float pixelSize, const std::array<float, 4>& color) {
    float x = penXpx;
    for (const LabelSpan& sp : splitLabel(label)) {
        if (sp.math) {
            const List tree = parse(sp.text);
            const Metrics m = layout(glyphs, overlay, glyphs.fonts(), tree, x, penYpx, fbWidth,
                                     fbHeight, pixelSize, color);
            x += m.width;
        } else {
            x += glyphs.appendRun(Face::Roman, sp.text, x, penYpx, fbWidth, fbHeight,
                                  {color, pixelSize});
        }
    }
    return x - penXpx;
}

float appendLabelRotated(MixedGlyphs& glyphs, iv::vk::Overlay& overlay, std::string_view label,
                         float pivotXpx, float pivotYpx, std::uint32_t fbWidth,
                         std::uint32_t fbHeight, float pixelSize,
                         const std::array<float, 4>& color, float angleRad) {
    // Lay the label out horizontally at the pivot, then rotate the quads it added in pixel space
    // about that pivot (ADR-0034). The pivot is the rotated baseline origin.
    const MixedGlyphs::Marker before = glyphs.marker();
    const float advance =
        appendLabel(glyphs, overlay, label, pivotXpx, pivotYpx, fbWidth, fbHeight, pixelSize, color);
    glyphs.rotateSince(before, angleRad, pivotXpx, pivotYpx, fbWidth, fbHeight);
    return advance;
}

float measureLabel(FontSet& fonts, std::string_view label, float pixelSize) {
    // Dry layout into throwaway sinks (the framebuffer size only affects the discarded NDC
    // positions, not the px advance). Glyph encoding is cached in the shapers (harmless).
    MixedGlyphs scratch(fonts);
    iv::vk::Overlay sink;
    return appendLabel(scratch, sink, label, 0.0f, 0.0f, 1u, 1u, pixelSize, {1.0f, 1.0f, 1.0f, 1.0f});
}

} // namespace iv::text::math
