# Embed a SPIR-V binary as a C++ byte array (ADR-0011, D-0022).
#
# Invoked at build time by a custom command:
#   cmake -DSPV=<in.spv> -DSYMBOL=<sym> -DOUT=<out.cpp> -P embed_spirv.cmake
#
# Emits definitions matching the extern declarations in include/iv/vk/shaders.hpp:
#   alignas(4) const unsigned char <SYMBOL>_data[] = { ... };
#   const std::size_t            <SYMBOL>_size      = sizeof(<SYMBOL>_data);
# The array is 4-byte aligned so it can be read as a uint32 SPIR-V stream.

file(READ "${SPV}" hex HEX)
string(REGEX REPLACE "(..)" "0x\\1," bytes "${hex}")

file(WRITE "${OUT}"
"// Generated from ${SPV} by tools/embed_spirv.cmake (ADR-0011). Do not edit.
#include \"iv/vk/shaders.hpp\"

namespace iv::vk::shaders {
alignas(4) const unsigned char ${SYMBOL}_data[] = {${bytes}};
const ::std::size_t ${SYMBOL}_size = sizeof(${SYMBOL}_data);
} // namespace iv::vk::shaders
")
