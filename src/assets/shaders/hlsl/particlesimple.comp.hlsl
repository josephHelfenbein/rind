#pragma pack_matrix(row_major)

struct ParticleData {
    float4 position; // w = age
    float4 prevPosition; // w = lifetime
    float4 prevPrevPosition; // w = type
    float4 color; // w = size
};

struct IrradianceProbeData {
    float4 position;
    float4 shCoeffs[9];
};

static const uint kMaxIrradianceProbes = 64u;

struct IrradianceProbesUBO {
    IrradianceProbeData probes[kMaxIrradianceProbes];
    uint4 numProbes;
};

[[vk::binding(0)]]
StructuredBuffer<ParticleData> particles;

[[vk::binding(1)]]
RWTexture2DArray<float4> outputCubemaps[kMaxIrradianceProbes];

[[vk::binding(2)]]
StructuredBuffer<uint> activeProbeIndices;

[[vk::binding(3)]]
ConstantBuffer<IrradianceProbesUBO> irradianceProbes;

[[vk::binding(4)]]
TextureCube<float4> bakedCubemaps[kMaxIrradianceProbes];

[[vk::binding(5)]]
SamplerState cubemapSampler;

struct PushConstants {
    float4 probePosition;
    float particleSize;
    uint particleCount;
    uint cubemapSize;
    uint activeProbeCount;
    uint layerBase;
    uint mappingOffset;
    uint pad;
};
[[vk::push_constant]] PushConstants pc;

static const float kEpsilon = 1e-5f;
static const float kProbeNear = 0.1f;

float3 cubemapTexelToDirection(uint face, float u, float v) {
    float3 dir;
    switch (face) {
        case 0: dir = float3(1.0f, -v, -u); break; // +X
        case 1: dir = float3(-1.0f, -v, u); break; // -X
        case 2: dir = float3(u, 1.0f, v); break; // +Y
        case 3: dir = float3(u, -1.0f, -v); break; // -Y
        case 4: dir = float3(u, -v, 1.0f); break; // +Z
        case 5: dir = float3(-u, -v, -1.0f); break; // -Z
        default: dir = float3(0.0f, 0.0f, 1.0f); break;
    }
    return normalize(dir);
}

bool projectToMappedFace(float3 rel, uint mappedFace, out float2 centerNdc, out float depth) {
    switch (mappedFace) {
        case 0u: // +X
            depth = rel.x;
            if (depth <= kProbeNear) return false;
            centerNdc = float2(-rel.z, -rel.y) / max(depth, kEpsilon);
            return true;
        case 1u: // -X
            depth = -rel.x;
            if (depth <= kProbeNear) return false;
            centerNdc = float2(rel.z, -rel.y) / max(depth, kEpsilon);
            return true;
        case 2u: // +Y
            depth = rel.y;
            if (depth <= kProbeNear) return false;
            centerNdc = float2(rel.x, rel.z) / max(depth, kEpsilon);
            return true;
        case 3u: // -Y
            depth = -rel.y;
            if (depth <= kProbeNear) return false;
            centerNdc = float2(rel.x, -rel.z) / max(depth, kEpsilon);
            return true;
        case 4u: // +Z
            depth = rel.z;
            if (depth <= kProbeNear) return false;
            centerNdc = float2(rel.x, -rel.y) / max(depth, kEpsilon);
            return true;
        case 5u: // -Z
            depth = -rel.z;
            if (depth <= kProbeNear) return false;
            centerNdc = float2(-rel.x, -rel.y) / max(depth, kEpsilon);
            return true;
        default:
            break;
    }

    depth = 0.0f;
    centerNdc = float2(0.0f, 0.0f);
    return false;
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID) {
    const uint cubemapSize = max(pc.cubemapSize, 1u);
    const uint activeLayerCount = pc.activeProbeCount * 6u;
    if (dispatchId.x >= cubemapSize || dispatchId.y >= cubemapSize || dispatchId.z >= activeLayerCount) {
        return;
    }

    const uint dispatchLayer = pc.mappingOffset + pc.layerBase + dispatchId.z;
    const uint mappedProbeLocalIndex = dispatchLayer / 6u;
    const uint mappedFace = dispatchLayer % 6u;
    if (mappedProbeLocalIndex >= pc.activeProbeCount || mappedProbeLocalIndex >= kMaxIrradianceProbes) {
        return;
    }
    const uint mappedProbeIndex = activeProbeIndices[mappedProbeLocalIndex];
    if (mappedProbeIndex >= kMaxIrradianceProbes) {
        return;
    }
    const float3 probePosition = irradianceProbes.probes[mappedProbeIndex].position.xyz;

    const int3 pixelCoord = int3(dispatchId.xy, mappedFace);

    const float2 pixelNdc = ((float2(dispatchId.xy) + 0.5f) / float(cubemapSize)) * 2.0f - 1.0f;
    const float3 sampleDir = cubemapTexelToDirection(mappedFace, pixelNdc.x, pixelNdc.y);
    float4 outColor = bakedCubemaps[mappedProbeIndex].SampleLevel(cubemapSampler, sampleDir, 0.0f);

    const uint particleCount = pc.particleCount;
    [loop]
    for (uint i = 0u; i < particleCount; ++i) {
        ParticleData p = particles[i];
        float lifetime = max(p.prevPosition.w, 1e-4f);
        float ageFade = 1.0f - saturate(p.position.w / lifetime);
        if (ageFade <= 0.0f) {
            continue;
        }

        float2 centerNdc = float2(0.0f, 0.0f);
        float depth = 0.0f;
        if (!projectToMappedFace(p.position.xyz - probePosition, mappedFace, centerNdc, depth)) {
            continue;
        }

        float probeRadius = max(irradianceProbes.probes[mappedProbeIndex].position.w, kProbeNear);
        if (depth > probeRadius) {
            continue;
        }

        float size = p.color.w;
        float particleHalfExtent = size * pc.particleSize * sqrt(20.0f / max(depth, kProbeNear));
        if (particleHalfExtent <= 0.0f) {
            continue;
        }

        float2 localCoord = (pixelNdc - centerNdc) / particleHalfExtent;
        if (abs(localCoord.x) > 1.0f || abs(localCoord.y) > 1.0f) {
            continue;
        }

        float topBrighten = saturate(localCoord.y * 0.5f + 0.5f);
        float3 colorTop = saturate(p.color.rgb + float3(0.5f, 0.5f, 0.5f));
        float3 particleColor = lerp(p.color.rgb, colorTop, topBrighten);

        outColor.rgb += particleColor;
        outColor.a += ageFade;
    }

    outputCubemaps[mappedProbeIndex][pixelCoord] = outColor;
}
