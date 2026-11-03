#version 450

layout(push_constant) uniform PickingConstants {
    mat4 mvp;
    uint entity;
    uint jointOffset;
} picking;

layout(location = 0) in vec3 inPosition;
layout(location = 0) flat out uint entityId;

layout(location = 1) in vec4 inWeights;
layout(location = 2) in uvec4 inJointIDs;
layout(set = 0, binding = 0, std430) readonly buffer SkinningMatrices {
    mat4 joints[];
};

void main() {
    uvec4 ids = inJointIDs + uvec4(picking.jointOffset);
    mat4 skin = inWeights.x * joints[ids.x]
              + inWeights.y * joints[ids.y]
              + inWeights.z * joints[ids.z]
              + inWeights.w * joints[ids.w];
    gl_Position = picking.mvp * skin * vec4(inPosition, 1.0);
    entityId = picking.entity;
}
