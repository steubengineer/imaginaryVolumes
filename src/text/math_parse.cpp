#include "iv/text/math.hpp"

#include <array>
#include <string>
#include <unordered_map>
#include <utility>

// LaTeX-subset math parser (ADR-0033 §1–2). Pure host logic: tokenize a math-mode string and
// recursively descend the controlled grammar into a math::List. No HarfBuzz (the layout stage
// owns that). Unknown control sequences / malformed input fall back to literal upright
// rendering with a recorded diagnostic and never throw (ADR-0003). The supported subset is
// exactly ADR-0033 §2; the macro table below is the explicit extension point.

namespace iv::text::math {

namespace {

// A symbol-macro maps a control word to a codepoint + math class, drawn from the math face.
struct SymInfo {
    char32_t cp;
    AtomClass cls;
};

// ADR-0033 §2 macro table (Greek, relations/operators/symbols, large operators). Curated;
// extend here. Not delimiters or constructs (\frac, \sqrt, \left, …) — those are handled
// structurally in handleMacro().
const std::unordered_map<std::string, SymInfo>& symbolTable() {
    static const std::unordered_map<std::string, SymInfo> t = {
        // Greek lowercase
        {"alpha", {0x03B1, AtomClass::Ord}},     {"beta", {0x03B2, AtomClass::Ord}},
        {"gamma", {0x03B3, AtomClass::Ord}},     {"delta", {0x03B4, AtomClass::Ord}},
        {"epsilon", {0x03F5, AtomClass::Ord}},   {"varepsilon", {0x03B5, AtomClass::Ord}},
        {"zeta", {0x03B6, AtomClass::Ord}},      {"eta", {0x03B7, AtomClass::Ord}},
        {"theta", {0x03B8, AtomClass::Ord}},     {"vartheta", {0x03D1, AtomClass::Ord}},
        {"iota", {0x03B9, AtomClass::Ord}},      {"kappa", {0x03BA, AtomClass::Ord}},
        {"lambda", {0x03BB, AtomClass::Ord}},    {"mu", {0x03BC, AtomClass::Ord}},
        {"nu", {0x03BD, AtomClass::Ord}},        {"xi", {0x03BE, AtomClass::Ord}},
        {"pi", {0x03C0, AtomClass::Ord}},        {"varpi", {0x03D6, AtomClass::Ord}},
        {"rho", {0x03C1, AtomClass::Ord}},       {"sigma", {0x03C3, AtomClass::Ord}},
        {"tau", {0x03C4, AtomClass::Ord}},       {"upsilon", {0x03C5, AtomClass::Ord}},
        {"phi", {0x03D5, AtomClass::Ord}},       {"varphi", {0x03C6, AtomClass::Ord}},
        {"chi", {0x03C7, AtomClass::Ord}},       {"psi", {0x03C8, AtomClass::Ord}},
        {"omega", {0x03C9, AtomClass::Ord}},
        // Greek uppercase (upright in TeX math)
        {"Gamma", {0x0393, AtomClass::Ord}},     {"Delta", {0x0394, AtomClass::Ord}},
        {"Theta", {0x0398, AtomClass::Ord}},     {"Lambda", {0x039B, AtomClass::Ord}},
        {"Xi", {0x039E, AtomClass::Ord}},        {"Pi", {0x03A0, AtomClass::Ord}},
        {"Sigma", {0x03A3, AtomClass::Ord}},     {"Upsilon", {0x03A5, AtomClass::Ord}},
        {"Phi", {0x03A6, AtomClass::Ord}},       {"Psi", {0x03A8, AtomClass::Ord}},
        {"Omega", {0x03A9, AtomClass::Ord}},
        // Binary operators
        {"times", {0x00D7, AtomClass::Bin}},     {"cdot", {0x22C5, AtomClass::Bin}},
        {"pm", {0x00B1, AtomClass::Bin}},        {"mp", {0x2213, AtomClass::Bin}},
        {"div", {0x00F7, AtomClass::Bin}},       {"ast", {0x2217, AtomClass::Bin}},
        {"star", {0x22C6, AtomClass::Bin}},      {"cup", {0x222A, AtomClass::Bin}},
        {"cap", {0x2229, AtomClass::Bin}},       {"otimes", {0x2297, AtomClass::Bin}},
        {"oplus", {0x2295, AtomClass::Bin}},
        // Relations
        {"leq", {0x2264, AtomClass::Rel}},       {"le", {0x2264, AtomClass::Rel}},
        {"geq", {0x2265, AtomClass::Rel}},       {"ge", {0x2265, AtomClass::Rel}},
        {"neq", {0x2260, AtomClass::Rel}},       {"ne", {0x2260, AtomClass::Rel}},
        {"approx", {0x2248, AtomClass::Rel}},    {"equiv", {0x2261, AtomClass::Rel}},
        {"sim", {0x223C, AtomClass::Rel}},       {"simeq", {0x2243, AtomClass::Rel}},
        {"propto", {0x221D, AtomClass::Rel}},    {"to", {0x2192, AtomClass::Rel}},
        {"rightarrow", {0x2192, AtomClass::Rel}}, {"leftarrow", {0x2190, AtomClass::Rel}},
        {"leftrightarrow", {0x2194, AtomClass::Rel}}, {"Rightarrow", {0x21D2, AtomClass::Rel}},
        {"Leftarrow", {0x21D0, AtomClass::Rel}}, {"mapsto", {0x21A6, AtomClass::Rel}},
        {"in", {0x2208, AtomClass::Rel}},        {"notin", {0x2209, AtomClass::Rel}},
        {"subset", {0x2282, AtomClass::Rel}},    {"supset", {0x2283, AtomClass::Rel}},
        {"subseteq", {0x2286, AtomClass::Rel}},  {"supseteq", {0x2287, AtomClass::Rel}},
        // Ordinary symbols
        {"infty", {0x221E, AtomClass::Ord}},     {"partial", {0x2202, AtomClass::Ord}},
        {"nabla", {0x2207, AtomClass::Ord}},     {"forall", {0x2200, AtomClass::Ord}},
        {"exists", {0x2203, AtomClass::Ord}},    {"hbar", {0x210F, AtomClass::Ord}},
        {"ell", {0x2113, AtomClass::Ord}},       {"Re", {0x211C, AtomClass::Ord}},
        {"Im", {0x2111, AtomClass::Ord}},        {"prime", {0x2032, AtomClass::Ord}},
        {"dagger", {0x2020, AtomClass::Ord}},    {"langle", {0x27E8, AtomClass::Open}},
        {"rangle", {0x27E9, AtomClass::Close}},  {"vert", {0x007C, AtomClass::Ord}},
        {"mid", {0x2223, AtomClass::Rel}},       {"Vert", {0x2016, AtomClass::Ord}},
        {"ldots", {0x2026, AtomClass::Inner}},   {"cdots", {0x22EF, AtomClass::Inner}},
        // Large operators
        {"sum", {0x2211, AtomClass::Op}},        {"int", {0x222B, AtomClass::Op}},
        {"prod", {0x220F, AtomClass::Op}},       {"oint", {0x222E, AtomClass::Op}},
        {"bigcup", {0x22C3, AtomClass::Op}},     {"bigcap", {0x22C2, AtomClass::Op}},
    };
    return t;
}

// Spacing macros (\, \; \quad …), width in em (TeXbook mu: thin=3/18, med=4/18, thick=5/18).
bool spacingMacro(const std::string& name, float& em) {
    if (name == "quad") {
        em = 1.0f;
        return true;
    }
    if (name == "qquad") {
        em = 2.0f;
        return true;
    }
    return false;
}

bool isLetter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool isDigit(char c) { return c >= '0' && c <= '9'; }

// Classify a literal (non-macro) character into a math Symbol.
Symbol charSymbol(char32_t cp) {
    if (cp < 0x80) {
        const char c = static_cast<char>(cp);
        if (isLetter(c)) {
            return {cp, AtomClass::Ord, Alphabet::MathItalic};
        }
        if (isDigit(c)) {
            return {cp, AtomClass::Ord, Alphabet::Upright};
        }
        switch (c) {
        case '+':
            return {U'+', AtomClass::Bin, Alphabet::Upright};
        case '-':
            return {0x2212, AtomClass::Bin, Alphabet::Upright}; // MINUS SIGN
        case '*':
            return {0x2217, AtomClass::Bin, Alphabet::Upright}; // ASTERISK OPERATOR
        case '=':
            return {U'=', AtomClass::Rel, Alphabet::Upright};
        case '<':
            return {U'<', AtomClass::Rel, Alphabet::Upright};
        case '>':
            return {U'>', AtomClass::Rel, Alphabet::Upright};
        case '(':
        case '[':
            return {cp, AtomClass::Open, Alphabet::Upright};
        case ')':
        case ']':
            return {cp, AtomClass::Close, Alphabet::Upright};
        case ',':
        case ';':
            return {cp, AtomClass::Punct, Alphabet::Upright};
        case '|':
            return {U'|', AtomClass::Ord, Alphabet::Upright};
        case '\'':
            return {0x2032, AtomClass::Ord, Alphabet::MathSymbol}; // PRIME
        default:
            return {cp, AtomClass::Ord, Alphabet::Upright};
        }
    }
    // A literal non-ASCII codepoint (e.g. a pasted π): draw it from the math face.
    return {cp, AtomClass::Ord, Alphabet::MathSymbol};
}

// Recursively set the alphabet of every Symbol leaf (for \mathrm / \mathbf / \mathit).
void applyAlphabet(List& list, Alphabet a);
void applyAlphabet(Node& n, Alphabet a) {
    if (auto* s = std::get_if<Symbol>(&n.v)) {
        // Only re-alphabet letters (digits/operators/symbols keep their nature; \mathbf of a
        // letter -> bold, \mathrm -> upright). Non-letters are left as-is.
        if (s->cp < 0x80 && isLetter(static_cast<char>(s->cp))) {
            s->alphabet = a;
        }
    } else if (auto* f = std::get_if<Fraction>(&n.v)) {
        applyAlphabet(f->num, a);
        applyAlphabet(f->den, a);
    } else if (auto* r = std::get_if<Radical>(&n.v)) {
        applyAlphabet(r->index, a);
        applyAlphabet(r->radicand, a);
    } else if (auto* ac = std::get_if<Accent>(&n.v)) {
        applyAlphabet(ac->base, a);
    } else if (auto* d = std::get_if<Delimited>(&n.v)) {
        applyAlphabet(d->body, a);
    } else if (auto* sc = std::get_if<Scripted>(&n.v)) {
        applyAlphabet(sc->nucleus, a);
        applyAlphabet(sc->sup, a);
        applyAlphabet(sc->sub, a);
    }
}
void applyAlphabet(List& list, Alphabet a) {
    for (auto& n : list) {
        applyAlphabet(n, a);
    }
}

// Delimiter codepoints from a control word (\langle, \lvert, …) or a control symbol (\{ \} \|).
char32_t delimFromName(const std::string& n) {
    if (n == "langle") {
        return 0x27E8;
    }
    if (n == "rangle") {
        return 0x27E9;
    }
    if (n == "lvert" || n == "rvert" || n == "vert") {
        return 0x007C;
    }
    if (n == "lVert" || n == "rVert" || n == "Vert") {
        return 0x2016;
    }
    if (n == "mid") {
        return 0x2223;
    }
    return 0; // unknown -> null delimiter
}

struct Parser {
    std::string_view s;
    std::size_t i{0};
    std::vector<std::string>* diag;

