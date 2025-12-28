#pragma pack_matrix(row_major)

struct VSOutput {
    float4 gl_Position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    [[vk::location(1)]] float age : TEXCOORD1;
    [[vk::location(2)]] float4 color : COLOR;
};

struct ParticleData {
    float3 position;
    float age;
    float3 velocity;
    float lifetime;
    float4 color;
};

[[vk::binding(0)]] StructuredBuffer<ParticleData> particles;

struct PushConstants {
    float4x4 viewProj;
    float2 screenSize;
    float particleSize;
    float streakScale;
};

[[vk::push_constant]] PushConstants pc;

static const float2 offsets[4] = { {-1,-1}, {1,-1}, {-1,1}, {1,1} };

VSOutput main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID) {
    VSOutput output;
    ParticleData p = particles[instanceID];
    float4 clipPos = mul(float4(p.position, 1.0), pc.viewProj);
    float4 velClip = mul(float4(p.velocity, 0.0), pc.viewProj);    
    float velMag = length(velClip.xy);
    float2 stretchDir = velMag > 0.001 ? normalize(velClip.xy) : float2(0, 1);
    float2 perpDir = float2(-stretchDir.y, stretchDir.x);
    float stretchLen = length(p.velocity) * pc.streakScale;
    
    float2 localOffset = offsets[vertexID];
    float alongVel = localOffset.y * (pc.particleSize + stretchLen);
    float perpVel = localOffset.x * pc.particleSize * 0.3;
    
    float2 finalOffset = stretchDir * alongVel + perpDir * perpVel;
    
    clipPos.xy += finalOffset * clipPos.w;
    
    output.gl_Position = clipPos;
    output.uv = offsets[vertexID] * 0.5 + 0.5;
    output.age = p.age / p.lifetime;
    output.color = p.color;
    return output;
}