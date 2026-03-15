#pragma pack_matrix(row_major)

struct VSOutput {
    [[vk::location(0)]] float2 fragTexCoord : TEXCOORD2;
};

[[vk::binding(0)]]
Texture2D<float4> blurringTexture;

[[vk::binding(1)]]
SamplerState sampleSampler;

static const int NUM_BILINEAR_TAPS = 8;
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
    float texelSize = 1.0 / float(width);
    
    float3 sum = blurringTexture.Sample(sampleSampler, input.fragTexCoord).rgb * centerWeight;
    float weightSum = centerWeight;

    [unroll]
    for (int i = 0; i < NUM_BILINEAR_TAPS; ++i) {
        float2 offset = float2(offsets[i] * texelSize, 0.0);
        sum += blurringTexture.Sample(sampleSampler, input.fragTexCoord + offset).rgb * weights[i];
        sum += blurringTexture.Sample(sampleSampler, input.fragTexCoord - offset).rgb * weights[i];
        weightSum += weights[i] * 2.0;
    }

    return float4(sum / weightSum, 1.0);
}