#pragma once

#include <engine/Renderer.h>
#include <string>
#include <vector>
#include <memory>
#include <typeindex>
#include <optional>
#include <unordered_set>
#include <unordered_map>
#include <map>
#include <functional>
#include <smaa/Textures/AreaTex.h>
#include <smaa/Textures/SearchTex.h>

namespace engine {
    struct CubeFace {
        glm::vec3 dir;
        glm::vec3 up;
    };
    static const CubeFace faces[6] = {
        { glm::vec3( 1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f) }, // +X
        { glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f) }, // -X
        { glm::vec3( 0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f) }, // +Y
        { glm::vec3( 0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f) }, // -Y
        { glm::vec3( 0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f) }, // +Z
        { glm::vec3( 0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f) }  // -Z
    };
    static glm::vec3 unitCube[36] = {
        // Front +Z
        {-0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f},
        {-0.5f,-0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f},
        // Back -Z
        { 0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f},
        { 0.5f,-0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f},
        // Right +X
        { 0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f},
        { 0.5f,-0.5f, 0.5f}, { 0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f, 0.5f},
        // Left -X
        {-0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f},
        {-0.5f,-0.5f,-0.5f}, {-0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f,-0.5f},
        // Top +Y
        {-0.5f, 0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f}, { 0.5f, 0.5f,-0.5f},
        {-0.5f, 0.5f, 0.5f}, { 0.5f, 0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f},
        // Bottom -Y
        {-0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f, 0.5f},
        {-0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f, 0.5f}, {-0.5f,-0.5f, 0.5f}
    };

    struct ShaderStageInfo {
        std::string path;
        VkShaderStageFlagBits stage;
    };

    struct PassImage {
        std::string name;
        // 0 uses swapchain dimensions
        uint32_t width = 0;
        uint32_t height = 0;

        VkClearValue clearValue = {};
        uint32_t mipLevels = 1;
        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
        VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
        VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
        VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        uint32_t arrayLayers = 1;
        VkImageCreateFlags flags = 0;

        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView imageView = VK_NULL_HANDLE;
        VkImageLayout currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    };

    struct PassInfo {
        std::string name;
        std::vector<VkAttachmentDescription> attachmentDescriptions;
        std::vector<VkFormat> attachmentFormats;
        VkFormat depthAttachmentFormat = VK_FORMAT_UNDEFINED;

        bool usesSwapchain = false;
        bool hasDepthAttachment = false;
        bool isActive = true;
        std::optional<std::vector<PassImage>> images = std::nullopt;
        std::vector<VkRenderingAttachmentInfo> colorAttachments;
        std::optional<VkRenderingAttachmentInfo> depthAttachment;
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
            std::vector<uint32_t> vertexDescriptorCounts = {};
            std::vector<VkDescriptorType> vertexDescriptorTypes = {};
            std::vector<uint32_t> fragmentDescriptorCounts = {};
            std::vector<VkDescriptorType> fragmentDescriptorTypes = {};
            VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
            VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            bool depthWrite = true;
            VkCompareOp depthCompare = VK_COMPARE_OP_LESS;
            bool enableDepth = true;
            std::shared_ptr<PassInfo> passInfo = nullptr;
            VkSampler sampler = VK_NULL_HANDLE;
            bool blendEnable = true;
            bool blendAdditive = false;
            VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;
            int colorAttachmentCount = 1;
            std::type_index pushConstantType = std::type_index(typeid(void));

            struct InputBinding {
                uint32_t binding;
                std::string sourceShaderName;
                std::string attachmentName;
            };
            std::vector<InputBinding> inputBindings;

            template<typename T>
            void setPushConstant(VkShaderStageFlags stageFlags) {
                pushConstantRange.stageFlags = stageFlags;
                pushConstantRange.offset = 0;
                pushConstantRange.size = sizeof(T);
                pushConstantType = std::type_index(typeid(T));
            }
            
            std::function<void(std::vector<VkVertexInputBindingDescription>&, std::vector<VkVertexInputAttributeDescription>&)> getVertexInputDescriptions = nullptr;
        } config;

        VkPipeline pipeline{};
        VkPipelineLayout pipelineLayout{};
        VkDescriptorSetLayout descriptorSetLayout{};
        VkDescriptorPool descriptorPool{};
        std::vector<VkDescriptorSet> descriptorSets;

        std::vector<VkDescriptorSet> createDescriptorSets(engine::Renderer* renderer, std::vector<struct Texture*>& textures, std::vector<VkBuffer>& buffers);

        void createDescriptorSetLayout(engine::Renderer* renderer);
        void createPipeline(engine::Renderer* renderer);
        void createDescriptorPool(engine::Renderer* renderer);
        void updateDescriptorSets(engine::Renderer* renderer, std::vector<VkDescriptorSet>& descriptorSets, std::vector<Texture*>& textures, std::vector<VkBuffer>& buffers);

        bool operator==(const GraphicsShader& other) const {
            return name == other.name;
        }
    };

    struct ComputeShader {
        std::string name;
        ShaderStageInfo compute;

        struct Config {
            VkPushConstantRange pushConstantRange{};
            int poolMultiplier = 1;
            int computeBitBindings = 1;
            int storageImageCount = 1;
            int storageBufferCount = 0;
            std::type_index pushConstantType = std::type_index(typeid(void));

            template<typename T>
            void setPushConstant(VkShaderStageFlags stageFlags) {
                pushConstantRange.stageFlags = stageFlags;
                pushConstantRange.offset = 0;
                pushConstantRange.size = sizeof(T);
                pushConstantType = std::type_index(typeid(T));
            }
        } config;

        VkPipeline pipeline{};
        VkPipelineLayout pipelineLayout{};
        VkDescriptorSetLayout descriptorSetLayout{};
        VkDescriptorPool descriptorPool{};

        void createDescriptorSetLayout(engine::Renderer* renderer);
        void createPipeline(engine::Renderer* renderer);
        void createDescriptorPool(engine::Renderer* renderer);

        bool operator==(const ComputeShader& other) const {
            return name == other.name;
        }
    };
};

