#pragma pack_matrix(row_major)

struct VSOutput {
    [[vk::location(0)]] float2 fragTexCoord : TEXCOORD2;
};

[[vk::binding(0)]]
Texture2D<float4> bloomMipTexture;

[[vk::binding(1)]]
SamplerState sampleSampler;

static const int GHOST_COUNT = 6;
static const float GHOST_SPACING = 0.28;
static const float GHOST_THRESHOLD = 10.0;
static const float HALO_MULTIPLIER = 0.05;
static const float HALO_WIDTH = 0.4;
static const float CHROMA_DISTORT = 0.001;
static const float FLARE_INTENSITY = 0.0035;

static const float3 tints[GHOST_COUNT] = {
    float3(1.0, 0.85, 0.7),
    float3(0.6, 1.0, 0.8),
    float3(0.8, 0.7, 1.0),
    float3(1.0, 0.6, 0.5),
    float3(0.5, 0.9, 1.0),
    float3(1.0, 1.0, 0.6),
};

float3 thresholdSample(float2 uv) {
    if (any(uv < 0.0) || any(uv > 1.0)) return 0.0;
    float3 c = bloomMipTexture.SampleLevel(sampleSampler, uv, 0).rgb;
    float l = dot(c, float3(0.2126, 0.7152, 0.0722));
    float s = max(l - GHOST_THRESHOLD, 0.0) / max(l, 1e-4);
    return c * s;
}

float3 chromaSample(float2 uv, float2 radialDir) {
    float3 c;
    c.r = thresholdSample(uv + radialDir * -CHROMA_DISTORT).r;
    c.g = thresholdSample(uv).g;
    c.b = thresholdSample(uv + radialDir * CHROMA_DISTORT).b;
    return c;
}

float4 main(VSOutput input) : SV_Target {
    float2 uv = input.fragTexCoord;
    float2 flipUV = 1.0 - uv;
    float2 ghostDir = (0.5 - flipUV) * GHOST_SPACING;

    float3 sum = 0.0;
    [unroll]
    for (int i = 0; i < GHOST_COUNT; ++i) {
        float2 sUV = flipUV + ghostDir * float(i);
        float2 radial = 0.5 - sUV;
        float2 rDir = normalize(radial + float2(1e-5, 1e-5));
        float3 c = chromaSample(sUV, rDir);
        float w = pow(saturate(1.0 - length(radial) * 2.0), 4.0);
        sum += c * tints[i] * w;
    }

    float2 haloDir = normalize(0.5 - flipUV + float2(1e-5, 1e-5));
    float2 haloUV = flipUV + haloDir * HALO_WIDTH;
    float haloW = pow(saturate(1.0 - length(0.5 - haloUV) * 2.0), 5.0);
    sum += chromaSample(haloUV, haloDir) * haloW * HALO_MULTIPLIER;

    float vign = pow(saturate(1.0 - length(uv - 0.5)), 1.5);
    return float4(sum * vign * FLARE_INTENSITY, 1.0);
}
