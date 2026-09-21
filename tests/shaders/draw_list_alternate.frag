#version 450
layout(set = 1, binding = 3, std140) uniform Material { vec4 tint; } material;
layout(location = 0) out vec4 color;
void main() { color = material.tint; }
