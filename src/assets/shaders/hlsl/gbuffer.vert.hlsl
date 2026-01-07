#pragma pack_matrix(row_major)

float3x3 inverse3x3(float3x3 m) {
    float3 c0 = m[0];
    float3 c1 = m[1];
    float3 c2 = m[2];
    
    float3 t0 = float3(c1.x, c2.x, c0.x);
    float3 t1 = float3(c1.y, c2.y, c0.y);
    float3 t2 = float3(c1.z, c2.z, c0.z);
    
    float3 m0 = t1 * c2.zxy - t2 * c1.zxy;
    float3 m1 = t2 * c0.zxy - t0 * c2.zxy;
    float3 m2 = t0 * c1.zxy - t1 * c0.zxy;
    
    float det = dot(c0, m0);
    float invDet = 1.0 / det;
    
    return float3x3(
        m0 * invDet,
        m1 * invDet,
        m2 * invDet
    );
}

struct VSOutput {
    float4 gl_Position : SV_Position;
    [[vk::location(0)]] float3 fragPosition : TEXCOORD0;
    [[vk::location(1)]] float3 fragNormal : TEXCOORD1;
    [[vk::location(2)]] float2 fragTexCoord : TEXCOORD2;
    [[vk::location(3)]] float3x3 fragTBN : TEXCOORD3;
};

struct VSInput {
    [[vk::location(0)]] float3 inPosition : POSITION;
    [[vk::location(1)]] float3 inNormal : NORMAL;
    [[vk::location(2)]] float2 inTexCoord : TEXCOORD0;
    [[vk::location(3)]] float4 inTangent : TANGENT;
    [[vk::location(4)]] float4 inJoints : BLENDINDICES;   // joint indices as floats
    [[vk::location(5)]] float4 inWeights : BLENDWEIGHT;   // joint weights (0.0-1.0)
};

struct PushConstants {
    float4x4 model;
    float4x4 view;
    float4x4 projection;
    float3 camPos;
    uint flags; // bit 0 = has skinning
};
[[vk::push_constant]] PushConstants pc;

struct JointMatricesUBO {
    float4x4 joints[128];
};
[[vk::binding(0)]] ConstantBuffer<JointMatricesUBO> jointUBO;

static const float4x4 IDENTITY = float4x4(
    1, 0, 0, 0,
    0, 1, 0, 0,
    0, 0, 1, 0,
    0, 0, 0, 1
);

VSOutput main(VSInput input) {
    float4x4 skinMatrix = IDENTITY;
    if ((pc.flags & 1) != 0) {
        uint4 jointIndices = uint4(input.inJoints);
        skinMatrix = jointUBO.joints[jointIndices.x] * input.inWeights.x +
                     jointUBO.joints[jointIndices.y] * input.inWeights.y +
                     jointUBO.joints[jointIndices.z] * input.inWeights.z +
                     jointUBO.joints[jointIndices.w] * input.inWeights.w;
    }
    float4 skinnedPosition = mul(float4(input.inPosition, 1.0), skinMatrix);
    float4 worldPos = mul(skinnedPosition, pc.model);
    float3x3 modelMatrix3 = (float3x3)pc.model;
    float3x3 skinMatrix3 = (float3x3)skinMatrix;
    float3x3 combinedMatrix = mul(skinMatrix3, modelMatrix3);
    float3x3 normalMatrix = transpose(combinedMatrix);
    float3 T = normalize(mul(input.inTangent.xyz, combinedMatrix));
    float3 N = normalize(mul(normalMatrix, input.inNormal));
    T = normalize(T - dot(T, N) * N);
    float3 B = cross(N, T) * input.inTangent.w;
    VSOutput output;
    output.gl_Position = mul(mul(worldPos, pc.view), pc.projection);
    output.fragPosition = worldPos.xyz;
    output.fragNormal = N;
    output.fragTexCoord = input.inTexCoord;
    output.fragTBN = float3x3(T, B, N);
    return output;
}