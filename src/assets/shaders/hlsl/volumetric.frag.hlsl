#pragma pack_matrix(row_major)

struct VSOutput {
    [[vk::location(0)]] float3 worldPos : TEXCOORD0;
    [[vk::location(1)]] nointerpolation uint instanceID : TEXCOORD1;
    [[vk::location(2)]] nointerpolation uint maxSteps : TEXCOORD2;
    [[vk::location(3)]] nointerpolation float baseDivs : TEXCOORD3;
    [[vk::location(4)]] nointerpolation uint fbmOctaves : TEXCOORD4;
    [[vk::location(5)]] nointerpolation uint doRefinement : TEXCOORD5;
    [[vk::location(6)]] nointerpolation float ageFade : TEXCOORD6;
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
    float amp = 0.7;
    float freq = 1.3;
    if (octaves >= 1) { v += vnoise(p * freq) * amp; amp *= 0.5; freq *= 2.0; }
    if (octaves >= 2) { v += vnoise(p * freq) * amp; amp *= 0.5; freq *= 2.0; }
    if (octaves >= 3) { v += vnoise(p * freq) * amp; amp *= 0.5; freq *= 2.0; }
    if (octaves >= 4) { v += vnoise(p * freq) * amp; amp *= 0.5; freq *= 2.0; }
    if (octaves >= 5) { v += vnoise(p * freq) * amp; }
    return v;
}

float sampleDensity(float3 localPos, float age, float ageFade, int fbmOctaves) {
    float r2 = dot(localPos, localPos);
    if (r2 >= 0.46) return 0.0;
    float radial = exp(-r2 * 12.0);
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

    int maxSteps = max(1, (int) input.maxSteps);
    float baseDivs = max(input.baseDivs, 1.0);
    int fbmOctaves = max(1, (int) input.fbmOctaves);
    bool doRefinement = (input.doRefinement != 0u);

    float totalLen = tFar - tNear;
    float baseStep = totalLen / baseDivs;
    float maxStep = baseStep * MAX_STEP_SCALE;
    float extinction = vol.color.w;
    float3 tint = vol.color.rgb;
    half extinction16 = half(extinction);
    half3 tint16 = half3(tint);

    half4 accum = half4(0.0, 0.0, 0.0, 0.0);
    float jitter = hash3(float3(fragCoord.xy, vol.age)) * baseStep;
    float t = tNear + jitter;
    float stepSize = baseStep;
    float ageFade = input.ageFade;
    int steps = 0;

    [loop]
    while (t < tFar && steps < maxSteps) {
        steps++;
        if (accum.a >= half(0.99)) break;
        float3 localMid = localOrigin + (t + stepSize * 0.5) * localDir;
        half density = half(sampleDensity(localMid, vol.age, ageFade, fbmOctaves));
        if (density <= half(THRESHOLD)) {
            stepSize = min(stepSize * 1.5, maxStep);
            t += stepSize;
            continue;
        }

        if (doRefinement && stepSize > baseStep * 1.1) {
            t = max(t - stepSize * 0.5, tNear);
            stepSize = baseStep;
            continue;
        }

        half transmittance = half(1.0) - accum.a;
        half opticalDepth = density * extinction16 * half(stepSize);
        half contrib = (half(1.0) - exp(-opticalDepth)) * transmittance;
        accum.rgb += contrib * tint16;
        accum.a += contrib;
        t += stepSize;
    }

    if (accum.a < half(0.001)) discard;
    return float4(accum);
}
