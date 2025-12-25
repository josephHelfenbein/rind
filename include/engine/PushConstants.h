#pragma once
#include <glm/glm.hpp>

namespace engine {
    struct GBufferPC {
        glm::mat4 model;
        glm::mat4 view;
        glm::mat4 projection;
        glm::vec3 camPos;
    };

    struct LightingPC {
        glm::mat4 invView;
        glm::mat4 invProj;
        glm::vec3 camPos;
    };

    struct UIPC {
        glm::vec3 tint;
        float _pad;
        glm::mat4 model;
    };

    struct ShadowPC {
        glm::mat4 model;
        glm::mat4 viewProj;
        glm::vec4 lightPos; // xyz = pos, w = radius
    };

    struct SSRPC {
        glm::mat4 view;
        glm::mat4 proj;
        glm::mat4 invView;
        glm::mat4 invProj;
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
}
