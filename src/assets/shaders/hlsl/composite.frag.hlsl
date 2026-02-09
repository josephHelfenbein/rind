struct VSOutput {
    [[vk::location(0)]] float2 fragTexCoord : TEXCOORD0;
};

struct PushConstants {
    float2 invScreenSize;
    uint flag; // bit 0 = enable fxaa
    uint pad;
};

[[vk::push_constant]] PushConstants pc;

[[vk::binding(0)]]
Texture2D<float4> sceneTexture;

[[vk::binding(1)]]
Texture2D<float4> uiTexture;

[[vk::binding(2)]]
Texture2D<float4> textTexture;

[[vk::binding(3)]]
Texture2D<float4> ssrTexture;

[[vk::binding(4)]]
Texture2D<float> aoTexture;

[[vk::binding(5)]]
Texture2D<float4> bloomTexture;

[[vk::binding(6)]]
SamplerState sampleSampler;

float3 sampleCombined(float2 uv) {
    float3 scene = sceneTexture.Sample(sampleSampler, uv).rgb;
    float4 ssr = ssrTexture.Sample(sampleSampler, uv);
    float ao = aoTexture.Sample(sampleSampler, uv);
    return scene * ao + ssr.rgb * ssr.a;
}

float3 FXAA(float2 uv) {
    const float edgeThreshold = 0.08;
    float2 texelSize = pc.invScreenSize;
    float3 rgbNW = sampleCombined(uv + float2(-texelSize.x, -texelSize.y));
    float3 rgbNE = sampleCombined(uv + float2(texelSize.x, -texelSize.y));
    float3 rgbSW = sampleCombined(uv + float2(-texelSize.x, texelSize.y));
    float3 rgbSE = sampleCombined(uv + float2(texelSize.x, texelSize.y));
    float3 rgbM  = sampleCombined(uv);

    float lumaNW = dot(rgbNW, float3(0.299, 0.587, 0.114));
    float lumaNE = dot(rgbNE, float3(0.299, 0.587, 0.114));
    float lumaSW = dot(rgbSW, float3(0.299, 0.587, 0.114));
    float lumaSE = dot(rgbSE, float3(0.299, 0.587, 0.114));
    float lumaM  = dot(rgbM,  float3(0.299, 0.587, 0.114));

    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    float range = lumaMax - lumaMin;
    if (range < edgeThreshold) {
        return rgbM;
    }

    float2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * 0.5), 1e-6);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = saturate(dir * rcpDirMin) * float2(texelSize.x, texelSize.y);
    float3 rgbA = 0.5 * (
        sampleCombined(uv + dir * (1.0 / 3.0 - 0.5)) +
        sampleCombined(uv + dir * (2.0 / 3.0 - 0.5)));
    float3 rgbB = rgbA * 0.5 + 0.25 * (
        sampleCombined(uv + dir * -0.5) +
        sampleCombined(uv + dir * 0.5));
    float subpixelBlend = 0.75;
    if (dot(rgbB, float3(0.299, 0.587, 0.114)) < lumaMin || dot(rgbB, float3(0.299, 0.587, 0.114)) > lumaMax) {
        return lerp(rgbA, rgbB, 1.0 - subpixelBlend);
    } else {
        return lerp(rgbA, rgbB, subpixelBlend);
    }
}

float4 main(VSOutput input) : SV_Target {
    float4 sceneColor = sceneTexture.Sample(sampleSampler, input.fragTexCoord);
    float4 uiColor = uiTexture.Sample(sampleSampler, input.fragTexCoord);
    float4 textColor = textTexture.Sample(sampleSampler, input.fragTexCoord);
    float4 ssrColor = ssrTexture.Sample(sampleSampler, input.fragTexCoord);
    float aoColor = aoTexture.Sample(sampleSampler, input.fragTexCoord);
    float4 bloomColor = bloomTexture.Sample(sampleSampler, input.fragTexCoord);

    float3 combinedScene = sceneColor.rgb * aoColor + ssrColor.rgb * ssrColor.a + bloomColor.rgb;

    if ((pc.flag & 1) != 0) {
        combinedScene = FXAA(input.fragTexCoord);
    }
   
    float4 sceneUI = lerp(float4(combinedScene, sceneColor.a), uiColor, uiColor.a);
    return lerp(sceneUI, textColor, textColor.a);
}
