#version 450

layout(push_constant) uniform PickingConstants {
    mat4 mvp;
    uint entity;
    uint jointOffset;
} picking;

layout(location = 0) in vec3 inPosition;
layout(location = 0) flat out uint entityId;

void main() {
    gl_Position = picking.mvp * vec4(inPosition, 1.0);
    entityId = picking.entity;
}
