struct VSOutput {
    [[vk::location(0)]] float2 fragTexCoord : TEXCOORD0;
};

struct PushConstants {
    float2 invScreenSize;
    uint flag; // 0 = none, 1 = fxaa, 2 = smaa
    uint pad;
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
    float3 rgbM  = sceneTexture.Sample(sampleSampler, uv).rgb;

    float lumaNW = dot(rgbNW, luma);
    float lumaNE = dot(rgbNE, luma);
    float lumaSW = dot(rgbSW, luma);
    float lumaSE = dot(rgbSE, luma);
    float lumaM  = dot(rgbM,  luma);

    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    float range = lumaMax - lumaMin;
    if (range < max(FXAA_EDGE_THRESHOLD_MIN, lumaMax * FXAA_EDGE_THRESHOLD)) {
        return rgbM;
    }

    float2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * FXAA_REDUCE_MUL), FXAA_REDUCE_MIN);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin,
                float2(-FXAA_SPAN_MAX, -FXAA_SPAN_MAX),
                float2( FXAA_SPAN_MAX,  FXAA_SPAN_MAX)) * texelSize;

    float3 rgbA = 0.5 * (
        sceneTexture.Sample(sampleSampler, saturate(uv + dir * (1.0 / 3.0 - 0.5))).rgb +
        sceneTexture.Sample(sampleSampler, saturate(uv + dir * (2.0 / 3.0 - 0.5))).rgb);
    float3 rgbB = rgbA * 0.5 + 0.25 * (
        sceneTexture.Sample(sampleSampler, saturate(uv + dir * -0.5)).rgb +
        sceneTexture.Sample(sampleSampler, saturate(uv + dir *  0.5)).rgb);

    float lumaB = dot(rgbB, luma);
    if (lumaB < lumaMin || lumaB > lumaMax) {
        return rgbA;
    }
    return rgbB;
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

    return lerp(float4(sceneColor, 1.0), uiColor, uiColor.a);
}
