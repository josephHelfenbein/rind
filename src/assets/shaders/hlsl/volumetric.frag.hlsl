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

static const float MAX_STEP_SCALE = 4.0;
static const float THRESHOLD = 0.01;
static const float LOD_NEAR = 2.0;
static const float LOD_FAR = 10.0;

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

float fbm(float3 p, int octaves) {
    float v = 0.0;
    float amp = 0.500;
    float freq = 1.0;
    if (octaves >= 1) { v += vnoise(p * freq) * amp; amp *= 0.5; freq *= 2.0; }
    if (octaves >= 2) { v += vnoise(p * freq) * amp; amp *= 0.5; freq *= 2.0; }
    if (octaves >= 3) { v += vnoise(p * freq) * amp; amp *= 0.5; freq *= 2.0; }
    if (octaves >= 4) { v += vnoise(p * freq) * amp; amp *= 0.5; freq *= 2.0; }
    if (octaves >= 5) { v += vnoise(p * freq) * amp; }
    return v;
}

float sampleDensity(float3 localPos, float age, float ageFade, int fbmOctaves) {
    float radial = exp(-dot(localPos, localPos) * 12.0);
    float3 noiseCoord = localPos * 6.0 + float3(0.0, age * 0.4, age * 0.15);
    float n = fbm(noiseCoord, fbmOctaves);
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

    float3 entryWorld = mul(float4(localOrigin + tNear * localDir, 1.0), vol.model).xyz;
    float4 entryClip = mul(float4(entryWorld, 1.0), pc.viewProj);
    float entryClipD = entryClip.z / entryClip.w;
    float2 entryUV = saturate(entryClip.xy / entryClip.w * 0.5 + 0.5);
    float preSceneD = depthTexture.SampleLevel(depthSampler, entryUV, 0);

    if (preSceneD < 1.0) {
        if (entryClipD >= preSceneD) discard; // fully occluded

        float3 exitWorld = mul(float4(localOrigin + tFar * localDir, 1.0), vol.model).xyz;
        float4 exitClip  = mul(float4(exitWorld, 1.0), pc.viewProj);
        float  exitClipD = exitClip.z / exitClip.w;

        if (exitClipD > preSceneD) { // partially occluded
            float depthFrac = saturate((preSceneD - entryClipD) / (exitClipD - entryClipD));
            tFar = lerp(tNear, tFar, depthFrac);
        }
    }

    float3 volCenter = vol.model[3].xyz;
    float camDist = length(volCenter - pc.camPos);
    float lodT = saturate((camDist - LOD_NEAR) / (LOD_FAR - LOD_NEAR));
    int maxSteps = (int) lerp(64.0, 8.0,  lodT);
    float baseDivs = lerp(24.0, 6.0, lodT);
    int fbmOctaves = (int) lerp(5.0, 2.0, lodT);
    bool doRefinement = lodT < 0.7;

    float totalLen = tFar - tNear;
    float baseStep = totalLen / baseDivs;
    float maxStep = baseStep * MAX_STEP_SCALE;
    float extinction = vol.color.w;
    float3 tint = vol.color.rgb;

    float4 accum = float4(0.0, 0.0, 0.0, 0.0);
    float jitter = hash3(float3(fragCoord.xy, vol.age)) * baseStep;
    float t = tNear + jitter;
    float stepSize = baseStep;
    float time = vol.age / max(vol.lifetime, 0.0001);
    float x = saturate(1.0 - time);
    float ageFade = x * x * (1.0 + 0.2 * x);
    int steps = 0;

    [loop]
    while (t < tFar && steps < maxSteps) {
        steps++;
        if (accum.a >= 0.99) break;
        float3 localMid = localOrigin + (t + stepSize * 0.5) * localDir;
        float density = sampleDensity(localMid, vol.age, ageFade, fbmOctaves);
        if (density <= THRESHOLD) {
            stepSize = min(stepSize * 1.5, maxStep);
            t += stepSize;
            continue;
        }

        if (doRefinement && stepSize > baseStep * 1.1) {
            t = max(t - stepSize * 0.5, tNear);
            stepSize = baseStep;
            continue;
        }

        float alpha = 1.0 - exp(-density * extinction * stepSize);
        accum.rgb += (1.0 - accum.a) * alpha * tint;
        accum.a += (1.0 - accum.a) * alpha;
        t += stepSize;
    }

    if (accum.a < 0.001) discard;
    return accum;
}
