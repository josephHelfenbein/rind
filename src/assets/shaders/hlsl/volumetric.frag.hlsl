struct VSOutput {
    [[vk::location(0)]] float3 worldPos : TEXCOORD0;
    [[vk::location(1)]] nointerpolation uint instanceID : TEXCOORD1;
};

struct VolumetricData {
    float4x4 model;
    float4x4 invModel;
    float3 color;
    float age;
    float lifetime;
    float density;
    float2 pad;
};

[[vk::binding(0)]] StructuredBuffer<VolumetricData> volumes;
[[vk::binding(1)]] Texture2D<float> depthTexture;
[[vk::binding(2)]] SamplerState depthSampler;

void main(VSOutput input) {
    VolumetricData vol = volumes[input.instanceID];
    float depth = depthTexture.Sample(depthSampler, input.worldPos.xy / input.worldPos.z);
    if (input.worldPos.z > depth + 0.01) {
        discard;
    }
}