    explicit Parser(std::string_view src, std::vector<std::string>* d) : s(src), diag(d) {}

    void note(std::string m) {
        if (diag != nullptr) {
            diag->push_back(std::move(m));
        }
    }
    [[nodiscard]] bool eof() const { return i >= s.size(); }
    [[nodiscard]] char cur() const { return s[i]; }
    void skipWs() {
        while (!eof() && (cur() == ' ' || cur() == '\t' || cur() == '\n' || cur() == '\r')) {
            ++i;
        }
    }

    // Decode and consume one UTF-8 codepoint.
    char32_t nextCp() {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            ++i;
            return c;
        }
        const int n = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : 0;
        char32_t cp = c & (0x3Fu >> n);
        ++i;
        for (int k = 0; k < n && i < s.size(); ++k) {
            cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3Fu);
            ++i;
        }
        return cp;
    }

    // Read a control-word name [A-Za-z]+ (i is just past the backslash).
    std::string readName() {
        std::string n;
        while (!eof() && isLetter(cur())) {
            n.push_back(cur());
            ++i;
        }
        return n;
    }

    // True if a "\right" control word begins at i (without consuming).
    [[nodiscard]] bool peekRight() const {
        if (eof() || cur() != '\\') {
            return false;
        }
        std::size_t j = i + 1;
        std::string n;
        while (j < s.size() && isLetter(s[j])) {
            n.push_back(s[j]);
            ++j;
        }
        return n == "right";
    }

