struct VSOutput {
    [[vk::location(0)]] float2 fragTexCoord : TEXCOORD0;
};

struct PushConstants {
    float exposure;
    uint flags; // bit 0 = SSR, bit 1 = HDR enabled, bit 2 = PQ (1) vs scRGB (0)
    float displayMaxNits;
    float paperWhiteNits;
};

[[vk::push_constant]] PushConstants pc;

[[vk::binding(0)]]
Texture2D<float4> sceneTexture;

[[vk::binding(1)]]
Texture2D<float4> ssrTexture;

[[vk::binding(2)]]
Texture2D<float4> bloomTexture;

[[vk::binding(3)]]
Texture2D<float4> flareTexture;

[[vk::binding(4)]]
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

float3 agxLog(float3 val) {
    val = mul(AgXInsetMatrix, val);
    val = max(val, 1e-10);
    val = log2(val);
    val = (val - AgxMinEv) / (AgxMaxEv - AgxMinEv);
    return val;
}

float3 agxLook(float3 val) {
    const float3 slope = float3(1.0, 1.0, 1.0);
    const float3 power = float3(1.35, 1.35, 1.35);
    const float3 offset = float3(0.0, 0.0, 0.0);
    const float saturation = 1.4;
    val = pow(max(val * slope + offset, 0.0), power);
    float luma = dot(val, float3(0.2126, 0.7152, 0.0722));
    return luma + saturation * (val - luma);
}

float3 agxSigmoid(float3 val) {
    return agxDefaultContrastApprox(saturate(val));
}

float3 agxEotf(float3 val) {
    val = mul(AgXOutsetMatrix, val);
    return pow(max(val, 0.0), 2.2);
}

float hash12(float2 p) {
    float3 p3 = frac(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float3 triangularDither(float2 pixel) {
    float r = hash12(pixel) - hash12(pixel + 17.13);
    float g = hash12(pixel + 41.71) - hash12(pixel + 71.29);
    float b = hash12(pixel + 113.5) - hash12(pixel + 151.7);
    return float3(r, g, b);
}

static const float3x3 Rec709ToRec2020 = {
    0.627403896, 0.329283069, 0.043313036,
    0.069097289, 0.919540395, 0.011362316,
    0.016391439, 0.088013308, 0.895595253
};

float3 agxToneHDR(float3 logVal, float peakRatio) {
    float3 sdrLinear = agxEotf(agxSigmoid(logVal));
    const float HL_KNEE = 0.68;
    float3 t = saturate((logVal - HL_KNEE) / (1.0 - HL_KNEE));
    float3 shoulder = t * t * (3.0 - 2.0 * t);
    return max(sdrLinear + shoulder * (peakRatio - 1.0), 0.0);
}

float3 pqInverseEOTF(float3 L) {
    const float m1 = 2610.0 / 16384.0;
    const float m2 = 2523.0 / 4096.0 * 128.0;
    const float c1 = 3424.0 / 4096.0;
    const float c2 = 2413.0 / 4096.0 * 32.0;
    const float c3 = 2392.0 / 4096.0 * 32.0;
    float3 Lm = pow(max(L, 0.0), m1);
    return pow((c1 + c2 * Lm) / (1.0 + c3 * Lm), m2);
}

float4 main(VSOutput input, float4 fragCoord : SV_Position) : SV_Target {
    float3 scene = sceneTexture.Sample(sampleSampler, input.fragTexCoord).rgb;
    float4 ssr = ((pc.flags & 1u) != 0u) ? ssrTexture.Sample(sampleSampler, input.fragTexCoord) : float4(0.0, 0.0, 0.0, 0.0);
    float3 bloom = bloomTexture.Sample(sampleSampler, input.fragTexCoord).rgb;
    float3 flare = flareTexture.Sample(sampleSampler, input.fragTexCoord).rgb;

    const float BLOOM_POWER = 1.5;
    const float BLOOM_GAIN = 0.45;
    bloom = pow(max(bloom, 0.0), BLOOM_POWER) * BLOOM_GAIN;

    float3 combined = scene + ssr.rgb * ssr.a;
    combined *= pc.exposure;
    combined += bloom;
    combined += flare;

    float3 graded = agxLook(agxLog(combined));

    bool hdrOn = (pc.flags & 2u) != 0u;
    bool isPQ  = (pc.flags & 4u) != 0u;

    if (hdrOn) {
        const float HDR_PIVOT = 0.5;
        const float HDR_CONTRAST = 1.06;
        graded = (graded - HDR_PIVOT) * HDR_CONTRAST + HDR_PIVOT;

        float peakRatio = max(pc.displayMaxNits / max(pc.paperWhiteNits, 1.0), 1.0);
        float3 linearRec709 = agxToneHDR(graded, peakRatio);
        float3 nitsRec709 = linearRec709 * pc.paperWhiteNits;

        float3 outRGB;
        if (isPQ) {
            float3 nitsRec2020 = mul(Rec709ToRec2020, nitsRec709);
            outRGB = pqInverseEOTF(nitsRec2020 / 10000.0);
            outRGB += triangularDither(fragCoord.xy) * (1.0 / 1023.0);
        } else {
            // scRGB
            outRGB = nitsRec709 / 80.0;
        }
        return float4(outRGB, 1.0);
    } else {
        combined = agxEotf(agxSigmoid(graded));
        combined = min((combined - 0.5) * 1.05 + 0.5, 1.0); // monitor used during testing had high contrast
        combined += triangularDither(fragCoord.xy) * (1.0 / 255.0);
        return float4(combined, 1.0);
    }
}
