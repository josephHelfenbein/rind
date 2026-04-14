#pragma pack_matrix(row_major)

struct SHOutput {
    float4 coeffs[9];
};

static const uint kMaxIrradianceProbes = 64u;

[[vk::binding(0)]]
RWStructuredBuffer<SHOutput> outputSH;

[[vk::binding(1)]]
TextureCube<float4> inputCubemaps[kMaxIrradianceProbes];

[[vk::binding(2)]]
SamplerState cubemapSampler;

[[vk::binding(3)]]
StructuredBuffer<uint> activeProbeIndices;

struct PushConstants {
    uint cubemapSize;
    uint activeProbeCount;
    uint pad0;
    uint pad1;
};
[[vk::push_constant]] PushConstants pc;

groupshared float3 sharedSH[64][9];

float3 cubemapTexelToDirection(uint face, float u, float v) {
    float3 dir;
    switch (face) {
        case 0: dir = float3(1.0f, -v, -u); break; // +X
        case 1: dir = float3(-1.0f, -v, u); break; // -X
        case 2: dir = float3(u, 1.0f, v); break; // +Y
        case 3: dir = float3(u, -1.0f, -v); break; // -Y
        case 4: dir = float3(u, -v, 1.0f); break; // +Z
        case 5: dir = float3(-u, -v, -1.0f); break; // -Z
        default: dir = float3(0, 0, 1); break;
    }
    return normalize(dir);
}

float texelSolidAngle(float u, float v, float invSize) {
    float x0 = u - invSize;
    float y0 = v - invSize;
    float x1 = u + invSize;
    float y1 = v + invSize;
    return atan2(x0 * y0, sqrt(x0 * x0 + y0 * y0 + 1.0f)) -
           atan2(x0 * y1, sqrt(x0 * x0 + y1 * y1 + 1.0f)) -
           atan2(x1 * y0, sqrt(x1 * x1 + y0 * y0 + 1.0f)) +
           atan2(x1 * y1, sqrt(x1 * x1 + y1 * y1 + 1.0f));
}

[numthreads(8, 8, 1)]
void main(
    uint3 globalID : SV_DispatchThreadID,
    uint3 groupID : SV_GroupID,
    uint3 localID : SV_GroupThreadID,
    uint localIndex : SV_GroupIndex
) {
    const uint cubemapSize = max(pc.cubemapSize, 1u);
    const uint layer = groupID.z;
    const uint probeLocalIndex = layer / 6u;
    const uint face = layer % 6u;

    if (probeLocalIndex >= pc.activeProbeCount || probeLocalIndex >= kMaxIrradianceProbes) {
        return;
    }

    const uint probeIndex = activeProbeIndices[probeLocalIndex];
    if (probeIndex >= kMaxIrradianceProbes) {
        return;
    }

    const float invSize = 1.0f / float(cubemapSize);
    [unroll]
    for (int i = 0; i < 9; ++i) {
        sharedSH[localIndex][i] = float3(0.0f, 0.0f, 0.0f);
    }
    GroupMemoryBarrierWithGroupSync();

    const uint x = globalID.x;
    const uint y = globalID.y;
    if (x < cubemapSize && y < cubemapSize && face < 6u) {
        float u = (float(x) + 0.5f) * invSize * 2.0f - 1.0f;
        float v = (float(y) + 0.5f) * invSize * 2.0f - 1.0f;

        float3 dir = cubemapTexelToDirection(face, u, v);
        float4 color = inputCubemaps[probeIndex].SampleLevel(cubemapSampler, dir, 0.0f);
        if (!any(isnan(color.rgb))) {
            float solidAngle = texelSolidAngle(u, v, invSize);

            float sx = dir.z;
            float sy = dir.x;
            float sz = dir.y;

            const float k01 = 0.282095f;
            const float k02 = 0.488603f;
            const float k03 = 1.092548f;
            const float k04 = 0.315392f;
            const float k05 = 0.546274f;

            float basis[9] = {
                k01,
                -k02 * sy,
                k02 * sz,
                -k02 * sx,
                k03 * sx * sy,
                -k03 * sy * sz,
                k04 * (3.0f * sz * sz - 1.0f),
                -k03 * sx * sz,
                k05 * (sx * sx - sy * sy)
            };

            [unroll]
            for (int coeff = 0; coeff < 9; ++coeff) {
                sharedSH[localIndex][coeff] = color.rgb * basis[coeff] * solidAngle;
            }
        }
    }
    GroupMemoryBarrierWithGroupSync();

    [unroll]
    for (int coeff = 0; coeff < 9; ++coeff) {
        sharedSH[localIndex][coeff] = WaveActiveSum(sharedSH[localIndex][coeff]);
    }
    GroupMemoryBarrierWithGroupSync();

    if (localIndex != 0u) {
        return;
    }

    const uint waveSize = WaveGetLaneCount();
    const uint numWaves = (64u + waveSize - 1u) / waveSize;
    const uint numGroupsX = (cubemapSize + 7u) / 8u;
    const uint numGroupsY = (cubemapSize + 7u) / 8u;
    const uint workgroupsPerProbe = numGroupsX * numGroupsY * 6u;
    const uint workgroupIndexInProbe = face * numGroupsX * numGroupsY + groupID.y * numGroupsX + groupID.x;
    const uint outputIndex = probeLocalIndex * workgroupsPerProbe + workgroupIndexInProbe;

    [unroll]
    for (int coeff = 0; coeff < 9; ++coeff) {
        float3 total = sharedSH[0][coeff];
        for (uint w = 1u; w < numWaves; ++w) {
            total += sharedSH[w * waveSize][coeff];
        }
        outputSH[outputIndex].coeffs[coeff] = float4(total, 0.0f);
    }
}