    // Parse a list until '}' (left for the caller), a "\right" (left for \left), or EOF.
    List parseList() {
        List out;
        for (;;) {
            skipWs();
            if (eof() || cur() == '}' || peekRight()) {
                break;
            }
            const std::size_t before = i;
            parseElement(out);
            if (i == before) {
                ++i; // safety: never spin on an unconsumable byte
            }
        }
        return out;
    }

    // A nucleus plus any trailing scripts.
    void parseElement(List& out) {
        List nucleus = parseNucleus();
        skipWs();
        bool hasSup = false;
        bool hasSub = false;
        List sup;
        List sub;
        while (!eof() && (cur() == '^' || cur() == '_')) {
            const char which = cur();
            ++i;
            List arg = parseArg();
            if (which == '^') {
                if (hasSup) {
                    note("double superscript");
                }
                sup = std::move(arg);
                hasSup = true;
            } else {
                if (hasSub) {
                    note("double subscript");
                }
                sub = std::move(arg);
                hasSub = true;
            }
            skipWs();
        }
        if (hasSup || hasSub) {
            Scripted sc;
            sc.nucleus = std::move(nucleus);
            sc.sup = std::move(sup);
            sc.sub = std::move(sub);
            sc.hasSup = hasSup;
            sc.hasSub = hasSub;
            out.push_back(Node{std::move(sc)});
        } else {
            for (auto& n : nucleus) {
                out.push_back(std::move(n));
            }
        }
    }

