#version 450
// Overlay fragment shader (ADR-0021): straight-through color. The pipeline's blend
// state (src_alpha, 1-src_alpha) composites this over the volume render.
layout(location = 0) in vec4 vColor;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = vColor;
}
