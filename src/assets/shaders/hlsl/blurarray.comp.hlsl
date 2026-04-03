#pragma pack_matrix(row_major)

[[vk::binding(0)]]
Texture2DArray<float> inputTexture;

[[vk::binding(1)]]
Texture2D<float> gBufferDepth;

[[vk::binding(2)]]
Texture2D<float4> gBufferNormal;

[[vk::binding(3)]]
RWTexture2DArray<float> outputTexture;

[[vk::binding(4)]]
SamplerState sampleSampler;

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
[[vk::binding(5)]]
ConstantBuffer<LightsUBO> lightsUBO;

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
static const uint INVALID_SHADOW_INDEX = 0xFFFFFFFF;

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

float bilateralGuideWeight(float centerLinearDepth, float3 centerNormal, float2 sampleUV) {
    float sampleDepth = gBufferDepth.SampleLevel(sampleSampler, sampleUV, 0.0);
    float sampleLinearDepth = linearViewDepth(sampleDepth, sampleUV);
    float depthDiff = abs(sampleLinearDepth - centerLinearDepth);
    float depthSigma = max(0.05, centerLinearDepth * depthSigmaScale);
    float depthWeight = exp(-(depthDiff * depthDiff) / max(2.0 * depthSigma * depthSigma, 1e-6));

    float3 sampleNormal = decodeNormal(gBufferNormal.SampleLevel(sampleSampler, sampleUV, 0.0).xyz);
    float nDot = saturate(dot(centerNormal, sampleNormal));
    float normalWeight = pow(nDot, normalPower);

    return depthWeight * normalWeight;
}

groupshared uint gs_needGuide[64];
groupshared float gs_plusWeights[64 * 8];
groupshared float gs_minusWeights[64 * 8];
groupshared float2 gs_plusUV[64 * 8];
groupshared float2 gs_minusUV[64 * 8];

