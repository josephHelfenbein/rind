#pragma pack_matrix(row_major)

[[vk::binding(0)]]
Texture2DArray<float> inputTexture;

[[vk::binding(1)]]
Texture2D<float> gBufferDepth;

[[vk::binding(2)]]
Texture2D<float4> gBufferNormal;

[[vk::binding(3)]]
RWTexture2DArray<float> outputTexture;

struct PointLight {
    float4 positionRadius;
    float4 colorIntensity;
    float4 shadowParams;
    uint4 shadowData;
};

struct LightsUBO {
    PointLight pointLights[64];
    uint4 numPointLights;
};
[[vk::binding(4)]]
ConstantBuffer<LightsUBO> lightsUBO;

[[vk::binding(5)]]
SamplerState sampleSampler;

struct PushConstants {
    float4x4 invProj;
    uint blurDirection; // 0 for horizontal, 1 for vertical
    uint taps; // number of taps to use, up to 8
    uint layerCount;
};
[[vk::push_constant]] PushConstants pc;

static const float depthSigmaScale = 0.015;
static const float normalPower = 32.0;

static const float centerWeight = 1.0;

static const float offsets[8] = {
    1.47659, 3.44544, 5.41503, 7.38516,
    9.35572, 11.32793, 13.30069, 15.27504
};

static const float weights[8] = {
    1.85173, 1.36155, 0.78249, 0.35148,
    0.12349, 0.03387, 0.00727, 0.00122
};

static const float flatShadowThreshold = 0.02;
static const float binaryShadowLow = 0.1;
static const float binaryShadowHigh = 0.9;
static const float fastBinaryShadowLow = 0.02;
static const float fastBinaryShadowHigh = 0.98;
static const float fastEdgeThreshold = 0.03;

static const uint GROUP_SIZE = 16u;
static const uint MAX_HALO = 16u;
static const uint TILE_SIZE = GROUP_SIZE + 2u * MAX_HALO;

groupshared float sharedShadow[GROUP_SIZE][TILE_SIZE];
groupshared float sharedLinearDepth[GROUP_SIZE][TILE_SIZE];
groupshared float3 sharedNormal[GROUP_SIZE][TILE_SIZE];

float linearViewDepth(float depth, float2 uv) {
    float4 ndc = float4(uv * 2.0 - 1.0, depth, 1.0);
    float4 viewPos = mul(ndc, pc.invProj);
    viewPos /= max(abs(viewPos.w), 1e-6);
    return abs(viewPos.z);
}

float3 decodeNormal(float3 packedNormal) {
    float3 rawNormal = packedNormal * 2.0 - 1.0;
    float normalLen = length(rawNormal);
    return (normalLen > 0.001) ? (rawNormal / normalLen) : float3(0.0, 1.0, 0.0);
}

float bilateralWeightLDS(float centerLinearDepth, float3 centerNormal,
                         float sampleLinearDepth, float3 sampleNormal) {
    float depthDiff = abs(sampleLinearDepth - centerLinearDepth);
    float depthSigma = max(0.05, centerLinearDepth * depthSigmaScale);
    float depthWeight = exp(-(depthDiff * depthDiff) / max(2.0 * depthSigma * depthSigma, 1e-6));
    float nDot = saturate(dot(centerNormal, sampleNormal));
    float normalWeight = pow(nDot, normalPower);
    return depthWeight * normalWeight;
}

float shadowTap(uint row, uint centerCol, float offsetTexels) {
    int off0 = int(floor(offsetTexels));
    float t = offsetTexels - float(off0);
    uint c0 = uint(int(centerCol) + off0);
    uint c1 = c0 + 1u;
    return lerp(sharedShadow[row][c0], sharedShadow[row][c1], t);
}

uint guideCol(uint centerCol, float offsetTexels) {
    return uint(int(centerCol) + int(floor(offsetTexels + 0.5)));
}

