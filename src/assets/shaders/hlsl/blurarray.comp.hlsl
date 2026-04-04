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

[numthreads(16, 16, 1)]
void main(uint3 globalID : SV_DispatchThreadID) {
    uint width, height, layers;
    outputTexture.GetDimensions(width, height, layers);

    if (globalID.x >= width || globalID.y >= height || globalID.z >= layers) {
        return;
    }

    float2 uv = (float2(globalID.xy) + 0.5) / float2(width, height);
    float2 texelSize = 1.0 / float2(width, height);
    float2 minUV = texelSize * 0.5;
    float2 maxUV = 1.0 - minUV;
    float2 blurAxis = (pc.blurDirection == 0) ? float2(1.0, 0.0) : float2(0.0, 1.0);
    uint tapCount = min(pc.taps, 8u);
    uint activeLayers = min(layers, pc.layerCount);
    uint layer = globalID.z;

    if (activeLayers == 0 || layer >= activeLayers || layer >= lightsUBO.numPointLights.x) {
        outputTexture[int3(globalID.xy, layer)] = 1.0;
        return;
    }
    PointLight layerLight = lightsUBO.pointLights[layer];
    if (layerLight.shadowData.x != layer || layerLight.shadowData.y == 0) {
        outputTexture[int3(globalID.xy, layer)] = 1.0;
        return;
    }
    float centerSample = inputTexture.SampleLevel(sampleSampler, float3(uv, float(layer)), 0.0);
    if (tapCount == 0) {
        outputTexture[int3(globalID.xy, layer)] = centerSample;
        return;
    }
    if (centerSample <= fastBinaryShadowLow || centerSample >= fastBinaryShadowHigh) {
        float2 edgeOffset = blurAxis * texelSize;
        float2 uvPlus = clamp(uv + edgeOffset, minUV, maxUV);
        float2 uvMinus = clamp(uv - edgeOffset, minUV, maxUV);
        float plusSample = inputTexture.SampleLevel(sampleSampler, float3(uvPlus, float(layer)), 0.0);
        float minusSample = inputTexture.SampleLevel(sampleSampler, float3(uvMinus, float(layer)), 0.0);
        float localEdgeDiff = max(abs(plusSample - centerSample), abs(minusSample - centerSample));
        if (localEdgeDiff < fastEdgeThreshold) {
            outputTexture[int3(globalID.xy, layer)] = centerSample;
            return;
        }
    }
    float centerDepth = gBufferDepth.SampleLevel(sampleSampler, uv, 0.0);
    float centerLinearDepth = linearViewDepth(centerDepth, uv);
    float3 centerNormal = decodeNormal(gBufferNormal.SampleLevel(sampleSampler, uv, 0.0).xyz);
    float2 offset0 = blurAxis * (offsets[0] * texelSize);
    float2 uvPlus0 = uv + offset0;
    float2 uvMinus0 = uv - offset0;

    float plusWeight0 = 0.0;
    float minusWeight0 = 0.0;
    if (uvPlus0.x >= minUV.x && uvPlus0.x <= maxUV.x && uvPlus0.y >= minUV.y && uvPlus0.y <= maxUV.y) {
        plusWeight0 = weights[0] * bilateralGuideWeight(centerLinearDepth, centerNormal, uvPlus0);
    }
    if (uvMinus0.x >= minUV.x && uvMinus0.x <= maxUV.x && uvMinus0.y >= minUV.y && uvMinus0.y <= maxUV.y) {
        minusWeight0 = weights[0] * bilateralGuideWeight(centerLinearDepth, centerNormal, uvMinus0);
    }

    bool hasPlus0 = plusWeight0 > 0.0;
    bool hasMinus0 = minusWeight0 > 0.0;
    float plus0Sample = hasPlus0 ? inputTexture.SampleLevel(sampleSampler, float3(uvPlus0, float(layer)), 0.0) : centerSample;
    float minus0Sample = hasMinus0 ? inputTexture.SampleLevel(sampleSampler, float3(uvMinus0, float(layer)), 0.0) : centerSample;

    if ((hasPlus0 || hasMinus0) && (centerSample <= binaryShadowLow || centerSample >= binaryShadowHigh)) {
        float localDiff = 0.0;
        if (hasPlus0) localDiff = max(localDiff, abs(plus0Sample - centerSample));
        if (hasMinus0) localDiff = max(localDiff, abs(minus0Sample - centerSample));
        if (localDiff < flatShadowThreshold) {
            outputTexture[int3(globalID.xy, layer)] = centerSample;
            return;
        }
    }

    float sum = centerSample * centerWeight;
    float weightSum = centerWeight;

    if (hasPlus0) { sum += plus0Sample * plusWeight0; weightSum += plusWeight0; }
    if (hasMinus0) { sum += minus0Sample * minusWeight0; weightSum += minusWeight0; }

    [unroll]
    for (uint i = 1; i < 8; ++i) {
        if (i >= tapCount) break;
        float2 offset = blurAxis * (offsets[i] * texelSize);

        float2 uvP = uv + offset;
        if (uvP.x >= minUV.x && uvP.x <= maxUV.x && uvP.y >= minUV.y && uvP.y <= maxUV.y) {
            float w = weights[i] * bilateralGuideWeight(centerLinearDepth, centerNormal, uvP);
            if (w > 0.0) {
                sum += inputTexture.SampleLevel(sampleSampler, float3(uvP, float(layer)), 0.0) * w;
                weightSum += w;
            }
        }

        float2 uvM = uv - offset;
        if (uvM.x >= minUV.x && uvM.x <= maxUV.x && uvM.y >= minUV.y && uvM.y <= maxUV.y) {
            float w = weights[i] * bilateralGuideWeight(centerLinearDepth, centerNormal, uvM);
            if (w > 0.0) {
                sum += inputTexture.SampleLevel(sampleSampler, float3(uvM, float(layer)), 0.0) * w;
                weightSum += w;
            }
        }
    }

    outputTexture[int3(globalID.xy, layer)] = sum / weightSum;
}
