#version 450
layout(location = 0) in vec3 position;
struct Camera { mat4 viewProjection; vec4 worldPosition; };
layout(set = 0, binding = 1, std140) uniform Cameras { Camera cameras[16]; };
struct Object { mat4 world; mat4 normalMatrix; };
layout(set = 0, binding = 2, std430) readonly buffer Objects { Object objects[]; };
layout(push_constant) uniform Draw { uint cameraIndex; uint objectIndex; } draw;
void main() {
    gl_Position = cameras[draw.cameraIndex].viewProjection * objects[draw.objectIndex].world * vec4(position, 1.0);
}
