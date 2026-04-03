#pragma pack_matrix(row_major)

struct VSOutput {
    [[vk::location(0)]] float2 fragTexCoord : TEXCOORD2;
};

[[vk::binding(0)]]
Texture2D<float4> blurringTexture;

[[vk::binding(1)]]
SamplerState sampleSampler;

struct PushConstants {
    uint blurDirection; // 0 for horizontal, 1 for vertical
    uint taps; // number of taps to use, up to 8
};
[[vk::push_constant]] PushConstants pc;

static const float centerWeight = 1.0;

static const float offsets[8] = {
    1.47659, 3.44544, 5.41503, 7.38516,
    9.35572, 11.32793, 13.30069, 15.27504
};
static const float weights[8] = {
    1.85173, 1.36155, 0.78249, 0.35148,
    0.12349, 0.03387, 0.00727, 0.00122
};

float4 main(VSOutput input) : SV_Target {
    uint width, height;
    blurringTexture.GetDimensions(width, height);
    float2 texelSize = 1.0 / float2(width, height);
    float2 minUV = texelSize * 0.5;
    float2 maxUV = 1.0 - minUV;
    float2 blurAxis = (pc.blurDirection == 0) ? float2(1.0, 0.0) : float2(0.0, 1.0);
    uint tapCount = min(pc.taps, 8u);

    float2 baseUV = input.fragTexCoord;
    float3 sum = blurringTexture.Sample(sampleSampler, baseUV).rgb * centerWeight;
    float weightSum = centerWeight;

    [unroll]
    for (int i = 0; i < 8; ++i) {
        if (i >= tapCount) {
            break;
        }
        float2 offset = blurAxis * (offsets[i] * texelSize);
        float2 uvPlus = baseUV + offset;
        if (uvPlus.x >= minUV.x && uvPlus.x <= maxUV.x && uvPlus.y >= minUV.y && uvPlus.y <= maxUV.y) {
            sum += blurringTexture.Sample(sampleSampler, uvPlus).rgb * weights[i];
            weightSum += weights[i];
        }
        float2 uvMinus = baseUV - offset;
        if (uvMinus.x >= minUV.x && uvMinus.x <= maxUV.x && uvMinus.y >= minUV.y && uvMinus.y <= maxUV.y) {
            sum += blurringTexture.Sample(sampleSampler, uvMinus).rgb * weights[i];
            weightSum += weights[i];
        }
    }

    return float4(sum / weightSum, 1.0);
}
