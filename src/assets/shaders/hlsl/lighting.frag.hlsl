struct VSOutput {
    [[vk::location(0)]] float2 fragTexCoord : TEXCOORD2;
};

struct PointLight {
    float4 positionRadius;
    float4 colorIntensity;
    float4x4 lightViewProj[6];
    float4 shadowParams;
    uint4 shadowData;
};

struct LightsUBO {
    PointLight pointLights[64];
    uint4 numPointLights;
};

[[vk::binding(0)]]
ConstantBuffer<LightsUBO> lightsUBO;

[[vk::binding(1)]]
Texture2D<float4> gBufferAlbedo;

[[vk::binding(2)]]
Texture2D<float4> gBufferNormal;

[[vk::binding(3)]]
Texture2D<float4> gBufferMaterial;

[[vk::binding(4)]]
Texture2D<float4> gBufferDepth;

[[vk::binding(5)]]
TextureCubeArray<float> shadowMaps;

[[vk::binding(6)]]
SamplerState sampleSampler;

struct PushConstants {
    float4x4 invView;
    float4x4 invProj;
    float3 camPos;
};
[[vk::push_constant]] PushConstants pc;

static const float PI = 3.14159265359;

float3 reconstructPosition(float2 uv, float depth) {
    float4 ndc = float4(uv * 2.0 - 1.0, depth, 1.0);
    float4 viewPos = mul(pc.invProj, ndc);
    viewPos /= viewPos.w;
    float4 worldPos = mul(pc.invView, viewPos);
    return worldPos.xyz;
}

float3 fresnelSchlickRoughness(float cosTheta, float3 F0, float roughness) {
    cosTheta = clamp(cosTheta, 0.0, 1.0);
    float3 oneMinusR = float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness);
    return F0 + (max(oneMinusR, F0) - F0) * pow(1.0 - cosTheta, 5.0);
}

float distributionGGX(float3 N, float3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return a2 / max(denom, 0.0001);
}

float geometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float denom = NdotV * (1.0 - k) + k;
    return NdotV / max(denom, 0.0001);
}

