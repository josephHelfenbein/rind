#pragma pack_matrix(row_major)

struct SHOutput {
    float4 coeffs[9];
};

[[vk::binding(0)]]
RWStructuredBuffer<SHOutput> outputSH;

[[vk::combinedImageSampler]]
[[vk::binding(1)]]
TextureCube<float4> inputCubemap;

[[vk::combinedImageSampler]]
[[vk::binding(1)]]
SamplerState cubemapSampler;

struct PushConstants {
    uint cubemapSize;
    uint pad0;
    uint pad1;
    uint pad2;
};
[[vk::push_constant]] PushConstants pc;

groupshared float3 sharedSH[9][64];

float shBasis(float3 n, int index) {
    float x = n.z, y = n.x, z = n.y;
    
    const float k01 = 0.282095f;
    const float k02 = 0.488603f;
    const float k03 = 1.092548f;
    const float k04 = 0.315392f;
    const float k05 = 0.546274f;
    
    switch (index) {
        case 0: return k01;
        case 1: return -k02 * y;
        case 2: return k02 * z;
        case 3: return -k02 * x;
        case 4: return k03 * x * y;
        case 5: return -k03 * y * z;
        case 6: return k04 * (3.0f * z * z - 1.0f);
        case 7: return -k03 * x * z;
        case 8: return k05 * (x * x - y * y);
        default: return 0.0f;
    }
}

float3 cubemapTexelToDirection(uint face, float u, float v) {
    float3 dir;
    switch (face) {
        case 0: dir = float3( 1.0f, -v, -u); break; // +X
        case 1: dir = float3(-1.0f, -v,  u); break; // -X
        case 2: dir = float3(u,  1.0f,  v); break;  // +Y
        case 3: dir = float3(u, -1.0f, -v); break;  // -Y
        case 4: dir = float3(u, -v,  1.0f); break;  // +Z
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
void main(uint3 globalID : SV_DispatchThreadID, 
          uint3 groupID : SV_GroupID,
          uint3 localID : SV_GroupThreadID,
          uint localIndex : SV_GroupIndex) {
    
    uint x = globalID.x;
    uint y = globalID.y;
    uint face = groupID.z;
    
    uint cubemapSize = pc.cubemapSize;
    float invSize = 1.0f / float(cubemapSize);
    
    for (int i = 0; i < 9; ++i) {
        sharedSH[i][localIndex] = float3(0, 0, 0);
    }
    GroupMemoryBarrierWithGroupSync();
    
    if (x < cubemapSize && y < cubemapSize && face < 6) {
        float u = (float(x) + 0.5f) * invSize * 2.0f - 1.0f;
        float v = (float(y) + 0.5f) * invSize * 2.0f - 1.0f;
        
        float3 dir = cubemapTexelToDirection(face, u, v);
        
        float4 color = inputCubemap.SampleLevel(cubemapSampler, dir, 0);
        
        if (!any(isnan(color.rgb))) {
            float solidAngle = texelSolidAngle(u, v, invSize);
            
            for (int i = 0; i < 9; ++i) {
                float basis = shBasis(dir, i);
                sharedSH[i][localIndex] = color.rgb * basis * solidAngle;
            }
        }
    }
    
    GroupMemoryBarrierWithGroupSync();
    
    for (uint stride = 32; stride > 0; stride >>= 1) {
        if (localIndex < stride) {
            for (int i = 0; i < 9; ++i) {
                sharedSH[i][localIndex] += sharedSH[i][localIndex + stride];
            }
        }
        GroupMemoryBarrierWithGroupSync();
    }
    
    if (localIndex == 0) {
        uint numGroupsX = (cubemapSize + 7) / 8;
        uint numGroupsY = (cubemapSize + 7) / 8;
        uint workgroupIndex = face * numGroupsX * numGroupsY + groupID.y * numGroupsX + groupID.x;
        
        for (int i = 0; i < 9; ++i) {
            outputSH[workgroupIndex].coeffs[i] = float4(sharedSH[i][0], 0.0f);
        }
    }
}
