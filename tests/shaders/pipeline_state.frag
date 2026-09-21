#version 450

layout(constant_id = 0) const float intensity = 1.0;
layout(location = 0) out vec4 firstColor;
layout(location = 1) out vec4 secondColor;

void main()
{
    firstColor = vec4(intensity, 0, 0, 0.5);
    secondColor = vec4(0, 0, intensity, 0.5);
}
