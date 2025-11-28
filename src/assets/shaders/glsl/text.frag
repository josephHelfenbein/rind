#version 450

layout (location = 0) in vec2 texCoord;
layout(binding = 0) uniform sampler2D sampleTexture;

layout(push_constant) uniform PushConstants {
    vec3 tint;
    mat4 model;
} pc;

layout (location = 0) out vec4 outColor;

void main(){
    vec2 glyphUV = vec2(texCoord.x, 1.0 - texCoord.y);
    float alpha = texture(sampleTexture, glyphUV).r;
    outColor = vec4(pc.textColor, alpha);
    if(alpha < 0.01) discard;
}