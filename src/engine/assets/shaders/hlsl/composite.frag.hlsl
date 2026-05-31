struct VSOutput {
    [[vk::location(0)]] float2 fragTexCoord : TEXCOORD0;
};

struct PushConstants {
    float2 invScreenSize;
    uint flag; // 0 = none, 1 = fxaa, 2 = smaa
    float fadeAmount; // 0 = no fade, 1 = fully black
    uint hdrFlags; // bit 1 = HDR enabled, bit 2 = PQ (1) vs scRGB (0)
    float paperWhiteNits;
};

[[vk::push_constant]] PushConstants pc;

[[vk::binding(0)]]
Texture2D<float4> sceneTexture;

[[vk::binding(1)]]
Texture2D<float4> uiTexture;

[[vk::binding(2)]]
Texture2D<float4> smaaTexture;

[[vk::binding(3)]]
SamplerState sampleSampler;

float3 FXAA(float2 uv) {
    const float3 luma = float3(0.299, 0.587, 0.114);
    const float FXAA_EDGE_THRESHOLD = 0.125;
    const float FXAA_EDGE_THRESHOLD_MIN = 0.0312;
    const float FXAA_REDUCE_MUL = 1.0 / 8.0;
    const float FXAA_REDUCE_MIN = 1.0 / 128.0;
    const float FXAA_SPAN_MAX = 8.0;

    float2 texelSize = pc.invScreenSize;
    float3 rgbNW = sceneTexture.Sample(sampleSampler, saturate(uv + float2(-texelSize.x, -texelSize.y))).rgb;
    float3 rgbNE = sceneTexture.Sample(sampleSampler, saturate(uv + float2(texelSize.x, -texelSize.y))).rgb;
    float3 rgbSW = sceneTexture.Sample(sampleSampler, saturate(uv + float2(-texelSize.x, texelSize.y))).rgb;
    float3 rgbSE = sceneTexture.Sample(sampleSampler, saturate(uv + float2(texelSize.x, texelSize.y))).rgb;
    float3 rgbM = sceneTexture.Sample(sampleSampler, uv).rgb;

    float lumaNW = dot(rgbNW, luma);
    float lumaNE = dot(rgbNE, luma);
    float lumaSW = dot(rgbSW, luma);
    float lumaSE = dot(rgbSE, luma);
    float lumaM = dot(rgbM, luma);

    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    float range = lumaMax - lumaMin;
    if (range < max(FXAA_EDGE_THRESHOLD_MIN, lumaMax * FXAA_EDGE_THRESHOLD)) {
        return rgbM;
    }

    float2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y = ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * FXAA_REDUCE_MUL), FXAA_REDUCE_MIN);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin,
                float2(-FXAA_SPAN_MAX, -FXAA_SPAN_MAX),
                float2( FXAA_SPAN_MAX, FXAA_SPAN_MAX)) * texelSize;

    float3 rgbA = 0.5 * (
        sceneTexture.Sample(sampleSampler, saturate(uv + dir * (1.0 / 3.0 - 0.5))).rgb +
        sceneTexture.Sample(sampleSampler, saturate(uv + dir * (2.0 / 3.0 - 0.5))).rgb);
    float3 rgbB = rgbA * 0.5 + 0.25 * (
        sceneTexture.Sample(sampleSampler, saturate(uv + dir * -0.5)).rgb +
        sceneTexture.Sample(sampleSampler, saturate(uv + dir * 0.5)).rgb);

    float lumaB = dot(rgbB, luma);
    if (lumaB < lumaMin || lumaB > lumaMax) {
        return rgbA;
    }
    return rgbB;
}

static const float3x3 Rec709ToRec2020 = {
    0.627403896, 0.329283069, 0.043313036,
    0.069097289, 0.919540395, 0.011362316,
    0.016391439, 0.088013308, 0.895595253
};

float3 srgbToLinear(float3 c) {
    float3 lo = c / 12.92;
    float3 hi = pow((c + 0.055) / 1.055, 2.4);
    return lerp(hi, lo, step(c, 0.04045));
}

float3 pqEOTF(float3 N) {
    const float m1 = 2610.0 / 16384.0;
    const float m2 = 2523.0 / 4096.0 * 128.0;
    const float c1 = 3424.0 / 4096.0;
    const float c2 = 2413.0 / 4096.0 * 32.0;
    const float c3 = 2392.0 / 4096.0 * 32.0;
    float3 Np = pow(max(N, 0.0), 1.0 / m2);
    float3 num = max(Np - c1, 0.0);
    float3 den = max(c2 - c3 * Np, 1e-6);
    return pow(num / den, 1.0 / m1);
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

float4 main(VSOutput input) : SV_Target {
    float4 uiColor = uiTexture.Sample(sampleSampler, input.fragTexCoord);

    float3 sceneColor;
    if (pc.flag == 2) {
        sceneColor = smaaTexture.Sample(sampleSampler, input.fragTexCoord).rgb;
    } else if (pc.flag == 1) {
        sceneColor = FXAA(input.fragTexCoord);
    } else {
        sceneColor = sceneTexture.Sample(sampleSampler, input.fragTexCoord).rgb;
    }

    bool hdrOn = (pc.hdrFlags & 2u) != 0u;
    bool isPQ  = (pc.hdrFlags & 4u) != 0u;

    if (hdrOn) {
        float fade = saturate(pc.fadeAmount);
        if (uiColor.a <= 0.0 && fade <= 0.0) {
            return float4(sceneColor, 1.0);
        }
        float3 uiNits709 = srgbToLinear(saturate(uiColor.rgb)) * pc.paperWhiteNits;
        if (isPQ) {
            float3 sceneNits = pqEOTF(sceneColor) * 10000.0;
            float3 uiNits = mul(Rec709ToRec2020, uiNits709);
            float3 blended = lerp(sceneNits, uiNits, uiColor.a) * (1.0 - fade);
            return float4(pqInverseEOTF(blended / 10000.0), 1.0);
        } else {
            // scRGB linear
            float3 sceneNits = sceneColor * 80.0;
            float3 blended = lerp(sceneNits, uiNits709, uiColor.a) * (1.0 - fade);
            return float4(blended / 80.0, 1.0);
        }
    }

    float4 composited = lerp(float4(sceneColor, 1.0), uiColor, uiColor.a);
    composited = float4((composited.rgb - 0.5) * (saturate(pc.fadeAmount) * 2.0 + 1.0) + 0.5, 1.0);
    return composited;
}
