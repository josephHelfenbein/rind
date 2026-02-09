#pragma pack_matrix(row_major)

struct VSOutput {
    [[vk::location(0)]] float2 fragTexCoord : TEXCOORD2;
};

[[vk::binding(0)]]
Texture2D<float4> blurringTexture;

[[vk::binding(1)]]
SamplerState sampleSampler;

static const int BLUR_RADIUS = 32;
static const float SIGMA = 8.0;
static const float PI = 3.14159265359;

float gaussian(float x, float sigma) {
    float sigma2 = sigma * sigma;
    return exp(-(x * x) / (2.0 * sigma2)) / sqrt(2.0 * PI * sigma2);
}

float4 main(VSOutput input) : SV_Target {
    uint width, height;
    blurringTexture.GetDimensions(width, height);
    float texelSize = 1.0 / float(width);
    
    float3 sum = float3(0.0, 0.0, 0.0);
    float weightSum = 0.0;
    
    [unroll]
    for (int dx = -BLUR_RADIUS; dx <= BLUR_RADIUS; ++dx) {
        float weight = gaussian(float(dx), SIGMA);
        float2 offset = float2(float(dx) * texelSize, 0.0);
        float3 texSample = blurringTexture.Sample(sampleSampler, input.fragTexCoord + offset).rgb;
        sum += texSample * weight;
        weightSum += weight;
    }
    
    return float4(sum / weightSum, 1.0);
}