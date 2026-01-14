#pragma once
#include <glm/glm.hpp>

namespace engine {
    struct GBufferPC {
        glm::mat4 model;
        glm::mat4 view;
        glm::mat4 projection;
        glm::vec3 camPos;
        uint32_t flags; // bit 0 = has skinning
    };

    struct LightingPC {
        glm::mat4 invView;
        glm::mat4 invProj;
        glm::vec3 camPos;
        uint32_t shadowSamples;
    };

    struct UIPC {
        glm::vec4 tint;
        glm::mat4 model;
    };

    struct ShadowPC {
        glm::mat4 model;
        glm::mat4 viewProj;
        glm::vec4 lightPos; // xyz = pos, w = radius
        uint32_t flags; // bit 0 = has skinning
        uint32_t pad[3];
    };

    struct SSRPC {
        glm::mat4 view;
        glm::mat4 proj;
        glm::mat4 invView;
        glm::mat4 invProj;
    };

    struct AOPC {
        glm::mat4 invProj;
        glm::mat4 proj;
        glm::mat4 view;
        uint32_t flags; // 0 = disabled, 1 = ssao, 2 = gtao
        uint32_t pad[3];
    };

    struct PointLight {
        glm::vec4 positionRadius;
        glm::vec4 colorIntensity;
        glm::mat4 lightViewProj[6];
        glm::vec4 shadowParams;
        glm::uvec4 shadowData;
    };

    struct LightsUBO {
        PointLight pointLights[64];
        glm::uvec4 numPointLights;
    };

    struct ParticlePC {
        glm::mat4 viewProj;
        glm::vec2 screenSize;
        float particleSize;
        float streakScale;
    };

    struct CompositePC {
        glm::vec2 inverseScreenSize;
        uint32_t flags; // bit 0 = use fxaa
        uint32_t pad;
    };
}
