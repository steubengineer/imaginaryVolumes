#version 450

// GPU glyph rendering (ADR-0023): fragment stage. Pulls in libharfbuzz-gpu's Slug
// coverage (hb-gpu-fragment.glsl) and the hb_gpu_draw() wrapper
// (hb-gpu-draw-fragment.glsl) from the vendored sources, compiled with
// -I third_party/harfbuzz/src and glslc's -fauto-bind-uniforms (which binds the
// atlas sampler `isamplerBuffer hb_gpu_atlas`, declared inside the included
// source, to set 0 / binding 0). Coverage is analytic from the glyph outline, so
// it stays crisp at any output resolution / zoom (ADR-0023).

#include "hb-gpu-fragment.glsl"
#include "hb-gpu-draw-fragment.glsl"

layout(location = 0) in vec2 v_texcoord;
layout(location = 1) flat in uint v_glyphLoc;
layout(location = 2) in vec4 v_color;

layout(location = 0) out vec4 outColor;

void main() {
    float cov = hb_gpu_draw(v_texcoord, v_glyphLoc); // Slug coverage in [0, 1]
    // Straight-alpha text: the pipeline's srcAlpha/oneMinusSrcAlpha blend composites
    // this over the volume (ADR-0021).
    outColor = vec4(v_color.rgb, v_color.a * cov);
}