float geometrySmith(float3 N, float3 V, float3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = geometrySchlickGGX(NdotV, roughness);
    float ggx1 = geometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

float specularAntiAliasing(float3 N, float roughness) {
    float3 dndu = ddx(N);
    float3 dndv = ddy(N);
    float variance = dot(dndu, dndu) + dot(dndv, dndv);
    float kernelRoughness = min(mul(2.0, variance), 1.0);
    return clamp(roughness + kernelRoughness, 0.0, 1.0);
}

static const uint INVALID_SHADOW_INDEX = 0xFFFFFFFF;

static const float3 sampleOffsetDirections[20] = {
    float3( 1,  1,  1), float3(-1,  1,  1), float3( 1, -1,  1), float3(-1, -1,  1),
    float3( 1,  1, -1), float3(-1,  1, -1), float3( 1, -1, -1), float3(-1, -1, -1),
    float3( 1,  0,  0), float3(-1,  0,  0), float3( 0,  1,  0), float3( 0, -1,  0),
    float3( 0,  0,  1), float3( 0,  0, -1), float3( 1,  1,  0), float3(-1,  1,  0),
    float3( 1, -1,  0), float3(-1, -1,  0), float3( 1,  0,  1), float3(-1,  0, -1)
};

float computePointShadow(PointLight light, float3 fragPos, float3 geomNormal, float3 lightDir) {
    if (light.shadowData.y == 0) return 1.0;
    uint shadowIndex = light.shadowData.x;
    if (shadowIndex == INVALID_SHADOW_INDEX) return 1.0;
    float3 lightPos = light.positionRadius.xyz;
    float3 toFrag = fragPos - lightPos;
    float currentDepth = length(toFrag);
    if (currentDepth <= 0.0001) return 1.0;
    float farPlane = light.shadowParams.y;
    if (currentDepth > farPlane) return 1.0;

    float3 sampleDir = normalize(toFrag);
    float depthSample = shadowMaps.Sample(sampleSampler, float4(sampleDir, shadowIndex)).r;
    if (depthSample >= 0.9999) return 1.0;
    uint samples = 30;
    float viewDistance = length(pc.camPos - fragPos);
    float diskRadius = (1.0 + (viewDistance / farPlane)) / 25.0;
    float shadow = 0.0;
    for (uint i = 0; i < samples; ++i) {
        float3 offsetDir = sampleOffsetDirections[i % 20];
        float3 sampleVec = sampleDir + offsetDir * diskRadius;
        sampleVec = normalize(sampleVec);
        float depthSample = shadowMaps.Sample(sampleSampler, float4(sampleVec, shadowIndex)).r;
        float closestDepth = depthSample * farPlane;
        
        float NoLGeom = max(dot(geomNormal, lightDir), 0.0);
        float baseBias = light.shadowParams.x;
        float normalBias = baseBias * 5.0 * (1.0 - NoLGeom);
        float bias = baseBias + normalBias;
        if (currentDepth - bias > closestDepth) {
            shadow += 1.0;
        }
    }
    shadow /= samples;
    float strength = clamp(light.shadowParams.w, 0.0, 1.0);
    return 1.0 - (shadow * strength);
}

float4 main(VSOutput input) : SV_Target {
    float3 albedoSample = gBufferAlbedo.Sample(sampleSampler, input.fragTexCoord).rgb;
    float alpha = gBufferAlbedo.Sample(sampleSampler, input.fragTexCoord).a;
    float3 N = normalize(gBufferNormal.Sample(sampleSampler, input.fragTexCoord).xyz * 2.0 - 1.0);
    float3 materialSample = gBufferMaterial.Sample(sampleSampler, input.fragTexCoord).rgb;
    float metallic = materialSample.r;
    float baseRoughness = materialSample.g;
    float depth = gBufferDepth.Sample(sampleSampler, input.fragTexCoord).r;
    if (depth >= 0.9999) {
        return float4(albedoSample, 1.0); // Background
    }
    float3 fragPos = reconstructPosition(input.fragTexCoord, depth);
    float3 V = normalize(pc.camPos - fragPos);
    float3 geomNormal = cross(ddx(fragPos), ddy(fragPos));
    if (dot(geomNormal, geomNormal) > 1e-10) {
        geomNormal = normalize(geomNormal);
    } else {
        geomNormal = N;
    }
    if (dot(geomNormal, N) < 0.0) {
        geomNormal = -geomNormal;
    }
    float roughness = specularAntiAliasing(N, baseRoughness);
    roughness = clamp(roughness, 0.05, 1.0);
    float dNdx = length(ddx(N));
    float dNdy = length(ddy(N));
    float dVdx = length(ddx(V));
    float dVdy = length(ddy(V));
    float sigma = max(max(dNdx, dNdy), max(dVdx, dVdy));
    roughness = max(roughness, sigma);
    uint numLights = lightsUBO.numPointLights.x;
    float3 Lo = float3(0.0, 0.0, 0.0);
    for (uint i = 0; i < numLights; ++i) {
        PointLight light = lightsUBO.pointLights[i];
        float3 lightPos = light.positionRadius.xyz;
        float lightRadius = light.positionRadius.w;
        float3 lightColor = light.colorIntensity.rgb;
        float intensity = light.colorIntensity.w;
        float3 L = normalize(lightPos - fragPos);
        float3 H = normalize(V + L);
        float distance = length(lightPos - fragPos);
        float t = saturate(1.0 - distance / lightRadius);
        float attenuation = t * t;
        
        float3 radiance = lightColor * intensity * attenuation;
        float NdotL = max(dot(N, L), 0.0);
        float NdotV = max(dot(N, V), 0.0);

        float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedoSample, metallic);
        float3 F = fresnelSchlickRoughness(max(dot(H, V), 0.0), F0, roughness);
        float D = distributionGGX(N, H, roughness);
        float G = geometrySmith(N, V, L, roughness);

        float3 numerator = F * D * G;
        float denominator = 4.0 * max(NdotV, 0.0001) * max(NdotL, 0.0001);
        float3 specular = numerator / denominator;
        float3 kD = (1.0 - F0) * (1.0 - metallic);
        float3 diffuse = kD * albedoSample;

        float shadow = computePointShadow(light, fragPos, geomNormal, L);
        Lo += (diffuse / PI + specular) * radiance * NdotL * shadow;
    }
    float3 ambient = float3(0.03, 0.03, 0.03) * albedoSample;
    Lo += ambient;
    float alphaOut = max(max(Lo.r, Lo.g), max(Lo.b, alpha));
    return float4(Lo, alphaOut);
}