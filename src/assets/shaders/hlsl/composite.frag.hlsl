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
Texture2D<float4> textTexture;

[[vk::binding(3)]]
Texture2D<float4> smaaTexture;

[[vk::binding(4)]]
SamplerState sampleSampler;

float3 FXAA(float2 uv) {
    const float edgeThreshold = 0.08;
    float2 texelSize = pc.invScreenSize;
    float3 rgbNW = sceneTexture.Sample(sampleSampler, saturate(uv + float2(-texelSize.x, -texelSize.y))).rgb;
    float3 rgbNE = sceneTexture.Sample(sampleSampler, saturate(uv + float2(texelSize.x, -texelSize.y))).rgb;
    float3 rgbSW = sceneTexture.Sample(sampleSampler, saturate(uv + float2(-texelSize.x, texelSize.y))).rgb;
    float3 rgbSE = sceneTexture.Sample(sampleSampler, saturate(uv + float2(texelSize.x, texelSize.y))).rgb;
    float3 rgbM  = sceneTexture.Sample(sampleSampler, uv).rgb;

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
        sceneTexture.Sample(sampleSampler, saturate(uv + dir * (1.0 / 3.0 - 0.5))).rgb +
        sceneTexture.Sample(sampleSampler, saturate(uv + dir * (2.0 / 3.0 - 0.5))).rgb);
    float3 rgbB = rgbA * 0.5 + 0.25 * (
        sceneTexture.Sample(sampleSampler, saturate(uv + dir * -0.5)).rgb +
        sceneTexture.Sample(sampleSampler, saturate(uv + dir * 0.5)).rgb);
    float subpixelBlend = 0.75;
    if (dot(rgbB, float3(0.299, 0.587, 0.114)) < lumaMin || dot(rgbB, float3(0.299, 0.587, 0.114)) > lumaMax) {
        return lerp(rgbA, rgbB, 1.0 - subpixelBlend);
    } else {
        return lerp(rgbA, rgbB, subpixelBlend);
    }
}

float4 main(VSOutput input) : SV_Target {
    float4 uiColor = uiTexture.Sample(sampleSampler, input.fragTexCoord);
    float4 textColor = textTexture.Sample(sampleSampler, input.fragTexCoord);
    
    float3 sceneColor;
    if (pc.flag == 2) {
        sceneColor = smaaTexture.Sample(sampleSampler, input.fragTexCoord).rgb;
    } else if (pc.flag == 1) {
        sceneColor = FXAA(input.fragTexCoord);
    } else {
        sceneColor = sceneTexture.Sample(sampleSampler, input.fragTexCoord).rgb;
    }
   
    float4 sceneUI = lerp(float4(sceneColor, 1.0), uiColor, uiColor.a);
    return lerp(sceneUI, textColor, textColor.a);
}
