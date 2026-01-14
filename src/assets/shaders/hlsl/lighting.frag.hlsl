#pragma pack_matrix(row_major)

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
Texture2D<float> gBufferDepth;

[[vk::binding(5)]]
Texture2D<float4> particleTexture;

[[vk::binding(6)]]
TextureCube<float> shadowMaps[64];

[[vk::binding(7)]]
SamplerState sampleSampler;

struct PushConstants {
    float4x4 invView;
    float4x4 invProj;
    float3 camPos;
    uint shadowSamples;
};
[[vk::push_constant]] PushConstants pc;

static const float PI = 3.14159265359;

float3 reconstructPosition(float2 uv, float depth) {
    float4 ndc = float4(uv * 2.0 - 1.0, depth, 1.0);
    float4 viewPos = mul(ndc, pc.invProj);
    viewPos /= viewPos.w;
    float4 worldPos = mul(viewPos, pc.invView);
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
    float kernelRoughness = min(2.0 * variance, 1.0);
    return clamp(roughness + kernelRoughness, 0.0, 1.0);
}

static const uint INVALID_SHADOW_INDEX = 0xFFFFFFFF;

static const float3 sampleOffsetDirections[32] = {
    float3( 1,  1,  1), float3(-1,  1,  1), float3( 1, -1,  1), float3(-1, -1,  1),
    float3( 1,  1, -1), float3(-1,  1, -1), float3( 1, -1, -1), float3(-1, -1, -1),
    float3( 1,  0,  0), float3(-1,  0,  0), float3( 0,  1,  0), float3( 0, -1,  0),
    float3( 0,  0,  1), float3( 0,  0, -1), float3( 1,  1,  0), float3(-1,  1,  0),
    float3( 1, -1,  0), float3(-1, -1,  0), float3( 1,  0,  1), float3(-1,  0, -1),
    float3( 0,  1,  1), float3( 0, -1, -1), float3( 1,  0, -1), float3(-1,  0,  1),
    float3( 0,  1, -1), float3( 0, -1,  1), float3( 0.5, 0.0, 1.0), float3(-0.5, 0.0, -1.0),
    float3( 0.0, 0.5, 1.0), float3( 0.0, -0.5, -1.0), float3( 1.0, 0.5, 0.0), float3(-1.0, -0.5, 0.0)
};

float linearizeDepth(float perspectiveDepth, float nearPlane, float farPlane) {
    return nearPlane * farPlane / (farPlane - perspectiveDepth * (farPlane - nearPlane));
}

float computePointShadow(PointLight light, float3 fragPos, float3 geomNormal, float3 lightDir) {
    uint shadowIndex = light.shadowData.x;
    uint hasShadow = light.shadowData.y;
    if (shadowIndex == INVALID_SHADOW_INDEX || hasShadow == 0) {
        return 1.0;
    }
    float3 lightPos = light.positionRadius.xyz;
    float3 toFrag = fragPos - lightPos;
    float currentDistance = length(toFrag);
    float farPlane = light.shadowParams.y;
    float nearPlane = light.shadowParams.z;
    float baseBias = light.shadowParams.x;
    float shadowFadeStart = nearPlane * 10.0;
    if (currentDistance <= nearPlane || currentDistance > farPlane) {
        return 1.0;
    }
    float shadowFade = saturate((currentDistance - nearPlane) / (shadowFadeStart - nearPlane));
    float currentDepth = currentDistance / farPlane;
    float3 sampleDir = normalize(toFrag);
    float NdotL = max(dot(geomNormal, lightDir), 0.0);
    float slopeBias = baseBias * (1.0 - NdotL) * 2.0;
    float distanceBias = baseBias * (nearPlane / max(currentDistance, nearPlane));
    float bias = baseBias + slopeBias + distanceBias;
    float shadow = 0.0;
    float totalWeight = 0.0;
    float diskRadius = (1.0 + (length(pc.camPos - fragPos)) / farPlane) / 25.0;
    float penumbraSize = 0.015 * (currentDistance / farPlane);
    for (uint i = 0; i < pc.shadowSamples; ++i) {
        float3 offsetDir = sampleOffsetDirections[i];
        offsetDir = normalize(offsetDir);
        offsetDir = offsetDir - dot(offsetDir, sampleDir) * sampleDir;
        offsetDir = normalize(offsetDir);
        float offsetLen = length(sampleOffsetDirections[i]);
        float weight = exp(-offsetLen * offsetLen * 0.5);
        float3 sampleOffset = sampleDir + offsetDir * diskRadius;
        float sampleDepth = shadowMaps[shadowIndex].Sample(sampleSampler, sampleOffset);
        float diff = (currentDepth - bias) - sampleDepth;
        shadow += smoothstep(0.0, penumbraSize, diff) * weight;
        totalWeight += weight;
    }
    shadow /= totalWeight;
    shadow *= shadowFade;
    return 1.0 - shadow;
}

float3 ACESFilm(float3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float4 main(VSOutput input) : SV_Target {
    float3 albedoSample = gBufferAlbedo.Sample(sampleSampler, input.fragTexCoord).rgb;
    float alpha = gBufferAlbedo.Sample(sampleSampler, input.fragTexCoord).a;
    float3 rawNormal = gBufferNormal.Sample(sampleSampler, input.fragTexCoord).xyz * 2.0 - 1.0;
    float normalLen = length(rawNormal);
    float3 N = (normalLen > 0.001) ? (rawNormal / normalLen) : float3(0.0, 1.0, 0.0);
    float3 materialSample = gBufferMaterial.Sample(sampleSampler, input.fragTexCoord).rgb;
    float metallic = materialSample.r;
    float baseRoughness = materialSample.g;
    float depth = gBufferDepth.Sample(sampleSampler, input.fragTexCoord);
    if (depth >= 0.9999) {
        return float4(ACESFilm(albedoSample), 1.0); // Background
    }
    float3 fragPos = reconstructPosition(input.fragTexCoord, depth);
    float3 toCamera = pc.camPos - fragPos;
    float camDist = length(toCamera);
    float3 V = (camDist > 0.001) ? (toCamera / camDist) : float3(0.0, 1.0, 0.0);
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
        float3 toLight = lightPos - fragPos;
        float distance = length(toLight);
        if (distance < 0.001) {
            continue;
        }
        float3 L = toLight / distance;
        float3 H = V + L;
        float hLen = length(H);
        if (hLen < 0.001) {
            continue;
        }
        H = H / hLen;        
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
        float3 kD = (1.0 - F) * (1.0 - metallic);
        float3 diffuse = kD * albedoSample;

        float shadow = computePointShadow(light, fragPos, geomNormal, L);
        
        float3 contribution = (diffuse + specular) * radiance * NdotL * shadow;
        contribution = clamp(contribution, float3(0.0, 0.0, 0.0), float3(100.0, 100.0, 100.0));
        Lo += contribution;
    }
    float3 ambient = float3(0.02, 0.02, 0.02) * albedoSample;
    Lo += ambient;
    if (any(isnan(Lo)) || any(isinf(Lo))) {
        Lo = albedoSample * 0.1;
    }
    float4 particleColor = particleTexture.Sample(sampleSampler, input.fragTexCoord);
    Lo += particleColor.rgb * particleColor.a;
    float alphaOut = max(max(Lo.r, Lo.g), max(Lo.b, alpha));
    return float4(ACESFilm(Lo), alphaOut);
}