// add hash specialization to allow usage in unordered_set and unordered_map
template <>
struct std::hash<engine::GraphicsShader> {
    std::size_t operator()(const engine::GraphicsShader& shader) const {
        return std::hash<std::string>()(shader.name);
    }
};
template <>
struct std::hash<engine::ComputeShader> {
    std::size_t operator()(const engine::ComputeShader& shader) const {
        return std::hash<std::string>()(shader.name);
    }
};

namespace engine {
    struct RenderNode {
        bool is2D = false;
        PassInfo* passInfo = nullptr;
        std::unordered_set<GraphicsShader*> shaders;
        std::vector<std::string> shaderNames;
    };
    struct RenderGraph {
        std::vector<RenderNode> nodes;
    };
    
    class ShaderManager {
    public:
        ShaderManager(engine::Renderer* renderer, const std::string& shaderDirectory = "src/assets/shaders/compiled/");
        ~ShaderManager();

        void addGraphicsShader(GraphicsShader shader);
        void addComputeShader(ComputeShader shader);

        std::vector<GraphicsShader> getGraphicsShaders() const;
        std::vector<ComputeShader> getComputeShaders() const;

        void loadAllShaders();
        void loadGraphicsShader(const std::string& name);
        void loadComputeShader(const std::string& name);

        void editGraphicsShader(const std::string& name, const ShaderStageInfo& newVertex, const ShaderStageInfo& newFragment);
        void editComputeShader(const std::string& name, const ShaderStageInfo& newCompute);

        GraphicsShader* getGraphicsShader(const std::string& name) const;
        ComputeShader* getComputeShader(const std::string& name) const;

        std::string getShaderFilePath(const std::string& name) const;

        void loadSMAATextures();
        std::vector<GraphicsShader> createDefaultShaders();
        std::vector<ComputeShader> createDefaultComputeShaders();
        std::vector<RenderNode>& getRenderGraph();
        const std::vector<RenderNode>& getRenderGraph() const;
        void resolveRenderGraphShaders();

        static VkShaderModule createShaderModule(const std::vector<char>& code, Renderer* renderer);

    private:
        std::vector<std::unique_ptr<GraphicsShader>> graphicsShaders;
        std::vector<std::unique_ptr<ComputeShader>> computeShaders;

        std::unordered_map<std::string, GraphicsShader*> graphicsShaderMap;
        std::unordered_map<std::string, ComputeShader*> computeShaderMap;

        std::unordered_map<std::string, std::string> foundShaderFiles;

        std::string shaderDirectory;

        engine::Renderer* renderer;
        RenderGraph renderGraph;
    };
};
