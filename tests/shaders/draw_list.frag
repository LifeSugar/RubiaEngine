#version 450
layout(set = 1, binding = 0, std140) uniform Material { vec4 tint; } material;
// Deliberately default-on: compiler must explicitly disable clipping for ordinary draws.
layout(constant_id = 1000) const bool alphaClipEnabled = true;
layout(push_constant) uniform Draw {
    uint cameraIndex;
    uint objectIndex;
    float alphaClipThreshold;
} draw;
layout(location = 0) out vec4 color;
void main() {
    if (alphaClipEnabled && material.tint.a < draw.alphaClipThreshold) discard;
    color = material.tint;
}
