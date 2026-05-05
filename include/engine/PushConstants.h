#pragma once
#include <glm/glm.hpp>

namespace engine {
    inline constexpr uint32_t kMaxIrradianceProbes = 64u;
    inline constexpr uint32_t kMaxPointLights = 16u;

    struct GBufferPC {
        alignas(16) glm::mat4 model;
        alignas(16) glm::mat4 view;
        alignas(16) glm::mat4 projection;
        alignas(16) glm::vec4 camPos; // w = bit 0, has skinning
    };

    struct LightingPC {
        alignas(16) glm::mat4 invView;
        alignas(16) glm::mat4 invProj;
        alignas(16) glm::vec4 camPos; // w = bit 0, 1 = 3x3 bilateral, 0 = single bilinear
    };

    struct ShadowImagePC {
        alignas(16) glm::mat4 invView;
        alignas(16) glm::mat4 invProj;
        alignas(4) uint32_t samples;
        alignas(4) uint32_t pad[3]{0, 0, 0};
    };

    struct UIPC {
        alignas(16) glm::mat4 model;
        alignas(16) glm::vec4 tint;
        alignas(16) glm::vec4 uvClip;
    };

    struct ShadowPC {
        alignas(16) glm::mat4 model;
        alignas(4) uint32_t lightIndex;
        alignas(4) uint32_t flags; // bit 0 = has skinning
        alignas(4) uint32_t pad[2]{0, 0};
    };

    struct ShadowLightEntry {
        alignas(16) glm::mat4 viewProjs[6];
        alignas(16) glm::vec4 lightPosRadius; // xyz = pos, w = radius
    };

    struct ShadowLightsSSBO {
        ShadowLightEntry lights[kMaxPointLights];
    };

    struct SSRPC {
        alignas(16) glm::mat4 view;
        alignas(16) glm::mat4 proj;
        alignas(16) glm::mat4 invView;
        alignas(16) glm::mat4 invProj;
        alignas(4) uint32_t maxSteps;
        alignas(4) uint32_t binarySearchSteps;
        alignas(4) uint32_t pad[2]{0, 0};
    };

    struct AOPC {
        alignas(16) glm::mat4 invProj;
        alignas(16) glm::mat4 proj;
        alignas(16) glm::mat4 view;
        alignas(4) uint32_t flags; // 0 = disabled, 1 = ssao, 2 = gtao
        alignas(4) uint32_t pad[3]{0, 0, 0};
    };

    struct PointLight {
        alignas(16) glm::vec4 positionRadius;
        alignas(16) glm::vec4 colorIntensity;
        alignas(16) glm::vec4 shadowParams;
        alignas(16) glm::uvec4 shadowData;
    };

    struct LightsUBO {
        PointLight pointLights[64];
        alignas(16) glm::uvec4 numPointLights;
    };

    struct IrradianceProbeData {
        alignas(16) glm::vec4 position; // w = influence radius
        alignas(16) glm::vec4 shCoeffs[9]; // spherical harmonics coefficients
    };

    struct IrradianceProbesUBO {
        IrradianceProbeData probes[kMaxIrradianceProbes];
        alignas(16) glm::uvec4 numProbes;
    };

    struct ProbeSHData {
        alignas(16) glm::vec4 coeffs[9];
    };

    struct IrradianceBakePC {
        alignas(16) glm::mat4 model;
        alignas(16) glm::mat4 viewProj;
    };

    struct ParticlePC {
        alignas(16) glm::mat4 viewProj;
        alignas(8) glm::vec2 screenSize;
        alignas(4) float particleSize;
        alignas(4) float trailWidth;
        alignas(4) float streakScale;
        alignas(4) uint32_t pad[3]{0, 0, 0};
    };

    struct SimpleParticlePC {
        alignas(16) glm::vec4 probePosition; // xyz = world position
        alignas(4) float particleSize;
        alignas(4) uint32_t particleCount;
        alignas(4) uint32_t cubemapSize;
        alignas(4) uint32_t activeProbeCount;
        alignas(4) uint32_t layerBase;
        alignas(4) uint32_t mappingOffset;
        alignas(4) uint32_t pad[2]{0, 0};
    };

    struct VolumetricPC {
        alignas(16) glm::mat4 viewProj;
        alignas(16) glm::vec4 camPos; // w = quality, 0 = very low, 1 = low, 2 = medium, 3 = high
    };

    struct CompositePC {
        alignas(8) glm::vec2 inverseScreenSize;
        alignas(4) uint32_t flags; // bit 0-1 = AA mode (0=none, 1=FXAA, 2=SMAA)
        alignas(4) uint32_t pad{0};
    };

    struct CombinePC {
        alignas(4) float exposure;
        alignas(4) uint32_t pad[3]{0, 0, 0};
    };

    struct BlurPC {
        alignas(4) uint32_t blurDirection; // 0 = horizontal, 1 = vertical
        alignas(4) uint32_t taps; // number of taps to use, up to 8
        alignas(4) uint32_t pad[2]{0, 0};
    };

    struct BlurArrayPC {
        alignas(16) glm::mat4 invProj;
        alignas(4) uint32_t blurDirection; // 0 = horizontal, 1 = vertical
        alignas(4) uint32_t taps; // number of taps to use, up to 8
        alignas(4) uint32_t layerCount; // number of shadow layers to blur
        alignas(4) uint32_t pad{0};
    };

    struct SHPC {
        alignas(4) uint32_t cubemapSize;
        alignas(4) uint32_t activeProbeCount;
        alignas(4) uint32_t pad[2]{0, 0};
    };
}
