#pragma pack_matrix(row_major)

[[vk::binding(0)]]
Texture2D<float> depthTexture;

[[vk::binding(1)]]
Texture2D<float4> normalTexture;

[[vk::binding(2)]]
RWTexture2D<float> outputTexture;

[[vk::binding(3)]]
SamplerState sampleSampler;

struct PushConstants {
    float4x4 invProj;
    float4x4 proj;
    float4x4 view;
    uint flag;
    uint pad[3];
};

[[vk::push_constant]] PushConstants pc;

static const float RADIUS = 1.0;
static const float BIAS = 0.025;
static const float INTENSITY = 2.0;

static const float MAX_SCREEN_RADIUS_UV = 0.08;

static const uint GROUP_SIZE = 16u;
static const uint HALO = 8u;
static const uint TILE_SIZE = GROUP_SIZE + 2u * HALO; // 32

groupshared float3 tileViewPos[TILE_SIZE][TILE_SIZE];
groupshared float tileRawDepth[TILE_SIZE][TILE_SIZE];

static const float3 kernel[16] = {
    float3(0.5381, 0.1856, 0.4319), float3(0.1379, 0.2486, 0.4430),
    float3(0.3371, 0.5679, 0.1057), float3(-0.6999, -0.0451, 0.1019),
    float3(0.0689, -0.1598, 0.8547), float3(0.0560, 0.0069, 0.1843),
    float3(-0.0146, 0.1402, 0.0762), float3(0.0100, -0.1924, 0.2344),
    float3(-0.3577, -0.5301, 0.4358), float3(-0.3169, 0.1063, 0.1158),
    float3(0.0103, -0.5869, 0.2046), float3(-0.0897, -0.4940, 0.3287),
    float3(0.7119, -0.0154, 0.1918), float3(-0.0533, 0.0596, 0.5411),
    float3(0.0352, -0.0631, 0.5460), float3(-0.4776, 0.2847, 0.2271)
};

float3 reconstructPosition(float2 uv, float depth) {
    float4 ndc = float4(uv * 2.0 - 1.0, depth, 1.0);
    float4 viewPos = mul(ndc, pc.invProj);
    viewPos /= viewPos.w;
    return viewPos.xyz;
}

float3x3 createTBN(float3 normal, float2 uv) {
    float3 randomVec = normalize(float3(
        frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453),
        frac(sin(dot(uv, float2(93.9898, 67.345))) * 24634.6345),
        frac(sin(dot(uv, float2(45.332, 12.345))) * 56445.2345)
    ) * 2.0 - 1.0);
    float3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    float3 bitangent = cross(normal, tangent);
    return float3x3(tangent, bitangent, normal);
}

float rand(float2 uv) {
    return frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453);
}

struct TapFetch {
    float rawDepth;
    float3 viewPos;
    bool valid;
};

TapFetch fetchTap(float2 sampleUV, float2 screenSize, float2 invScreenSize,
                  int2 tileOriginPx, float2 tileCenterTexels) {
    TapFetch tap;
    tap.rawDepth = 1.0;
    tap.viewPos = float3(0.0, 0.0, 0.0);
    tap.valid = false;
    if (any(sampleUV < 0.0) || any(sampleUV > 1.0)) return tap;

    float2 sampleTexels = sampleUV * screenSize;
    int2 samplePx = int2(sampleTexels);
    int2 localPx = samplePx - tileOriginPx;

    if (all(localPx >= int2(0, 0)) && all(localPx < int2(int(TILE_SIZE), int(TILE_SIZE)))) {
        tap.rawDepth = tileRawDepth[localPx.y][localPx.x];
        tap.viewPos = tileViewPos[localPx.y][localPx.x];
        tap.valid = true;
        return tap;
    }

    float distTexels = length(sampleTexels - tileCenterTexels);
    float mipLevel = clamp(log2(max(distTexels / float(GROUP_SIZE), 1.0)), 0.0, 4.0);
    tap.rawDepth = depthTexture.SampleLevel(sampleSampler, sampleUV, mipLevel);
    tap.viewPos = reconstructPosition(sampleUV, tap.rawDepth);
    tap.valid = true;
    return tap;
}

