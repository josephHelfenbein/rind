#version 450

layout (location = 0) in vec2 texCoord;
layout(binding = 0) uniform sampler2D sampleTexture;

layout(push_constant) uniform PushConstants {
    vec3 tint;
    mat4 model;
} pc;

layout (location = 0) out vec4 outColor;

void main(){
    outColor = texture(sampleTexture, texCoord) * vec4(pc.tint, 1.0);
}