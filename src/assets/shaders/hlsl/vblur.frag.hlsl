#pragma pack_matrix(row_major)

struct VSOutput {
    [[vk::location(0)]] float2 fragTexCoord : TEXCOORD2;
};

[[vk::binding(0)]]
Texture2D<float4> blurringTexture;

[[vk::binding(1)]]
SamplerState sampleSampler;

static const int NUM_BILINEAR_TAPS = 4;
static const float centerWeight = 1.0;
static const float offsets[4] = { 1.40734, 3.29423, 5.20199, 7.13283 };
static const float weights[4] = { 1.48903, 0.45999, 0.05505, 0.00252 };

float4 main(VSOutput input) : SV_Target {
    uint width, height;
    blurringTexture.GetDimensions(width, height);
    float texelSize = 1.0 / float(height);

    float3 sum = blurringTexture.Sample(sampleSampler, input.fragTexCoord).rgb * centerWeight;
    float weightSum = centerWeight;

    [unroll]
    for (int i = 0; i < NUM_BILINEAR_TAPS; ++i) {
        float2 offset = float2(0.0, offsets[i] * texelSize);
        sum += blurringTexture.Sample(sampleSampler, input.fragTexCoord + offset).rgb * weights[i];
        sum += blurringTexture.Sample(sampleSampler, input.fragTexCoord - offset).rgb * weights[i];
        weightSum += weights[i] * 2.0;
    }

    return float4(sum / weightSum, 1.0);
}