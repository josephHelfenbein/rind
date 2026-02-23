struct VSOutput {
    [[vk::location(0)]] float2 fragTexCoord : TEXCOORD0;
};

struct PushConstants {
    float2 invScreenSize;
    uint flags;
    uint pad;
};

[[vk::push_constant]] PushConstants pc;

[[vk::binding(0)]]
Texture2D<float4> edgesTexture;

[[vk::binding(1)]]
Texture2D<float4> areaTexture;

[[vk::binding(2)]]
Texture2D<float4> searchTexture;

[[vk::binding(3)]]
SamplerState pointSampler;

[[vk::binding(4)]]
SamplerState linearSampler;

static const int SMAA_MAX_SEARCH_STEPS = 8;
static const float SMAA_AREATEX_MAX_DISTANCE = 16.0;
static const float2 SMAA_AREATEX_PIXEL_SIZE = 1.0 / float2(160.0, 560.0);

float2 SMAAArea(float2 dist, float e1, float e2) {
    float2 texcoord = SMAA_AREATEX_MAX_DISTANCE * round(4.0 * float2(e1, e2)) + dist;
    texcoord = SMAA_AREATEX_PIXEL_SIZE * texcoord + 0.5 * SMAA_AREATEX_PIXEL_SIZE;
    return areaTexture.SampleLevel(linearSampler, texcoord, 0).rg;
}

float SearchXLeft(float2 texcoord) {
    [unroll(8)]
    for (int i = 0; i < SMAA_MAX_SEARCH_STEPS; i++) {
        float2 e = edgesTexture.SampleLevel(pointSampler, texcoord, 0).rg;
        if (e.g < 0.5 || e.r > 0.5) break;
        texcoord.x -= 2.0 * pc.invScreenSize.x;
    }
    return texcoord.x;
}

float SearchXRight(float2 texcoord) {
    [unroll(8)]
    for (int i = 0; i < SMAA_MAX_SEARCH_STEPS; i++) {
        float2 e = edgesTexture.SampleLevel(pointSampler, texcoord, 0).rg;
        if (e.g < 0.5 || e.r > 0.5) break;
        texcoord.x += 2.0 * pc.invScreenSize.x;
    }
    return texcoord.x;
}

float SearchYUp(float2 texcoord) {
    [unroll(8)]
    for (int i = 0; i < SMAA_MAX_SEARCH_STEPS; i++) {
        float2 e = edgesTexture.SampleLevel(pointSampler, texcoord, 0).rg;
        if (e.r < 0.5 || e.g > 0.5) break;
        texcoord.y -= 2.0 * pc.invScreenSize.y;
    }
    return texcoord.y;
}

float SearchYDown(float2 texcoord) {
    [unroll(8)]
    for (int i = 0; i < SMAA_MAX_SEARCH_STEPS; i++) {
        float2 e = edgesTexture.SampleLevel(pointSampler, texcoord, 0).rg;
        if (e.r < 0.5 || e.g > 0.5) break;
        texcoord.y += 2.0 * pc.invScreenSize.y;
    }
    return texcoord.y;
}

float4 main(VSOutput input) : SV_Target {
    float2 texcoord = input.fragTexCoord;
    float2 e = edgesTexture.SampleLevel(pointSampler, texcoord, 0).rg;
    float4 weights = float4(0, 0, 0, 0);

    if (e.g > 0.5) {
        float leftEnd = SearchXLeft(texcoord - float2(pc.invScreenSize.x, 0));
        float rightEnd = SearchXRight(texcoord + float2(pc.invScreenSize.x, 0));

        float2 dist;
        dist.x = (texcoord.x - leftEnd) / pc.invScreenSize.x;
        dist.y = (rightEnd - texcoord.x) / pc.invScreenSize.x;
        dist = clamp(dist, 0, SMAA_AREATEX_MAX_DISTANCE - 1.0);
        dist = sqrt(dist);

        float e1 = edgesTexture.SampleLevel(pointSampler, float2(leftEnd, texcoord.y + pc.invScreenSize.y), 0).r;
        float e2 = edgesTexture.SampleLevel(pointSampler, float2(rightEnd + pc.invScreenSize.x, texcoord.y + pc.invScreenSize.y), 0).r;

        weights.rg = SMAAArea(dist, e1, e2);
    }

    if (e.r > 0.5) {
        float topEnd = SearchYUp(texcoord - float2(0, pc.invScreenSize.y));
        float bottomEnd = SearchYDown(texcoord + float2(0, pc.invScreenSize.y));

        float2 dist;
        dist.x = (texcoord.y - topEnd) / pc.invScreenSize.y;
        dist.y = (bottomEnd - texcoord.y) / pc.invScreenSize.y;
        dist = clamp(dist, 0, SMAA_AREATEX_MAX_DISTANCE - 1.0);
        dist = sqrt(dist);

        float e1 = edgesTexture.SampleLevel(pointSampler, float2(texcoord.x + pc.invScreenSize.x, topEnd), 0).g;
        float e2 = edgesTexture.SampleLevel(pointSampler, float2(texcoord.x + pc.invScreenSize.x, bottomEnd + pc.invScreenSize.y), 0).g;

        weights.ba = SMAAArea(dist, e1, e2);
    }

    return weights;
}