    // A single atom, a braced group, or a macro construct (may be empty, e.g. a leading '^').
    List parseNucleus() {
        skipWs();
        if (eof() || cur() == '^' || cur() == '_' || cur() == '}') {
            return {};
        }
        if (cur() == '{') {
            return parseGroup();
        }
        if (cur() == '\\') {
            return parseMacro();
        }
        return {Node{charSymbol(nextCp())}};
    }

    // The argument of a script or a one-argument macro: a group or a single nucleus.
    List parseArg() {
        skipWs();
        if (eof()) {
            note("missing argument");
            return {};
        }
        if (cur() == '{') {
            return parseGroup();
        }
        return parseNucleus();
    }

    // { ... } — consume the braces, return the inner list.
    List parseGroup() {
        ++i; // '{'
        List inner = parseList();
        if (!eof() && cur() == '}') {
            ++i;
        } else {
            note("missing '}'");
        }
        return inner;
    }

    // A control sequence (i at the backslash).
    List parseMacro() {
        ++i; // backslash
        if (eof()) {
            note("trailing backslash");
            return {};
        }
        if (!isLetter(cur())) {
            return controlSymbol(nextCp());
        }
        return handleMacro(readName());
    }

    // \, \; \! \{ \} \| \$ … (backslash + one non-letter).
    List controlSymbol(char32_t cp) {
        switch (cp) {
        case U',':
            return {Node{Space{0.16667f}}};
        case U';':
            return {Node{Space{0.27778f}}};
        case U'!':
            return {Node{Space{-0.16667f}}};
        case U' ':
            return {Node{Space{0.25f}}};
        case U'{':
            return {Node{Symbol{0x007B, AtomClass::Open, Alphabet::Upright}}};
        case U'}':
            return {Node{Symbol{0x007D, AtomClass::Close, Alphabet::Upright}}};
        case U'|':
            return {Node{Symbol{0x2016, AtomClass::Ord, Alphabet::MathSymbol}}};
        case U'$':
        case U'%':
        case U'&':
        case U'#':
        case U'_':
            return {Node{charSymbol(cp)}};
        default:
            note("unknown control symbol");
            return {Node{charSymbol(cp)}};
        }
    }

    // Read a \left/\right delimiter (a char, '.', or a control delimiter); 0 = null delimiter.
    char32_t readDelim() {
        skipWs();
        if (eof()) {
            note("missing delimiter");
            return 0;
        }
        if (cur() == '\\') {
            ++i;
            if (eof()) {
                return 0;
            }
            if (!isLetter(cur())) {
                const char32_t c = nextCp();
                if (c == U'{') {
                    return 0x007B;
                }
                if (c == U'}') {
                    return 0x007D;
                }
                if (c == U'|') {
                    return 0x2016;
                }
                return c;
            }
            return delimFromName(readName());
        }
        const char32_t c = nextCp();
        if (c == U'.') {
            return 0; // null delimiter
        }
        if (c == U'<') {
            return 0x27E8;
        }
        if (c == U'>') {
            return 0x27E9;
        }
        return c; // ( ) [ ] | / etc. drawn as-is
    }

