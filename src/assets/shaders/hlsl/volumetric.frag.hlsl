#pragma pack_matrix(row_major)

struct VSOutput {
    [[vk::location(0)]] float3 worldPos : TEXCOORD0;
    [[vk::location(1)]] nointerpolation uint instanceID : TEXCOORD1;
};

struct VolumetricData {
    float4x4 model;
    float4x4 invModel;
    float4 color; // rgb = tint, w = density
    float age;
    float lifetime;
    float2 pad;
};

[[vk::binding(0)]] StructuredBuffer<VolumetricData> volumes;
[[vk::binding(1)]] Texture2D<float> depthTexture;
[[vk::binding(2)]] SamplerState depthSampler;

struct PushConstants {
    float4x4 viewProj;
    float3 camPos;
    float pad;
};
[[vk::push_constant]] PushConstants pc;

static const int COARSE_STEPS = 8;
static const int FINE_STEPS = 4;
static const float THRESHOLD = 0.01;

float hash3(float3 p) {
    p = frac(p * float3(443.897, 441.423, 437.195));
    p += dot(p, p.yzx + 19.19);
    return frac((p.x + p.y) * p.z);
}

float vnoise(float3 p) {
    float3 i = floor(p);
    float3 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    return lerp(
        lerp(
            lerp(hash3(i), hash3(i + float3(1, 0, 0)), f.x),
            lerp(hash3(i + float3(0, 1, 0)), hash3(i + float3(1, 1, 0)), f.x),
            f.y
        ),
        lerp(
            lerp(hash3(i + float3(0, 0, 1)), hash3(i + float3(1, 0, 1)), f.x),
            lerp(hash3(i + float3(0, 1, 1)), hash3(i + float3(1, 1, 1)), f.x),
            f.y
        ),
        f.z
    );
}

float fbm(float3 p) {
    return vnoise(p) * 0.500 + vnoise(p * 2.0) * 0.250 + vnoise(p * 4.0) * 0.125 + vnoise(p * 8.0) * 0.063;
}

float sampleDensity(float3 localPos, float age, float lifetime) {
    float t = age / max(lifetime, 0.0001);
    float radial = exp(-dot(localPos, localPos) * 12.0);
    float3 noiseCoord = localPos * 3.5 + float3(0.0, age * 0.4, age * 0.15);
    float n = fbm(noiseCoord);
    float ageFade = pow(saturate(1.0 - t), 1.8);
    return radial * n * ageFade;
}

float4 main(VSOutput input, float4 fragCoord : SV_Position) : SV_Target {
    VolumetricData vol = volumes[input.instanceID];

    float3 rayOrigin = pc.camPos;
    float3 rayDir = normalize(input.worldPos - rayOrigin);
    float3 localOrigin = mul(float4(rayOrigin, 1.0), vol.invModel).xyz;
    float3 localDir = mul(float4(rayDir, 0.0), vol.invModel).xyz;

    float3 tMin = (-0.5 - localOrigin) / localDir;
    float3 tMax = (0.5 - localOrigin) / localDir;
    float3 t1 = min(tMin, tMax);
    float3 t2 = max(tMin, tMax);
    float tNear = max(max(t1.x, t1.y), t1.z);
    float tFar = min(min(t2.x, t2.y), t2.z);

    tNear = max(tNear, 0.0);
    if (tFar <= tNear) discard;

    float totalLen = tFar - tNear;
    float coarseSize = totalLen / float(COARSE_STEPS);
    float fineSize = coarseSize / float(FINE_STEPS);
    float extinction = vol.color.w;
    float3 tint = vol.color.rgb;

    float4 accum = float4(0.0, 0.0, 0.0, 0.0);

    for (int i = 0; i < COARSE_STEPS; i++) {
        if (accum.a >= 0.99) break;
        float tCoarse = tNear + (float(i) + 0.5) * coarseSize;
        float3 localMid = localOrigin + tCoarse * localDir;

        float3 worldMid = mul(float4(localMid, 1.0), vol.model).xyz;
        float4 clipMid = mul(float4(worldMid, 1.0), pc.viewProj);
        float2 depthUV = saturate(clipMid.xy / clipMid.w * 0.5 + 0.5);
        float sceneD = depthTexture.SampleLevel(depthSampler, depthUV, 0);
        float sampleD = clipMid.z / clipMid.w;
        if (sceneD < 1.0 && sampleD > sceneD) break;

        float coarseDensity = sampleDensity(localMid, vol.age, vol.lifetime);
        if (coarseDensity <= THRESHOLD) continue;

        float tFineBase = tNear + float(i) * coarseSize;
        for (int j = 0; j < FINE_STEPS; j++) {
            if (accum.a >= 0.99) break;
            float tFine = tFineBase + (float(j) + 0.5) * fineSize;
            float3 localFine = localOrigin + tFine * localDir;
            float density = sampleDensity(localFine, vol.age, vol.lifetime);
            float alpha = 1.0 - exp(-density * extinction * fineSize);

            accum.rgb += (1.0 - accum.a) * alpha * tint;
            accum.a += (1.0 - accum.a) * alpha;
        }
    }

    if (accum.a < 0.001) discard;
    return accum;
}
