#version 450
// 2D/3D overlay vertex shader (ADR-0021). Positions are transformed by a
// push-constant mat4: identity for screen-space (clip-space) geometry, or the
// camera view-projection (ADR-0012) for world-space geometry such as the M7
// bounding box. Color is passed straight through; alpha is composited by the
// pipeline's blend state over the volume render.
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inColor;

layout(push_constant) uniform PushConstants {
    mat4 transform; // column-major (GLSL convention)
} pc;

layout(location = 0) out vec4 vColor;

void main() {
    gl_Position = pc.transform * vec4(inPos, 1.0);
    vColor = inColor;
}
