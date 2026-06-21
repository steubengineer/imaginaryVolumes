#ifndef IV_TEXT_MATH_HPP
#define IV_TEXT_MATH_HPP

// Inline-math model (ADR-0033): the parsed representation of a LaTeX-subset math expression
// (the content of a `$…$` island), plus the label splitter. This header is the math layer's
// internal-but-testable surface — it carries NO HarfBuzz type (ADR-0004); the OpenType-MATH
// layout that turns this tree into positioned glyphs lives in math_layout (it needs a Shaper
// + the MATH table). The parser is pure host logic over a controlled grammar; unknown control
// sequences and malformed input fall back to literal rendering with a recorded diagnostic and
// never throw (ADR-0003).

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace iv::text::math {

// Which alphabet a symbol is drawn from (selects the face + codepoint mapping at layout):
//   MathItalic — letters as math variables (the italic face)
//   Upright    — digits, operators, \mathrm (the roman face)
//   Bold       — \mathbf (the math face's bold alphabet)
//   MathSymbol — symbols / Greek / operators by codepoint (the math face)
enum class Alphabet : std::uint8_t { MathItalic, Upright, Bold, MathSymbol };

// TeX math class — drives inter-atom spacing (stage-2 layout) and script/limit behavior.
enum class AtomClass : std::uint8_t { Ord, Op, Bin, Rel, Open, Close, Punct, Inner };

struct Node;
using List = std::vector<Node>;

// A single glyph by Unicode codepoint, drawn from `alphabet`, with a math class.
struct Symbol {
    char32_t cp{};
    AtomClass cls{AtomClass::Ord};
    Alphabet alphabet{Alphabet::MathItalic};
};

// \frac{num}{den}
struct Fraction {
    List num;
    List den;
};

// \sqrt{radicand} or \sqrt[index]{radicand}
struct Radical {
    List index; // empty unless hasIndex
    List radicand;
    bool hasIndex{false};
};

// \hat{base} / \dot{base} (overbar=false, `mark` is the accent codepoint) and
// \overline{base} (overbar=true, `mark` unused — a rule spans the base).
struct Accent {
    char32_t mark{};
    List base;
    bool overbar{false};
};

// \left<l> body \right<r> (and the bra–ket expansions). A delimiter codepoint of 0 is the
// null delimiter (LaTeX `.`): nothing drawn on that side.
struct Delimited {
    char32_t left{};
    char32_t right{};
    List body;
};

// A nucleus with optional super/subscripts (x^2, a_i, x_i^2).
struct Scripted {
    List nucleus;
    List sup; // valid iff hasSup
    List sub; // valid iff hasSub
    bool hasSup{false};
    bool hasSub{false};
};

// Explicit horizontal space (\, \; \quad …), width in em.
struct Space {
    float em{};
};

struct Node {
    std::variant<Symbol, Fraction, Radical, Accent, Delimited, Scripted, Space> v;
};

// Parse a math-mode source string (the text between `$…$`) into a node list. Unknown control
// sequences and malformed input append a human-readable message to `diagnostics` (when
// non-null) and fall back to literal upright rendering of the offending source; the function
// never throws (ADR-0003). Whitespace in math mode is insignificant (it only separates tokens).
[[nodiscard]] List parse(std::string_view mathSrc, std::vector<std::string>* diagnostics = nullptr);

// One span of a label string: text (rendered roman) or math (rendered via the layout).
struct LabelSpan {
    bool math{false};
    std::string text; // for a text span: the literal text (escapes resolved); for math: the
                      // raw math source (between the `$`), to be parse()d.
};

// Split a label on unescaped `$` into alternating text/math spans (ADR-0033 §1). `\$` is a
// literal dollar in text mode. An odd number of `$` (an unmatched opener) is non-fatal: the
// trailing run is emitted as a TEXT span and a diagnostic is recorded. Adjacent/empty spans
// are dropped. Never throws.
[[nodiscard]] std::vector<LabelSpan> splitLabel(std::string_view label,
                                                std::vector<std::string>* diagnostics = nullptr);

} // namespace iv::text::math

#endif // IV_TEXT_MATH_HPP
