#pragma pack_matrix(row_major)

struct VSOutput {
    [[vk::location(0)]] float2 fragTexCoord : TEXCOORD2;
};

struct PointLight {
    float4 positionRadius;
    float4 colorIntensity;
    float4 shadowParams;
    uint4 shadowData;
};

struct LightsUBO {
    PointLight pointLights[64];
    uint4 numPointLights;
};

struct IrradianceProbe {
    float4 position; // w = influence radius
    float4 shCoeffs[9];
};

static const uint kMaxIrradianceProbes = 64u;

struct IrradianceProbesUBO {
    IrradianceProbe probes[kMaxIrradianceProbes];
    uint4 numProbes;
};

struct ProbeSHData {
    float4 coeffs[9];
};

static const uint INVALID_SHADOW_INDEX = 0xFFFFFFFF;

[[vk::binding(0)]]
ConstantBuffer<LightsUBO> lightsUBO;

[[vk::binding(1)]]
ConstantBuffer<IrradianceProbesUBO> irradianceProbesUBO;

[[vk::binding(10)]]
StructuredBuffer<ProbeSHData> irradianceProbeSH;

[[vk::binding(2)]]
Texture2D<float4> gBufferAlbedo;

[[vk::binding(3)]]
Texture2D<float4> gBufferNormal;

[[vk::binding(4)]]
Texture2D<float4> gBufferMaterial;

[[vk::binding(5)]]
Texture2D<float> gBufferDepth;

[[vk::binding(6)]]
Texture2D<float4> particleTexture;

[[vk::binding(7)]]
Texture2D<float4> volumetricTexture;

[[vk::binding(8)]]
Texture2DArray<float> shadowTexture;

[[vk::binding(9)]]
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