float computeSSAO(float2 uv, float3 centerPos, float3 centerNormal, float effectiveRadius,
                  float2 screenSize, float2 invScreenSize, int2 tileOriginPx, float2 tileCenterTexels) {
    float3x3 TBN = createTBN(centerNormal, uv);
    float occlusion = 0.0;

    const int numSamples = 16;
    float4 projCenter = mul(float4(centerPos, 1.0), pc.proj);

    for (uint i = 0; i < numSamples; ++i) {
        float3 sampleVec = mul(kernel[i], TBN) * effectiveRadius;

        float4 projOffset = mul(float4(sampleVec, 1.0), pc.proj);
        float4 offsetPos = projCenter + projOffset;
        offsetPos /= offsetPos.w;
        float2 sampleUV = offsetPos.xy * 0.5 + 0.5;
        float expectedDepth = offsetPos.z;

        TapFetch tap = fetchTap(sampleUV, screenSize, invScreenSize, tileOriginPx, tileCenterTexels);
        if (!tap.valid) continue;
        if (tap.rawDepth >= 1.0 || tap.rawDepth <= 0.0) continue;

        float depthDiscontinuity = expectedDepth - tap.rawDepth;
        float edgeFade = 1.0;
        if (depthDiscontinuity > 0.002) {
            float distToActual = length(tap.viewPos - centerPos);
            edgeFade = saturate(1.0 - (distToActual - effectiveRadius) / effectiveRadius);
            if (distToActual > effectiveRadius * 1.5) continue;
        }

        float3 v = tap.viewPos - centerPos;
        float dist = length(v);
        float rangeCheck = smoothstep(0.0, 1.0, effectiveRadius / (dist + 1e-5));
        float horizon = dot(normalize(v), centerNormal);

        if (horizon > BIAS) {
            occlusion += horizon * rangeCheck * edgeFade;
        }
    }

    occlusion = occlusion / float(numSamples);
    return 1.0 - saturate(occlusion * INTENSITY);
}

float computeGTAO(float2 uv, float3 centerPos, float3 centerNormal, float effectiveRadius,
                  float2 screenSize, float2 invScreenSize, int2 tileOriginPx, float2 tileCenterTexels) {
    float3x3 TBN = createTBN(centerNormal, uv);
    float occlusion = 0.0;

    const int numDirections = 8;
    const int numSteps = 6;
    const float stepSize = effectiveRadius / float(numSteps);

    const float TWOPI = 6.28318530718;
    float randAngle = rand(uv) * TWOPI;

    for (int d = 0; d < numDirections; ++d) {
        float angle = randAngle + (float(d) / float(numDirections)) * TWOPI;
        float3 dir = normalize(cos(angle) * TBN[0] + sin(angle) * TBN[1]);

        float maxHorizon = 0.0;

        for (int s = 1; s <= numSteps; ++s) {
            float3 samplePos = centerPos + dir * (float(s) * stepSize);

            float4 offsetPos = mul(float4(samplePos, 1.0), pc.proj);
            offsetPos /= offsetPos.w;
            float2 sampleUV = offsetPos.xy * 0.5 + 0.5;
            float expectedDepth = offsetPos.z;

            TapFetch tap = fetchTap(sampleUV, screenSize, invScreenSize, tileOriginPx, tileCenterTexels);
            if (!tap.valid) continue;
            if (tap.rawDepth >= 1.0 || tap.rawDepth <= 0.0) continue;

            float depthDiscontinuity = expectedDepth - tap.rawDepth;
            float edgeFade = 1.0;
            if (depthDiscontinuity > 0.002) {
                float distToActual = length(tap.viewPos - centerPos);
                edgeFade = saturate(1.0 - (distToActual - effectiveRadius) / effectiveRadius);
                if (distToActual > effectiveRadius * 1.5) continue;
            }

            float3 v = tap.viewPos - centerPos;
            float dist = length(v);
            float rangeCheck = smoothstep(0.0, 1.0, effectiveRadius / (dist + 1e-5));
            float horizon = dot(normalize(v), centerNormal);

            if (horizon > BIAS) {
                maxHorizon = max(maxHorizon, (horizon - BIAS) * rangeCheck * edgeFade);
            }
        }

        occlusion += maxHorizon;
    }

    occlusion = occlusion / float(numDirections);
    return 1.0 - saturate(occlusion * INTENSITY);
}

