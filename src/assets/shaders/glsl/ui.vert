#version 450

layout (location = 0) in vec2 inPosition;
layout (location = 1) in vec2 inTexCoords;

layout (location = 0) out vec2 texCoord;

layout(push_constant) uniform PushConstants {
    vec3 tint;
    mat4 model;
} pc;

void main(){
    texCoord = inTexCoords;
    gl_Position = pc.model * vec4(inPosition, 0.0, 1.0);
}