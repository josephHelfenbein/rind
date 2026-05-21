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
SamplerState sampleSampler;

float4 main(VSOutput input) : SV_Target {
    float3 weights = float3(0.2126, 0.7152, 0.0722);
    float L = dot(colorTexture.Sample(sampleSampler, input.fragTexCoord).rgb, weights);

    float2 leftUV = input.fragTexCoord + float2(-pc.invScreenSize.x, 0);
    float2 topUV  = input.fragTexCoord + float2(0, -pc.invScreenSize.y);
    float Lleft = (leftUV.x >= 0.0) ? dot(colorTexture.Sample(sampleSampler, leftUV).rgb, weights) : L;
    float Ltop  = (topUV.y  >= 0.0) ? dot(colorTexture.Sample(sampleSampler, topUV).rgb,  weights) : L;

    float2 delta = abs(L - float2(Lleft, Ltop));
    float2 edges = step(float2(0.1, 0.1), delta);
    return float4(edges, 0.0, 1.0);
}