    // Dispatch a control word to its construct or symbol.
    List handleMacro(const std::string& name) {
        if (name == "frac") {
            Fraction f;
            f.num = parseArg();
            f.den = parseArg();
            return {Node{std::move(f)}};
        }
        if (name == "sqrt") {
            Radical r;
            skipWs();
            if (!eof() && cur() == '[') {
                ++i;
                r.index = parseUntil(']');
                r.hasIndex = true;
            }
            r.radicand = parseArg();
            return {Node{std::move(r)}};
        }
        if (name == "hat" || name == "dot" || name == "overline") {
            Accent a;
            a.overbar = (name == "overline");
            a.mark = (name == "hat") ? 0x0302 : (name == "dot") ? 0x0307 : 0;
            a.base = parseArg();
            return {Node{std::move(a)}};
        }
        if (name == "left") {
            Delimited d;
            d.left = readDelim();
            d.body = parseList();
            if (peekRight()) {
                ++i;            // backslash
                (void) readName(); // "right"
                d.right = readDelim();
            } else {
                note("missing \\right");
            }
            return {Node{std::move(d)}};
        }
        if (name == "right") {
            note("lone \\right");
            (void) readDelim();
            return {};
        }
        if (name == "ket" || name == "bra" || name == "braket") {
            Delimited d;
            d.left = (name == "ket") ? 0x007C : 0x27E8;  // | …  or  ⟨ …
            d.right = (name == "bra") ? 0x007C : 0x27E9; // … |  or  … ⟩
            d.body = parseArg();
            return {Node{std::move(d)}};
        }
        if (name == "ketbra") {
            Delimited a;
            a.left = 0x007C;
            a.right = 0x27E9;
            a.body = parseArg();
            Delimited b;
            b.left = 0x27E8;
            b.right = 0x007C;
            b.body = parseArg();
            return {Node{std::move(a)}, Node{std::move(b)}};
        }
        if (name == "mathrm" || name == "mathbf" || name == "mathit") {
            List inner = parseArg();
            applyAlphabet(inner, name == "mathrm" ? Alphabet::Upright
                                 : name == "mathbf" ? Alphabet::Bold
                                                    : Alphabet::MathItalic);
            return inner;
        }
        if (float em = 0.0f; spacingMacro(name, em)) {
            return {Node{Space{em}}};
        }
        if (auto it = symbolTable().find(name); it != symbolTable().end()) {
            return {Node{Symbol{it->second.cp, it->second.cls, Alphabet::MathSymbol}}};
        }
        // Unknown control sequence: literal fallback (backslash + name, upright) + diagnostic.
        note("unknown command \\" + name);
        List out;
        out.push_back(Node{Symbol{U'\\', AtomClass::Ord, Alphabet::Upright}});
        for (char c : name) {
            out.push_back(Node{Symbol{static_cast<char32_t>(c), AtomClass::Ord, Alphabet::Upright}});
        }
        return out;
    }

    // Parse a list until a literal stop char (e.g. ']' for the \sqrt index), consuming it.
    List parseUntil(char stop) {
        List out;
        for (;;) {
            skipWs();
            if (eof()) {
                note("missing closing bracket");
                break;
            }
            if (cur() == stop) {
                ++i;
                break;
            }
            if (cur() == '}') {
                break;
            }
            const std::size_t before = i;
            parseElement(out);
            if (i == before) {
                ++i;
            }
        }
        return out;
    }
};

} // namespace

List parse(std::string_view mathSrc, std::vector<std::string>* diagnostics) {
    Parser p(mathSrc, diagnostics);
    return p.parseList();
}

std::vector<LabelSpan> splitLabel(std::string_view label, std::vector<std::string>* diagnostics) {
    std::vector<LabelSpan> spans;
    std::string text;
    std::string mathSrc;
    bool inMath = false;
    for (std::size_t i = 0; i < label.size();) {
        const char c = label[i];
        if (!inMath) {
            if (c == '\\' && i + 1 < label.size() && label[i + 1] == '$') {
                text.push_back('$'); // \$ -> literal dollar
                i += 2;
                continue;
            }
            if (c == '$') {
                if (!text.empty()) {
                    spans.push_back({false, std::move(text)});
                    text.clear();
                }
                inMath = true;
                ++i;
                continue;
            }
            text.push_back(c);
            ++i;
        } else {
            if (c == '$') {
                spans.push_back({true, std::move(mathSrc)});
                mathSrc.clear();
                inMath = false;
                ++i;
                continue;
            }
            mathSrc.push_back(c);
            ++i;
        }
    }
    if (inMath) {
        // Unmatched '$': the trailing run renders as literal text (ADR-0033 §1), '$' included.
        if (diagnostics != nullptr) {
            diagnostics->push_back("unmatched '$' — trailing run rendered as text");
        }
        std::string lit = "$";
        lit += mathSrc;
        spans.push_back({false, std::move(lit)});
    } else if (!text.empty()) {
        spans.push_back({false, std::move(text)});
    }
    return spans;
}

} // namespace iv::text::math
