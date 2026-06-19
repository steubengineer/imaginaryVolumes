# Embed an arbitrary binary file as a C++ byte array (generalizes
# embed_spirv.cmake; ADR-0011/D-0022 pattern). Used for the bundled font
# (ADR-0022) so the default face needs no runtime file path.
#
# Invoked at build time by a custom command:
#   cmake -DIN=<file> -DSYMBOL=<sym> -DNS=<namespace> -DHDR=<include>
#         -DOUT=<out.cpp> -P embed_bytes.cmake
#
# Emits, inside namespace <NS>:
#   alignas(4) const unsigned char <SYMBOL>_data[] = { ... };
#   const std::size_t            <SYMBOL>_size      = sizeof(<SYMBOL>_data);
# matching the extern declarations in <HDR>.

file(READ "${IN}" hex HEX)
string(REGEX REPLACE "(..)" "0x\\1," bytes "${hex}")

file(WRITE "${OUT}"
"// Generated from ${IN} by tools/embed_bytes.cmake. Do not edit.
#include \"${HDR}\"

namespace ${NS} {
alignas(4) const unsigned char ${SYMBOL}_data[] = {${bytes}};
const ::std::size_t ${SYMBOL}_size = sizeof(${SYMBOL}_data);
} // namespace ${NS}
")