[numthreads(8, 8, 1)]
void main(uint3 globalID : SV_DispatchThreadID, uint3 localID : SV_GroupThreadID) {
    uint width, height, layers;
    outputTexture.GetDimensions(width, height, layers);
    uint pixelIndex = localID.y * 8u + localID.x;
    uint pixelBase = pixelIndex * 8u;
    if (localID.z == 0) {
        gs_needGuide[pixelIndex] = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    bool inBounds = globalID.x < width && globalID.y < height && globalID.z < layers;
    float2 uv = (float2(globalID.xy) + 0.5) / float2(width, height);
    float2 texelSize = 1.0 / float2(width, height);
    float2 minUV = texelSize * 0.5;
    float2 maxUV = 1.0 - minUV;
    float2 blurAxis = (pc.blurDirection == 0) ? float2(1.0, 0.0) : float2(0.0, 1.0);
    uint tapCount = min(pc.taps, 8u);
    uint activeLayers = min(layers, pc.layerCount);
    uint layer = globalID.z;

    bool processLayer = false;
    bool writeNow = false;
    float writeValue = 1.0;
    float centerSample = 1.0;

    if (inBounds && activeLayers > 0 && layer < activeLayers) {
        if (layer >= lightsUBO.numPointLights.x) {
            writeNow = true;
            writeValue = 1.0;
        } else {
            PointLight layerLight = lightsUBO.pointLights[layer];
            if (layerLight.shadowData.x != layer || layerLight.shadowData.y == 0) {
                writeNow = true;
                writeValue = 1.0;
            } else {
                centerSample = inputTexture.SampleLevel(sampleSampler, float3(uv, float(layer)), 0.0);
                if (tapCount == 0) {
                    writeNow = true;
                    writeValue = centerSample;
                } else {
                    if (centerSample <= fastBinaryShadowLow || centerSample >= fastBinaryShadowHigh) {
                        float2 edgeOffset = blurAxis * texelSize;
                        float2 uvPlus = clamp(uv + edgeOffset, minUV, maxUV);
                        float2 uvMinus = clamp(uv - edgeOffset, minUV, maxUV);
                        float plusSample = inputTexture.SampleLevel(sampleSampler, float3(uvPlus, float(layer)), 0.0);
                        float minusSample = inputTexture.SampleLevel(sampleSampler, float3(uvMinus, float(layer)), 0.0);
                        float localEdgeDiff = max(abs(plusSample - centerSample), abs(minusSample - centerSample));
                        if (localEdgeDiff < fastEdgeThreshold) {
                            writeNow = true;
                            writeValue = centerSample;
                        }
                    }
                    if (!writeNow) {
                        processLayer = true;
                        InterlockedOr(gs_needGuide[pixelIndex], 1u);
                    }
                }
            }
        }
    }

    GroupMemoryBarrierWithGroupSync();

    if (localID.z == 0 && gs_needGuide[pixelIndex] != 0u && globalID.x < width && globalID.y < height) {
        float2 sharedUV = (float2(globalID.xy) + 0.5) / float2(width, height);
        float centerDepth = gBufferDepth.SampleLevel(sampleSampler, sharedUV, 0.0);
        float centerLinearDepth = linearViewDepth(centerDepth, sharedUV);
        float3 centerNormal = decodeNormal(gBufferNormal.SampleLevel(sampleSampler, sharedUV, 0.0).xyz);

        [unroll]
        for (uint i = 0; i < 8; ++i) {
            uint idx = pixelBase + i;
            gs_plusWeights[idx] = 0.0;
            gs_minusWeights[idx] = 0.0;
            gs_plusUV[idx] = sharedUV;
            gs_minusUV[idx] = sharedUV;

            if (i >= tapCount) {
                continue;
            }

            float2 offset = blurAxis * (offsets[i] * texelSize);
            float2 uvPlus = sharedUV + offset;
            if (uvPlus.x >= minUV.x && uvPlus.x <= maxUV.x && uvPlus.y >= minUV.y && uvPlus.y <= maxUV.y) {
                gs_plusUV[idx] = uvPlus;
                gs_plusWeights[idx] = weights[i] * bilateralGuideWeight(centerLinearDepth, centerNormal, uvPlus);
            }
            float2 uvMinus = sharedUV - offset;
            if (uvMinus.x >= minUV.x && uvMinus.x <= maxUV.x && uvMinus.y >= minUV.y && uvMinus.y <= maxUV.y) {
                gs_minusUV[idx] = uvMinus;
                gs_minusWeights[idx] = weights[i] * bilateralGuideWeight(centerLinearDepth, centerNormal, uvMinus);
            }
        }
    }

    GroupMemoryBarrierWithGroupSync();

    if (!inBounds) {
        return;
    }

    if (writeNow) {
        outputTexture[int3(globalID.xy, layer)] = writeValue;
        return;
    }

    if (!processLayer) {
        return;
    }

    float plusWeight0 = gs_plusWeights[pixelBase + 0u];
    float minusWeight0 = gs_minusWeights[pixelBase + 0u];
    bool hasPlus0 = plusWeight0 > 0.0;
    bool hasMinus0 = minusWeight0 > 0.0;
    float plus0Sample = hasPlus0 ? inputTexture.SampleLevel(sampleSampler, float3(gs_plusUV[pixelBase + 0u], float(layer)), 0.0) : centerSample;
    float minus0Sample = hasMinus0 ? inputTexture.SampleLevel(sampleSampler, float3(gs_minusUV[pixelBase + 0u], float(layer)), 0.0) : centerSample;

    if ((hasPlus0 || hasMinus0) && (centerSample <= binaryShadowLow || centerSample >= binaryShadowHigh)) {
        float localDiff = 0.0;
        if (hasPlus0) {
            localDiff = max(localDiff, abs(plus0Sample - centerSample));
        }
        if (hasMinus0) {
            localDiff = max(localDiff, abs(minus0Sample - centerSample));
        }
        if (localDiff < flatShadowThreshold) {
            outputTexture[int3(globalID.xy, layer)] = centerSample;
            return;
        }
    }

    float sum = centerSample * centerWeight;
    float weightSum = centerWeight;

    if (hasPlus0) {
        sum += plus0Sample * plusWeight0;
        weightSum += plusWeight0;
    }
    if (hasMinus0) {
        sum += minus0Sample * minusWeight0;
        weightSum += minusWeight0;
    }

    [unroll]
    for (uint i = 1; i < 8; ++i) {
        if (i >= tapCount) {
            break;
        }
        uint idx = pixelBase + i;
        float plusWeight = gs_plusWeights[idx];
        float minusWeight = gs_minusWeights[idx];
        if (plusWeight > 0.0) {
            sum += inputTexture.SampleLevel(sampleSampler, float3(gs_plusUV[idx], float(layer)), 0.0) * plusWeight;
            weightSum += plusWeight;
        }
        if (minusWeight > 0.0) {
            sum += inputTexture.SampleLevel(sampleSampler, float3(gs_minusUV[idx], float(layer)), 0.0) * minusWeight;
            weightSum += minusWeight;
        }
    }

    outputTexture[int3(globalID.xy, layer)] = sum / weightSum;
}
