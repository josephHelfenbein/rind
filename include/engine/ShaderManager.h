#pragma once

#include <engine/Renderer.h>
#include <string>
#include <map>
#include <chrono>
#include <filesystem>
#include <format>
#include <iostream>
#include <fstream>
#include <vector>
#include <memory>

namespace engine {

    struct ShaderStageInfo {
        std::string path;
        VkShaderStageFlagBits stage;
    };

    struct GraphicsShader {
        std::string name;
        ShaderStageInfo vertex;
        ShaderStageInfo fragment;

        struct Config {
            VkPushConstantRange pushConstantRange{};
            int poolMultiplier = 1;
            int vertexBitBindings = 1;
            int fragmentBitBindings = 4;
            std::function<void(VkVertexInputBindingDescription&, std::vector<VkVertexInputAttributeDescription>&)> getVertexInputDescriptions = nullptr;
            std::vector<uint32_t> fragmentDescriptorCounts = {};
            VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
            VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            bool depthWrite = true;
            VkCompareOp depthCompare = VK_COMPARE_OP_LESS;
            bool enableDepth = true;
            VkRenderPass renderPassToUse = VK_NULL_HANDLE;
            VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;
            int colorAttachmentCount = 1;
        } config;

        VkPipeline pipeline{};
        VkPipelineLayout pipelineLayout{};
        VkDescriptorSetLayout descriptorSetLayout{};
        VkDescriptorPool descriptorPool{};
    };

    struct ComputeShader {
        std::string name;
        ShaderStageInfo compute;

        struct Config {
            VkPushConstantRange pushConstantRange{};
            int poolMultiplier = 1;
            int computeBitBindings = 1;
            int storageImageCount = 1;
        } config;

        VkPipeline pipeline{};
        VkPipelineLayout pipelineLayout{};
        VkDescriptorSetLayout descriptorSetLayout{};
        VkDescriptorPool descriptorPool{};
    };
    
    class ShaderManager {
    public:
        ShaderManager(engine::Renderer* renderer, std::string shaderDirectory = "src/assets/shaders/compiled/");
        ~ShaderManager();

        void addGraphicsShader(const std::string& name, const ShaderStageInfo& vertex, const ShaderStageInfo& fragment, const GraphicsShader::Config& config = {});

        void addComputeShader(const std::string& name, const ShaderStageInfo& compute, const ComputeShader::Config& config = {});

        void loadAllShaders();
        void loadGraphicsShader(const std::string& name);
        void loadComputeShader(const std::string& name);

        void editGraphicsShader(const std::string& name, const ShaderStageInfo& newVertex, const ShaderStageInfo& newFragment);
        void editComputeShader(const std::string& name, const ShaderStageInfo& newCompute);

        GraphicsShader* getGraphicsShader(const std::string& name);
        ComputeShader* getComputeShader(const std::string& name);

        std::string getShaderFilePath(const std::string& name);

    private:
        std::vector<std::unique_ptr<GraphicsShader>> graphicsShaders;
        std::vector<std::unique_ptr<ComputeShader>> computeShaders;

        std::map<std::string, GraphicsShader*> graphicsShaderMap;
        std::map<std::string, ComputeShader*> computeShaderMap;

        std::map<std::string, std::string> foundShaderFiles;

        engine::Renderer* renderer;
    };
};