[numthreads(16, 16, 1)]
void main(uint3 globalID : SV_DispatchThreadID,
          uint3 groupID : SV_GroupID,
          uint3 localID : SV_GroupThreadID) {
    uint width, height;
    outputTexture.GetDimensions(width, height);
    const float2 screenSize = float2(width, height);
    const float2 invScreenSize = 1.0 / screenSize;

    const int2 groupBase = int2(groupID.xy) * int(GROUP_SIZE);
    const int2 tileOriginPx = groupBase - int(HALO);
    const float2 tileCenterTexels = float2(groupBase) + float(GROUP_SIZE) * 0.5;
    const uint localLinear = localID.y * GROUP_SIZE + localID.x;

    [unroll]
    for (uint load = 0u; load < 4u; ++load) {
        uint idx = localLinear + load * (GROUP_SIZE * GROUP_SIZE);
        uint r = idx / TILE_SIZE;
        uint c = idx % TILE_SIZE;
        int2 samplePx = tileOriginPx + int2(int(c), int(r));
        int2 clampedPx = clamp(samplePx, int2(0, 0), int2(int(width) - 1, int(height) - 1));
        float2 sampleUV = (float2(clampedPx) + 0.5) * invScreenSize;
        float rawDepth = depthTexture.SampleLevel(sampleSampler, sampleUV, 0.0);
        tileRawDepth[r][c] = rawDepth;
        tileViewPos[r][c] = reconstructPosition(sampleUV, rawDepth);
    }

    GroupMemoryBarrierWithGroupSync();

    if (globalID.x >= width || globalID.y >= height) return;

    const uint2 centerLocal = uint2(localID.x + HALO, localID.y + HALO);
    const float centerDepth = tileRawDepth[centerLocal.y][centerLocal.x];

    if (centerDepth >= 1.0 || pc.flag == 0u) {
        outputTexture[globalID.xy] = 1.0;
        return;
    }

    const float2 uv = (float2(globalID.xy) + 0.5) * invScreenSize;
    const float3 centerPos = tileViewPos[centerLocal.y][centerLocal.x];
    const float3 worldNormal = normalize(normalTexture.SampleLevel(sampleSampler, uv, 0.0).xyz * 2.0 - 1.0);
    const float3 centerNormal = normalize(mul(float4(worldNormal, 0.0), pc.view).xyz);

    const float focal = pc.proj[1][1];
    const float screenRadiusUV = 0.5 * focal * RADIUS / max(abs(centerPos.z), 1e-4);
    const float radiusScale = min(1.0, MAX_SCREEN_RADIUS_UV / max(screenRadiusUV, 1e-6));
    const float effectiveRadius = RADIUS * radiusScale;

    float occlusion = 1.0;
    if (pc.flag == 1u) {
        occlusion = computeSSAO(uv, centerPos, centerNormal, effectiveRadius,
                                screenSize, invScreenSize, tileOriginPx, tileCenterTexels);
    } else if (pc.flag == 2u) {
        occlusion = computeGTAO(uv, centerPos, centerNormal, effectiveRadius,
                                screenSize, invScreenSize, tileOriginPx, tileCenterTexels);
    }

    outputTexture[globalID.xy] = occlusion;
}
