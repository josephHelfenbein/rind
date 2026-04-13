#pragma pack_matrix(row_major)

struct SHOutput {
    float4 coeffs[9];
};

[[vk::binding(0)]]
RWStructuredBuffer<SHOutput> outputSH;

[[vk::binding(1)]]
StructuredBuffer<SHOutput> partialSH;

[[vk::binding(2)]]
StructuredBuffer<uint> activeProbeIndices;

struct PushConstants {
    uint cubemapSize;
    uint activeProbeCount;
    uint pad0;
    uint pad1;
};
[[vk::push_constant]] PushConstants pc;

groupshared float3 sharedReduce[64][9];

[numthreads(64, 1, 1)]
void main(
    uint3 globalID : SV_DispatchThreadID,
    uint3 groupID : SV_GroupID,
    uint3 localID : SV_GroupThreadID,
    uint localIndex : SV_GroupIndex
) {
    const uint probeLocalIndex = groupID.x;
    if (probeLocalIndex >= pc.activeProbeCount || probeLocalIndex >= 32u) {
        return;
    }

    const uint probeIndex = activeProbeIndices[probeLocalIndex];
    if (probeIndex >= 32u) {
        return;
    }

    const uint cubemapSize = max(pc.cubemapSize, 1u);
    const uint numGroupsX = (cubemapSize + 7u) / 8u;
    const uint numGroupsY = (cubemapSize + 7u) / 8u;
    const uint workgroupsPerProbe = numGroupsX * numGroupsY * 6u;
    const uint baseIndex = probeLocalIndex * workgroupsPerProbe;

    [unroll]
    for (int initCoeff = 0; initCoeff < 9; ++initCoeff) {
        sharedReduce[localIndex][initCoeff] = float3(0.0f, 0.0f, 0.0f);
    }

    for (uint idx = localIndex; idx < workgroupsPerProbe; idx += 64u) {
        SHOutput partial = partialSH[baseIndex + idx];
        [unroll]
        for (int accumCoeff = 0; accumCoeff < 9; ++accumCoeff) {
            sharedReduce[localIndex][accumCoeff] += partial.coeffs[accumCoeff].xyz;
        }
    }
    GroupMemoryBarrierWithGroupSync();

    for (uint stride = 32u; stride > 0u; stride >>= 1u) {
        if (localIndex < stride) {
            [unroll]
            for (int reduceCoeff = 0; reduceCoeff < 9; ++reduceCoeff) {
                sharedReduce[localIndex][reduceCoeff] += sharedReduce[localIndex + stride][reduceCoeff];
            }
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (localIndex != 0u) {
        return;
    }

    [unroll]
    for (int coeff = 0; coeff < 9; ++coeff) {
        outputSH[probeIndex].coeffs[coeff] = float4(sharedReduce[0][coeff], 0.0f);
    }
}
