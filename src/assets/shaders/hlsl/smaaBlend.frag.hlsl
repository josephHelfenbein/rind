struct VSOutput {
    [[vk::location(0)]] float2 fragTexCoord : TEXCOORD0;
};

struct PushConstants {
    float2 invScreenSize;
    uint flags;
    uint pad;
};

[[vk::push_constant]] PushConstants pc;

[[vk::binding(0)]]
Texture2D<float4> colorTexture;

[[vk::binding(1)]]
Texture2D<float4> blendTexture;

[[vk::binding(2)]]
SamplerState sampleSampler;

#define SMAA_RT_METRICS float4(pc.invScreenSize.x, pc.invScreenSize.y, 1.0/pc.invScreenSize.x, 1.0/pc.invScreenSize.y)

void SMAAMovc(bool2 cond, inout float2 variable, float2 value) {
    [flatten] if (cond.x) variable.x = value.x;
    [flatten] if (cond.y) variable.y = value.y;
}

void SMAAMovc(bool4 cond, inout float4 variable, float4 value) {
    SMAAMovc(cond.xy, variable.xy, value.xy);
    SMAAMovc(cond.zw, variable.zw, value.zw);
}

void SMAANeighborhoodBlendingVS(float2 texcoord, out float4 offset) {
    offset = mad(SMAA_RT_METRICS.xyxy, float4(1.0, 0.0, 0.0, 1.0), texcoord.xyxy);
}

float4 SMAANeighborhoodBlendingPS(float2 texcoord, float4 offset) {
    float4 a;
    a.x = blendTexture.Sample(sampleSampler, offset.xy).a;
    a.y = blendTexture.Sample(sampleSampler, offset.zw).g;
    a.wz = blendTexture.Sample(sampleSampler, texcoord).xz;

    [branch]
    if (dot(a, float4(1.0, 1.0, 1.0, 1.0)) < 1e-5) {
        return colorTexture.SampleLevel(sampleSampler, texcoord, 0);
    } else {
        bool h = max(a.x, a.z) > max(a.y, a.w);

        float4 blendingOffset = float4(0.0, a.y, 0.0, a.w);
        float2 blendingWeight = a.yw;
        SMAAMovc(bool4(h, h, h, h), blendingOffset, float4(a.x, 0.0, a.z, 0.0));
        SMAAMovc(bool2(h, h), blendingWeight, a.xz);
        blendingWeight /= dot(blendingWeight, float2(1.0, 1.0));

        float4 blendingCoord = mad(blendingOffset, float4(SMAA_RT_METRICS.xy, -SMAA_RT_METRICS.xy), texcoord.xyxy);

        float4 color = blendingWeight.x * colorTexture.SampleLevel(sampleSampler, blendingCoord.xy, 0);
        color += blendingWeight.y * colorTexture.SampleLevel(sampleSampler, blendingCoord.zw, 0);

        return color;
    }
}

float4 main(VSOutput input) : SV_Target {
    float4 offset;
    SMAANeighborhoodBlendingVS(input.fragTexCoord, offset);
    return SMAANeighborhoodBlendingPS(input.fragTexCoord, offset);
}
