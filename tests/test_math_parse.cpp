// LaTeX-subset math parser + label splitter (ADR-0033 §1–2). Pure host logic (no GPU/font),
// so these are fast structural teeth on the parse tree. They pin: the $…$ text/math split (and
// \$ escape / unmatched-$ fallback); atom classification (letters italic, digits/operators
// upright); scripts; \frac argument order; \sqrt[n]; accents; \left…\right + bra–ket; the macro
// table; \mathrm/\mathbf alphabet override; and the unknown-command literal fallback (+diagnostic,
// never dropped/thrown). The layout stage (ADR-0033 §3) is tested separately.

#include "iv/text/math.hpp"

#include "catch_amalgamated.hpp"

#include <string>
#include <variant>
#include <vector>

using namespace iv::text::math;

namespace {
const Symbol* sym(const Node& n) { return std::get_if<Symbol>(&n.v); }
const Fraction* frac(const Node& n) { return std::get_if<Fraction>(&n.v); }
const Radical* rad(const Node& n) { return std::get_if<Radical>(&n.v); }
const Accent* acc(const Node& n) { return std::get_if<Accent>(&n.v); }
const Delimited* delim(const Node& n) { return std::get_if<Delimited>(&n.v); }
const Scripted* scr(const Node& n) { return std::get_if<Scripted>(&n.v); }
} // namespace

TEST_CASE("splitLabel separates text and $…$ math spans (ADR-0033)", "[math]") {
    auto a = splitLabel("Wave $f$");
    REQUIRE(a.size() == 2);
    CHECK_FALSE(a[0].math);
    CHECK(a[0].text == "Wave ");
    CHECK(a[1].math);
    CHECK(a[1].text == "f");

    // \$ is a literal dollar in text mode (no math span forms).
    // teeth: if \$ were not handled, this splits into text "a\" + math "b…".
    auto b = splitLabel("a\\$b");
    REQUIRE(b.size() == 1);
    CHECK_FALSE(b[0].math);
    CHECK(b[0].text == "a$b");

    auto c = splitLabel("$f$ x $g$");
    REQUIRE(c.size() == 3);
    CHECK((c[0].math && c[0].text == "f"));
    CHECK((!c[1].math && c[1].text == " x "));
    CHECK((c[2].math && c[2].text == "g"));

    auto d = splitLabel("plain text");
    REQUIRE(d.size() == 1);
    CHECK_FALSE(d[0].math);

    // Unmatched '$' is non-fatal: trailing run becomes literal text + a diagnostic.
    std::vector<std::string> diags;
    auto e = splitLabel("E $= mc^2", &diags);
    REQUIRE(e.size() == 2);
    CHECK((!e[0].math && e[0].text == "E "));
    CHECK((!e[1].math && e[1].text == "$= mc^2"));
    CHECK_FALSE(diags.empty());
}

TEST_CASE("parse: atoms — letters italic, digits/operators upright (ADR-0033)", "[math]") {
    auto l = parse("x+2");
    REQUIRE(l.size() == 3);
    REQUIRE(sym(l[0]));
    CHECK(sym(l[0])->cp == U'x');
    CHECK(sym(l[0])->alphabet == Alphabet::MathItalic); // a variable
    REQUIRE(sym(l[1]));
    CHECK(sym(l[1])->cls == AtomClass::Bin);
    CHECK(sym(l[1])->alphabet == Alphabet::Upright);
    REQUIRE(sym(l[2]));
    CHECK(sym(l[2])->cp == U'2');
    CHECK(sym(l[2])->alphabet == Alphabet::Upright); // a digit
}

TEST_CASE("parse: scripts attach to the preceding nucleus (ADR-0033)", "[math]") {
    auto l = parse("a_i^2");
    REQUIRE(l.size() == 1);
    const Scripted* s = scr(l[0]);
    REQUIRE(s);
    REQUIRE(s->nucleus.size() == 1);
    CHECK(sym(s->nucleus[0])->cp == U'a');
    REQUIRE(s->hasSup);
    REQUIRE(s->hasSub);
    // teeth: '^' fills sup, '_' fills sub — a swap would put '2' in sub and 'i' in sup.
    CHECK(sym(s->sup[0])->cp == U'2');
    CHECK(sym(s->sub[0])->cp == U'i');

    // A braced group is one nucleus for the script.
    auto g = parse("{ab}^n");
    REQUIRE(g.size() == 1);
    REQUIRE(scr(g[0]));
    CHECK(scr(g[0])->nucleus.size() == 2); // a, b
}

TEST_CASE("parse: \\frac keeps numerator/denominator order (ADR-0033)", "[math]") {
    auto l = parse("\\frac{a}{b}");
    REQUIRE(l.size() == 1);
    const Fraction* f = frac(l[0]);
    REQUIRE(f);
    REQUIRE(f->num.size() == 1);
    REQUIRE(f->den.size() == 1);
    // teeth: a swapped parser yields num='b', den='a'.
    CHECK(sym(f->num[0])->cp == U'a');
    CHECK(sym(f->den[0])->cp == U'b');
}