[numthreads(16, 16, 1)]
void main(uint3 globalID : SV_DispatchThreadID,
          uint3 groupID : SV_GroupID,
          uint3 localID : SV_GroupThreadID) {
    uint width, height, layers;
    outputTexture.GetDimensions(width, height, layers);

    const uint layer = groupID.z;
    const uint activeLayers = min(layers, pc.layerCount);

    if (activeLayers == 0u || layer >= activeLayers || layer >= lightsUBO.numPointLights.x) {
        if (globalID.x < width && globalID.y < height) {
            outputTexture[int3(globalID.xy, layer)] = 1.0;
        }
        return;
    }
    PointLight layerLight = lightsUBO.pointLights[layer];
    if (layerLight.shadowData.x != layer || layerLight.shadowData.y == 0u) {
        if (globalID.x < width && globalID.y < height) {
            outputTexture[int3(globalID.xy, layer)] = 1.0;
        }
        return;
    }

    const bool horizontal = (pc.blurDirection == 0u);
    const float2 texelSize = 1.0 / float2(width, height);
    const int2 groupBase = int2(groupID.xy) * int(GROUP_SIZE);
    const uint localLinear = localID.y * GROUP_SIZE + localID.x;

    [unroll]
    for (uint load = 0u; load < 3u; ++load) {
        uint idx = localLinear + load * (GROUP_SIZE * GROUP_SIZE);
        uint r = idx / TILE_SIZE;
        uint c = idx % TILE_SIZE;

        int2 samplePx = horizontal
            ? int2(groupBase.x - int(MAX_HALO) + int(c), groupBase.y + int(r))
            : int2(groupBase.x + int(r), groupBase.y - int(MAX_HALO) + int(c));
        samplePx.x = clamp(samplePx.x, 0, int(width) - 1);
        samplePx.y = clamp(samplePx.y, 0, int(height) - 1);
        float2 sampleUV = (float2(samplePx) + 0.5) * texelSize;

        sharedShadow[r][c] = inputTexture.SampleLevel(sampleSampler, float3(sampleUV, float(layer)), 0.0);
        float rawDepth = gBufferDepth.SampleLevel(sampleSampler, sampleUV, 0.0);
        sharedLinearDepth[r][c] = linearViewDepth(rawDepth, sampleUV);
        sharedNormal[r][c] = decodeNormal(gBufferNormal.SampleLevel(sampleSampler, sampleUV, 0.0).xyz);
    }

    GroupMemoryBarrierWithGroupSync();

    if (globalID.x >= width || globalID.y >= height) return;

    const uint row = horizontal ? localID.y : localID.x;
    const uint centerCol = (horizontal ? localID.x : localID.y) + MAX_HALO;

    const float centerSample = sharedShadow[row][centerCol];
    const uint tapCount = min(pc.taps, 8u);

    if (tapCount == 0u) {
        outputTexture[int3(globalID.xy, layer)] = centerSample;
        return;
    }

    if (centerSample <= fastBinaryShadowLow || centerSample >= fastBinaryShadowHigh) {
        float plusSample = sharedShadow[row][centerCol + 1u];
        float minusSample = sharedShadow[row][centerCol - 1u];
        float localEdgeDiff = max(abs(plusSample - centerSample), abs(minusSample - centerSample));
        if (localEdgeDiff < fastEdgeThreshold) {
            outputTexture[int3(globalID.xy, layer)] = centerSample;
            return;
        }
    }

    const float centerLinearDepth = sharedLinearDepth[row][centerCol];
    const float3 centerNormal = sharedNormal[row][centerCol];

    const float off0 = offsets[0];
    const uint gcP0 = guideCol(centerCol, off0);
    const uint gcN0 = guideCol(centerCol, -off0);
    const float plusWeight0 = weights[0] * bilateralWeightLDS(centerLinearDepth, centerNormal,
                                                              sharedLinearDepth[row][gcP0],
                                                              sharedNormal[row][gcP0]);
    const float minusWeight0 = weights[0] * bilateralWeightLDS(centerLinearDepth, centerNormal,
                                                               sharedLinearDepth[row][gcN0],
                                                               sharedNormal[row][gcN0]);
    const float plus0Sample = shadowTap(row, centerCol, off0);
    const float minus0Sample = shadowTap(row, centerCol, -off0);

    if (centerSample <= binaryShadowLow || centerSample >= binaryShadowHigh) {
        float localDiff = max(abs(plus0Sample - centerSample), abs(minus0Sample - centerSample));
        if (localDiff < flatShadowThreshold) {
            outputTexture[int3(globalID.xy, layer)] = centerSample;
            return;
        }
    }

    float sum = centerSample * centerWeight;
    float weightSum = centerWeight;
    sum += plus0Sample * plusWeight0 + minus0Sample * minusWeight0;
    weightSum += plusWeight0 + minusWeight0;

    [unroll]
    for (uint i = 1u; i < 8u; ++i) {
        if (i >= tapCount) break;
        float off = offsets[i];
        uint gcP = guideCol(centerCol, off);
        uint gcN = guideCol(centerCol, -off);
        float wP = weights[i] * bilateralWeightLDS(centerLinearDepth, centerNormal,
                                                   sharedLinearDepth[row][gcP],
                                                   sharedNormal[row][gcP]);
        float wN = weights[i] * bilateralWeightLDS(centerLinearDepth, centerNormal,
                                                   sharedLinearDepth[row][gcN],
                                                   sharedNormal[row][gcN]);
        sum += shadowTap(row, centerCol, off) * wP;
        sum += shadowTap(row, centerCol, -off) * wN;
        weightSum += wP + wN;
    }

    outputTexture[int3(globalID.xy, layer)] = sum / weightSum;
}