float geometrySmith(float NdotV, float NdotL, float roughness) {
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

void evaluateIrradiance(float3 diffuseDir, float3 specularDir, float3 fragPos, out float3 diffuseIrr, out float3 specularIrr) {
    diffuseIrr = float3(0.0, 0.0, 0.0);
    specularIrr = float3(0.0, 0.0, 0.0);
    uint numProbes = irradianceProbesUBO.numProbes.x;
    float totalWeightDiffuse = 0.0001;
    float totalWeightSpecular = 0.0001;
    
    float3 diffuseN = float3(diffuseDir.z, diffuseDir.x, diffuseDir.y);
    float3 specularN = float3(specularDir.z, specularDir.x, specularDir.y);
    
    for (uint i = 0; i < numProbes; ++i) {
        float3 probePos = irradianceProbesUBO.probes[i].position.xyz;
        float probeRadius = irradianceProbesUBO.probes[i].position.w;
        float dist = length(fragPos - probePos);
        
        if (dist <= probeRadius) {
            float t = saturate(1.0 - dist / probeRadius);
            float weight = t * t;
            
            float3 L00 = irradianceProbeSH[i].coeffs[0].xyz;
            float3 L1m1 = irradianceProbeSH[i].coeffs[1].xyz;
            float3 L10 = irradianceProbeSH[i].coeffs[2].xyz;
            float3 L11 = irradianceProbeSH[i].coeffs[3].xyz;
            float3 L2m2 = irradianceProbeSH[i].coeffs[4].xyz;
            float3 L2m1 = irradianceProbeSH[i].coeffs[5].xyz;
            float3 L20 = irradianceProbeSH[i].coeffs[6].xyz;
            float3 L21 = irradianceProbeSH[i].coeffs[7].xyz;
            float3 L22 = irradianceProbeSH[i].coeffs[8].xyz;
            
            const float c1 = 0.429043;
            const float c2 = 0.511664;
            const float c3 = 0.743125;
            const float c4 = 0.886227;
            const float c5 = 0.247708;
            
            float3 shSumDiffuse = c4 * L00
                         + 2.0 * c2 * (L11 * diffuseN.x + L1m1 * diffuseN.y + L10 * diffuseN.z)
                         + 2.0 * c1 * (L2m2 * diffuseN.x * diffuseN.y + L21 * diffuseN.x * diffuseN.z + L2m1 * diffuseN.y * diffuseN.z)
                         + c3 * L20 * diffuseN.z * diffuseN.z
                         + c1 * L22 * (diffuseN.x * diffuseN.x - diffuseN.y * diffuseN.y)
                         - c5 * L20;
            diffuseIrr += shSumDiffuse * weight;
            totalWeightDiffuse += weight;

            float3 shSumSpecular = c4 * L00
                         + 2.0 * c2 * (L11 * specularN.x + L1m1 * specularN.y + L10 * specularN.z)
                         + 2.0 * c1 * (L2m2 * specularN.x * specularN.y + L21 * specularN.x * specularN.z + L2m1 * specularN.y * specularN.z)
                         + c3 * L20 * specularN.z * specularN.z
                         + c1 * L22 * (specularN.x * specularN.x - specularN.y * specularN.y)
                         - c5 * L20;
            specularIrr += shSumSpecular * weight;
            totalWeightSpecular += weight;
        }
    }
    diffuseIrr /= totalWeightDiffuse;
    specularIrr /= totalWeightSpecular;
    diffuseIrr = max(diffuseIrr, float3(0.0, 0.0, 0.0));
    specularIrr = max(specularIrr, float3(0.0, 0.0, 0.0));
}

float4 main(VSOutput input) : SV_Target {
    float4 albedoSample = gBufferAlbedo.Sample(sampleSampler, input.fragTexCoord);
    float3 rawNormal = gBufferNormal.Sample(sampleSampler, input.fragTexCoord).xyz * 2.0 - 1.0;
    float normalLen = length(rawNormal);
    float3 N = (normalLen > 0.001) ? (rawNormal / normalLen) : float3(0.0, 1.0, 0.0);
    float3 materialSample = gBufferMaterial.Sample(sampleSampler, input.fragTexCoord).rgb;
    float metallic = materialSample.r;
    float baseRoughness = materialSample.g;
    float depth = gBufferDepth.Sample(sampleSampler, input.fragTexCoord);
    if (depth >= 0.9999) {
        float4 particleColor = particleTexture.Sample(sampleSampler, input.fragTexCoord);
        float4 volumetricColor = volumetricTexture.Sample(sampleSampler, input.fragTexCoord);
        float3 result = albedoSample.rgb + particleColor.rgb * particleColor.a + volumetricColor.rgb;
        return float4(result, particleColor.a + volumetricColor.a);
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
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedoSample.rgb, metallic);
    float3 Lo = float3(0.0, 0.0, 0.0);
    for (uint i = 0; i < numLights; ++i) {
        PointLight light = lightsUBO.pointLights[i];
        float3 lightPos = light.positionRadius.xyz;
        float lightRadius = light.positionRadius.w;
        float3 lightColor = light.colorIntensity.rgb;
        uint shadowIndex = light.shadowData.x;
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

        float3 F = fresnelSchlickRoughness(max(dot(H, V), 0.0), F0, roughness);
        float D = distributionGGX(N, H, roughness);
        float G = geometrySmith(NdotV, NdotL, roughness);

        float3 numerator = F * D * G;
        float denominator = 4.0 * max(NdotV, 0.0001) * max(NdotL, 0.0001);
        float3 specular = numerator / denominator;
        float3 kD = (1.0 - F) * (1.0 - metallic);
        float3 diffuse = kD * albedoSample.rgb;

        bool hasShadow = (shadowIndex != INVALID_SHADOW_INDEX && light.shadowData.y != 0 && shadowIndex < 64u);
        float shadow = hasShadow
            ? shadowTexture.Sample(sampleSampler, float3(input.fragTexCoord, float(shadowIndex)))
            : 1.0;

        float3 contribution = (diffuse + specular) * radiance * NdotL * shadow;
        contribution = clamp(contribution, float3(0.0, 0.0, 0.0), float3(100.0, 100.0, 100.0));
        Lo += contribution;
    }
    float NdotV = max(dot(N, V), 0.0);
    float3 FIndirect = fresnelSchlickRoughness(NdotV, F0, roughness);
    float3 kDIndirect = (1.0 - FIndirect) * (1.0 - metallic);
    
    float3 R = reflect(-V, N);
    
    float3 dominantDir = lerp(R, N, roughness * roughness);
    float3 irradiance = float3(0.0, 0.0, 0.0);
    float3 specularIrradiance = float3(0.0, 0.0, 0.0);

    evaluateIrradiance(N, dominantDir, fragPos, irradiance, specularIrradiance);
    
    float3 indirectDiffuse = kDIndirect * irradiance * albedoSample.rgb;
    float shSpecularValidity = roughness * roughness;
    
    float2 envBRDF = float2(1.0 - roughness, roughness) * FIndirect.r;
    float3 specularScale = FIndirect * envBRDF.x + envBRDF.y;
    
    float specularAttenuation = lerp(shSpecularValidity, 1.0, roughness);
    float3 indirectSpecular = specularScale * specularIrradiance * specularAttenuation;
    
    Lo += indirectDiffuse + indirectSpecular;

    if (any(isnan(Lo)) || any(isinf(Lo))) {
        Lo = albedoSample.rgb * 0.1;
    }
    float4 particleColor = particleTexture.Sample(sampleSampler, input.fragTexCoord);
    float4 volumetricColor = volumetricTexture.Sample(sampleSampler, input.fragTexCoord);
    Lo += particleColor.rgb * particleColor.a + volumetricColor.rgb;
    float alphaOut = max(max(Lo.r, Lo.g), max(Lo.b, albedoSample.a));
    return float4(Lo, alphaOut);
}