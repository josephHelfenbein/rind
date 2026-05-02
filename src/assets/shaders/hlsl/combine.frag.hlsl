struct VSOutput {
    [[vk::location(0)]] float2 fragTexCoord : TEXCOORD0;
};

struct PushConstants {
    float exposure;
    uint pad0;
    uint pad1;
    uint pad2;
};

[[vk::push_constant]] PushConstants pc;

[[vk::binding(0)]]
Texture2D<float4> sceneTexture;

[[vk::binding(1)]]
Texture2D<float4> ssrTexture;

[[vk::binding(2)]]
Texture2D<float4> bloomTexture;

[[vk::binding(3)]]
SamplerState sampleSampler;

static const float3x3 AgXInsetMatrix = {
    0.842479062253094, 0.0784335999999992, 0.0792237451477643,
    0.0423282422610123, 0.878468636469772, 0.0791661274605434,
    0.0423756549057051, 0.0784336, 0.879142973793104
};

static const float3x3 AgXOutsetMatrix = {
     1.19687900512017, -0.0980208811401368, -0.0990297440797205,
    -0.0528968517574562, 1.15190312990417, -0.0989611768448433,
    -0.0529716355144438, -0.0980434501171241, 1.15107367264116
};

static const float AgxMinEv = -12.47393;
static const float AgxMaxEv = 4.026069;

float3 agxDefaultContrastApprox(float3 x) {
    float3 x2 = x * x;
    float3 x4 = x2 * x2;
    return 15.5 * x4 * x2
         - 40.14 * x4 * x
         + 31.96 * x4
         - 6.868 * x2 * x
         + 0.4298 * x2
         + 0.1191 * x
         - 0.00232;
}

float3 agx(float3 val) {
    val = mul(AgXInsetMatrix, val);
    val = max(val, 1e-10);
    val = log2(val);
    val = (val - AgxMinEv) / (AgxMaxEv - AgxMinEv);
    val = saturate(val);
    return agxDefaultContrastApprox(val);
}

float3 agxLookPunchy(float3 val) {
    const float3 slope  = float3(1.0, 1.0, 1.0);
    const float3 power  = float3(1.35, 1.35, 1.35);
    const float3 offset = float3(0.0, 0.0, 0.0);
    const float saturation = 1.4;
    val = pow(max(val * slope + offset, 0.0), power);
    float luma = dot(val, float3(0.2126, 0.7152, 0.0722));
    return luma + saturation * (val - luma);
}

float3 agxEotf(float3 val) {
    val = mul(AgXOutsetMatrix, val);
    return pow(max(val, 0.0), 2.2);
}

float4 main(VSOutput input) : SV_Target {
    float3 scene = sceneTexture.Sample(sampleSampler, input.fragTexCoord).rgb;
    float4 ssr = ssrTexture.Sample(sampleSampler, input.fragTexCoord);
    float3 bloom = bloomTexture.Sample(sampleSampler, input.fragTexCoord).rgb;

    bloom = bloom * bloom * 0.5;

    float3 combined = scene + ssr.rgb * ssr.a;
    combined *= pc.exposure;
    combined += bloom;
    combined = agxEotf(agxLookPunchy(agx(combined)));
    return float4(combined, 1.0);
}
