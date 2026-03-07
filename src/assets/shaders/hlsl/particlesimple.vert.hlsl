#pragma pack_matrix(row_major)

struct VSOutput {
    float4 gl_Position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR;
};

struct ParticleData {
    float4 position; // w = age
    float4 prevPosition; // w = lifetime
    float4 prevPrevPosition; // w = type
    float4 color; // w = size
};

[[vk::binding(0)]] StructuredBuffer<ParticleData> particles;

struct PushConstants {
    float4x4 viewProj;
    float particleSize;
};

[[vk::push_constant]] PushConstants pc;

static const float2 offsets[4] = { {-1,-1}, {1,-1}, {-1,1}, {1,1} };

VSOutput main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID) {
    VSOutput output;
    ParticleData p = particles[instanceID];

    float age = p.position.w;
    float lifetime = p.prevPosition.w;
    float size = p.color.w;

    float4 clipPos = mul(float4(p.position.xyz, 1.0), pc.viewProj);

    float particleSize = size * pc.particleSize * sqrt(20.0 / max(clipPos.w, 0.01));
    float2 localOffset = offsets[vertexID];
    
    clipPos.xy += localOffset * particleSize * clipPos.w;

    float ageFade = 1.0 - saturate(age / lifetime);

    output.gl_Position = clipPos;
    output.color = float4(lerp(p.color.rgb, saturate(p.color.rgb + float3(0.5, 0.5, 0.5)), step(2, vertexID)), ageFade);
    return output;
}