TEST_CASE("parse: \\sqrt and \\sqrt[n] (ADR-0033)", "[math]") {
    auto a = parse("\\sqrt{x}");
    REQUIRE(a.size() == 1);
    REQUIRE(rad(a[0]));
    CHECK_FALSE(rad(a[0])->hasIndex);
    CHECK(sym(rad(a[0])->radicand[0])->cp == U'x');

    auto b = parse("\\sqrt[3]{x}");
    REQUIRE(b.size() == 1);
    REQUIRE(rad(b[0]));
    REQUIRE(rad(b[0])->hasIndex);
    CHECK(sym(rad(b[0])->index[0])->cp == U'3');
    CHECK(sym(rad(b[0])->radicand[0])->cp == U'x');
}

TEST_CASE("parse: accents \\hat/\\dot and \\overline (ADR-0033)", "[math]") {
    auto h = parse("\\hat{x}");
    REQUIRE(h.size() == 1);
    REQUIRE(acc(h[0]));
    CHECK_FALSE(acc(h[0])->overbar);
    CHECK(acc(h[0])->mark == 0x0302); // combining circumflex
    CHECK(sym(acc(h[0])->base[0])->cp == U'x');

    auto o = parse("\\overline{z}");
    REQUIRE(o.size() == 1);
    REQUIRE(acc(o[0]));
    CHECK(acc(o[0])->overbar);
}

TEST_CASE("parse: \\left…\\right and bra–ket (ADR-0033)", "[math]") {
    auto p = parse("\\left(\\frac a b\\right)");
    REQUIRE(p.size() == 1);
    const Delimited* d = delim(p[0]);
    REQUIRE(d);
    CHECK(d->left == U'(');
    CHECK(d->right == U')');
    REQUIRE(d->body.size() == 1);
    CHECK(frac(d->body[0]));

    auto k = parse("\\ket{\\psi}");
    REQUIRE(k.size() == 1);
    REQUIRE(delim(k[0]));
    CHECK(delim(k[0])->left == 0x007C);  // |
    CHECK(delim(k[0])->right == 0x27E9); // ⟩
    CHECK(sym(delim(k[0])->body[0])->cp == 0x03C8); // ψ

    auto br = parse("\\braket{\\phi|\\psi}");
    REQUIRE(br.size() == 1);
    REQUIRE(delim(br[0]));
    CHECK(delim(br[0])->left == 0x27E8);  // ⟨
    CHECK(delim(br[0])->right == 0x27E9); // ⟩
}

TEST_CASE("parse: macro table + \\mathrm override (ADR-0033)", "[math]") {
    auto a = parse("\\alpha");
    REQUIRE(a.size() == 1);
    REQUIRE(sym(a[0]));
    CHECK(sym(a[0])->cp == 0x03B1);
    CHECK(sym(a[0])->alphabet == Alphabet::MathSymbol);

    // The full uppercase Greek alphabet, incl. the Latin-lookalikes stock LaTeX omits.
    CHECK(sym(parse("\\Alpha")[0])->cp == 0x0391);
    CHECK(sym(parse("\\Omicron")[0])->cp == 0x039F);
    CHECK(sym(parse("\\Chi")[0])->cp == 0x03A7);
    CHECK(sym(parse("\\Rho")[0])->cp == 0x03A1);
    CHECK(sym(parse("\\Omega")[0])->cp == 0x03A9);
    // Tensor product (\otimes binary) + the n-fold \bigotimes (a large operator).
    CHECK(sym(parse("\\otimes")[0])->cp == 0x2297);
    CHECK(sym(parse("\\bigotimes")[0])->cp == 0x2A02);
    CHECK(sym(parse("\\bigotimes")[0])->cls == AtomClass::Op);
    CHECK(sym(parse("\\nabla")[0])->cp == 0x2207);
    CHECK(sym(parse("\\partial")[0])->cp == 0x2202);

    // A bare letter is math-italic; \mathrm makes it upright (operator name, differential d).
    CHECK(sym(parse("d")[0])->alphabet == Alphabet::MathItalic);
    auto r = parse("\\mathrm{d}");
    REQUIRE(r.size() == 1);
    REQUIRE(sym(r[0]));
    CHECK(sym(r[0])->cp == U'd');
    CHECK(sym(r[0])->alphabet == Alphabet::Upright); // teeth: without override -> MathItalic

    auto bf = parse("\\mathbf{v}");
    REQUIRE(sym(bf[0]));
    CHECK(sym(bf[0])->alphabet == Alphabet::Bold);
}

TEST_CASE("parse: unknown command falls back to literal + diagnostic (ADR-0033)", "[math]") {
    std::vector<std::string> diags;
    auto l = parse("\\foo", &diags);
    // Rendered literally as \,f,o,o (upright) — never silently dropped, never thrown.
    REQUIRE(l.size() == 4);
    CHECK(sym(l[0])->cp == U'\\');
    CHECK(sym(l[1])->cp == U'f');
    CHECK(sym(l[2])->cp == U'o');
    CHECK(sym(l[3])->cp == U'o');
    CHECK_FALSE(diags.empty()); // the concern is recorded
}
