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
    float3 prevPosition;
    float lifetime;
    float3 prevPrevPosition;
    float type;
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

float3 bezierTangent(float3 p0, float3 p1, float3 p2, float t) {
    return 2.0 * (1.0 - t) * (p1 - p0) + 2.0 * t * (p2 - p1);
}

float hash(uint seed) {
    seed = seed * 747796405u + 2891336453u;
    seed = ((seed >> ((seed >> 28u) + 4u)) ^ seed) * 277803737u;
    return float((seed >> 22u) ^ seed) / 4294967295.0;
}

VSOutput main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID) {
    VSOutput output;
    ParticleData p = particles[instanceID];
    
    if (p.type > 0.5) {
        float3 startPos = p.position;
        float3 dir = p.prevPosition;
        float3 endPos = startPos + dir;
        
        float3 lineDir = normalize(dir);
        float3 worldUp = float3(0, 1, 0);
        float3 perpWorld = cross(lineDir, worldUp);
        if (length(perpWorld) < 0.001) {
            perpWorld = cross(lineDir, float3(1, 0, 0));
        }
        perpWorld = normalize(perpWorld);
        
        float width = 0.03;
        
        float2 localOffset = offsets[vertexID];
        
        float t = localOffset.y * 0.5 + 0.5;
        float3 worldPos = lerp(startPos, endPos, t);
        worldPos += perpWorld * localOffset.x * width;
        float4 posClip = mul(float4(worldPos, 1.0), pc.viewProj);
        
        posClip.z -= 0.001 * posClip.w;
        
        output.gl_Position = posClip;
        output.uv = float2(localOffset.x * 0.5 + 0.5, t);
        output.age = p.age / p.lifetime;
        output.color = p.color;
        output.color.a = -1.0; // marks this as a trail
        return output;
    }

    float4 clipPos = mul(float4(p.position, 1.0), pc.viewProj);
    float3 trailDir = bezierTangent(p.prevPrevPosition, p.prevPosition, p.position, 1.0);
    float trailLen = length(trailDir);
    if (trailLen < 0.0001) {
        trailDir = p.position - p.prevPosition;
        trailLen = length(trailDir);
    }
    float4 trailClip = mul(float4(trailDir, 0.0), pc.viewProj);
    float trailMag = length(trailClip.xy);
    float2 stretchDir = trailMag > 0.001 ? normalize(trailClip.xy) : float2(0, 1);
    float2 perpDir = float2(-stretchDir.y, stretchDir.x);
    float stretchLen = trailLen * pc.streakScale;
    
    float sizeVariation = hash(instanceID) * 1.6 + 0.2; // range [0.2, 1.8]
    float particleSize = pc.particleSize * sizeVariation;
    
    float2 localOffset = offsets[vertexID];
    float alongVel = localOffset.y * (particleSize + stretchLen);
    float perpVel = localOffset.x * particleSize * 0.3;
    
    float2 finalOffset = stretchDir * alongVel + perpDir * perpVel;
    
    clipPos.xy += finalOffset * clipPos.w;
    
    output.gl_Position = clipPos;
    output.uv = offsets[vertexID] * 0.5 + 0.5;
    output.age = p.age / p.lifetime;
    output.color = p.color;
    return output;
}