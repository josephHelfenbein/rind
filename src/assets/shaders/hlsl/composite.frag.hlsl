struct VSOutput {
    [[vk::location(0)]] float2 fragTexCoord : TEXCOORD0;
};

struct PushConstans {
    float2 invScreenSize;
    uint flag; // bit 0 = enable fxaa
    uint pad;
};

[[vk::push_constant]] PushConstans pc;

[[vk::binding(0)]]
Texture2D<float4> sceneTexture;

[[vk::binding(1)]]
Texture2D<float4> uiTexture;

[[vk::binding(2)]]
Texture2D<float4> textTexture;

[[vk::binding(3)]]
Texture2D<float4> ssrTexture;

[[vk::binding(4)]]
SamplerState sampleSampler;

float3 ACESFilm(float3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float3 FXAA(Texture2D<float4> tex, float2 uv) {
    float2 texelSize = pc.invScreenSize;
    float3 rgbNW = tex.Sample(sampleSampler, uv + float2(-texelSize.x, -texelSize.y)).rgb;
    float3 rgbNE = tex.Sample(sampleSampler, uv + float2(texelSize.x, -texelSize.y)).rgb;
    float3 rgbSW = tex.Sample(sampleSampler, uv + float2(-texelSize.x, texelSize.y)).rgb;
    float3 rgbSE = tex.Sample(sampleSampler, uv + float2(texelSize.x, texelSize.y)).rgb;
    float3 rgbM  = tex.Sample(sampleSampler, uv).rgb;

    float lumaNW = dot(rgbNW, float3(0.299, 0.587, 0.114));
    float lumaNE = dot(rgbNE, float3(0.299, 0.587, 0.114));
    float lumaSW = dot(rgbSW, float3(0.299, 0.587, 0.114));
    float lumaSE = dot(rgbSE, float3(0.299, 0.587, 0.114));
    float lumaM  = dot(rgbM,  float3(0.299, 0.587, 0.114));

    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    float range = lumaMax - lumaMin;
    if (range < 0.1) {
        return rgbM;
    }

    float2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * 0.5), 1e-6);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = saturate(dir * rcpDirMin) * float2(texelSize.x, texelSize.y);
    float3 rgbA = 0.5 * (
        tex.Sample(sampleSampler, uv + dir * (1.0 / 3.0 - 0.5)).rgb +
        tex.Sample(sampleSampler, uv + dir * (2.0 / 3.0 - 0.5)).rgb);
    float3 rgbB = rgbA * 0.5 + 0.25 * (
        tex.Sample(sampleSampler, uv + dir * -0.5).rgb +
        tex.Sample(sampleSampler, uv + dir * 0.5).rgb);
    if (dot(rgbB, float3(0.299, 0.587, 0.114)) < lumaMin || dot(rgbB, float3(0.299, 0.587, 0.114)) > lumaMax) {
        return rgbA;
    } else {
        return rgbB;
    }
}

float4 main(VSOutput input) : SV_Target {
    float4 sceneColor = sceneTexture.Sample(sampleSampler, input.fragTexCoord);
    float4 uiColor = uiTexture.Sample(sampleSampler, input.fragTexCoord);
    float4 textColor = textTexture.Sample(sampleSampler, input.fragTexCoord);
    float4 ssrColor = ssrTexture.Sample(sampleSampler, input.fragTexCoord);

    float3 tonemapped = ACESFilm(sceneColor.rgb + ssrColor.rgb * ssrColor.a);

    if ((pc.flag & 1) != 0) {
        tonemapped = FXAA(sceneTexture, input.fragTexCoord);
    }

    float4 sceneUI = lerp(float4(tonemapped, sceneColor.a), uiColor, uiColor.a);
    return lerp(sceneUI, textColor, textColor.a);
}
