#version 450

// GPU glyph rendering (ADR-0023): vertex stage. Glyph quad corners are
// pre-projected to clip space (NDC) on the CPU, so this stage is a pass-through;
// the resolution-independent coverage is computed per fragment from the Slug
// atlas. texcoord is em-space (font units) — the coordinate the Slug shader
// samples the glyph outline at.

layout(location = 0) in vec2 a_position; // clip-space (NDC) quad corner
layout(location = 1) in vec2 a_texcoord; // em-space sample coordinate (font units)
layout(location = 2) in uint a_glyphLoc; // atlas texel offset for this glyph
layout(location = 3) in vec4 a_color;    // RGBA, straight (non-premultiplied) alpha

layout(location = 0) out vec2 v_texcoord;
layout(location = 1) flat out uint v_glyphLoc;
layout(location = 2) out vec4 v_color;

void main() {
    gl_Position = vec4(a_position, 0.0, 1.0);
    v_texcoord = a_texcoord;
    v_glyphLoc = a_glyphLoc;
    v_color = a_color;
}
