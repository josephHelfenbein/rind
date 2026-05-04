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
static const uint kCullChunk = 64u;

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

groupshared uint gAliveCount;
groupshared float4 gCenterExtent[kCullChunk]; // xy=centerNdc, z=halfExtent, w=ageFade
groupshared float3 gColor[kCullChunk];

[numthreads(8, 8, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID,
          uint3 localID : SV_GroupThreadID) {
    const uint cubemapSize = max(pc.cubemapSize, 1u);
    const uint activeLayerCount = pc.activeProbeCount * 6u;

    if (dispatchId.z >= activeLayerCount) return;

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
    const float  probeRadius   = max(irradianceProbes.probes[mappedProbeIndex].position.w, kProbeNear);

    const bool inBounds = (dispatchId.x < cubemapSize && dispatchId.y < cubemapSize);
    const float2 pixelNdc = ((float2(dispatchId.xy) + 0.5f) / float(cubemapSize)) * 2.0f - 1.0f;

    float4 outColor = float4(0.0f, 0.0f, 0.0f, 0.0f);
    if (inBounds) {
        const float3 sampleDir = cubemapTexelToDirection(mappedFace, pixelNdc.x, pixelNdc.y);
        outColor = bakedCubemaps[mappedProbeIndex].SampleLevel(cubemapSampler, sampleDir, 0.0f);
    }

    const uint threadIndex = localID.y * 8u + localID.x;
    const uint particleCount = pc.particleCount;

    [loop]
    for (uint base = 0u; base < particleCount; base += kCullChunk) {
        if (threadIndex == 0u) {
            gAliveCount = 0u;
        }
        GroupMemoryBarrierWithGroupSync();

        const uint pi = base + threadIndex;
        if (pi < particleCount) {
            ParticleData p = particles[pi];
            float invLifetime = 1.0f / max(p.prevPosition.w, 1e-4f);
            float ageFade = 1.0f - saturate(p.position.w * invLifetime);
            if (ageFade > 0.0f) {
                float2 centerNdc;
                float depth;
                if (projectToMappedFace(p.position.xyz - probePosition, mappedFace, centerNdc, depth)
                    && depth <= probeRadius) {
                    float halfExtent = p.color.w * pc.particleSize * sqrt(20.0f / max(depth, kProbeNear));
                    if (halfExtent > 0.0f) {
                        uint slot;
                        InterlockedAdd(gAliveCount, 1u, slot);
                        gCenterExtent[slot] = float4(centerNdc, halfExtent, ageFade);
                        gColor[slot] = p.color.rgb;
                    }
                }
            }
        }
        GroupMemoryBarrierWithGroupSync();

        const uint n = gAliveCount;
        for (uint k = 0u; k < n; ++k) {
            const float4 ce = gCenterExtent[k];
            float2 localCoord = (pixelNdc - ce.xy) / ce.z;
            if (any(abs(localCoord) > 1.0f)) continue;

            float topBrighten = saturate(localCoord.y * 0.5f + 0.5f);
            float3 baseColor = gColor[k];
            float3 colorTop = saturate(baseColor + float3(0.5f, 0.5f, 0.5f));
            outColor.rgb += lerp(baseColor, colorTop, topBrighten);
            outColor.a += ce.w;
        }
    }

    if (inBounds) {
        const int3 pixelCoord = int3(dispatchId.xy, mappedFace);
        outputCubemaps[mappedProbeIndex][pixelCoord] = outColor;
    }
}
