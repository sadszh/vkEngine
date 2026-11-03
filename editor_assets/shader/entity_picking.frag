#version 450

layout(location = 0) flat in uint entityId;
layout(location = 0) out uint outEntityId;

void main() {
    outEntityId = entityId;
}
