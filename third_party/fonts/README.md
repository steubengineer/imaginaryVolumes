# Bundled font (ADR-0022)

The default label font: **New Computer Modern (NCM)** — the OpenType, extended-Unicode
successor to Knuth's Computer Modern (the publication "TeX look").

## Bundled faces
- **`NewCM10-Book.otf`** — the Roman text face, "Book" weight (NCM 8.1.0's default
  text weight: `\usepackage[default]{fontsetup}` loads Book). ~0.68 MB. → `iv::text::bundledFont()`.
- **`NewCM10-BookItalic.otf`** — the matching **true italic** face (ADR-0032), for math
  variables and the legend field name (resolves B-0012). ~0.68 MB. → `iv::text::bundledFontItalic()`.
- **`NewCMMath-Book.otf`** — the **OpenType MATH** face (ADR-0032/0033): carries the MATH
  table (math italic/bold alphabets, large operators, radicals, stretchy delimiter
  variants/assemblies, and the positioning constants) HarfBuzz reads via `hb_ot_math_*` for
  M9 math typesetting. ~1.4 MB. → `iv::text::bundledFontMath()`.
- All three are embedded into `iv_text` at build time (`tools/embed_bytes.cmake`), so the
  default fonts need no runtime file path (consistent with the embedded shaders / colormap LUT).

## Provenance
- Package: New Computer Modern **8.1.0**, Antonis Tsolomitis.
- Source: https://ctan.org/pkg/newcomputermodern
  (mirror `https://mirrors.ctan.org/fonts/newcomputermodern.zip`, the `otf/` dir).

## License — GUST Font License (GFL), free/libre
Per the package `License.txt` (also bundled here as `NCM-License.txt`): **all NCM faces
are under the GUST Font License (GFL, an LPPL-1.3c-based free license) EXCEPT**
`NewCM10-Regular`, all `NewCMUncial*`, and all `NewCM*Devanagari`, which are
GPL3+FontException+DistributionException.

`NewCM10-Book`, `NewCM10-BookItalic`, and `NewCMMath-Book` are **not** in that GPL subset
(only the *Regular* text weight `NewCM10-Regular`, Uncial, and Devanagari are), so all three
bundled faces are **GFL** — verified from each face's `name` table copyright (nameID 0:
"released under the GUST Font License", © Antonis Tsolomitis). The GFL text is
`GUST-FONT-LICENSE.txt`. The GPL subset is deliberately **excluded** from this repository to
keep the bundle maximally permissive (ADR-0022).

> GFL clause 1 *requests* (does not require) that derived/renamed fonts be renamed. We
> ship the face **verbatim and unmodified**, so the request does not apply.

A top-level project `LICENSE` is still to be added (Backlog **B-0009**); it does not
affect bundling these GFL fonts.
