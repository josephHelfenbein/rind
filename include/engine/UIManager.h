#pragma once

#include <vulkan/vulkan.h>

#include <glfw/include/GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <engine/Renderer.h>
#include <engine/TextureManager.h>
#include <engine/ShaderManager.h>
#include <string>
#include <vector>
#include <map>

namespace engine {
    class UIObject {
    public:
        enum class Corner {
            TopLeft,
            TopRight,
            BottomLeft,
            BottomRight,
            Center
        };
        UIObject(glm::mat4 transform, std::string name, std::string texture, Corner anchorCorner = Corner::Center)
            : transform(transform), name(name), texture(texture), anchorCorner(anchorCorner) {}
        virtual ~UIObject() = default;

        const std::string& getName() const { return name; }
        const glm::mat4& getTransform() const { return transform; }
        void setTransform(const glm::mat4& transform) { this->transform = transform; }

        const std::string& getTexture() const { return texture; }

        const std::vector<VkDescriptorSet>& getDescriptorSets() const { return descriptorSets; }
        void setDescriptorSets(const std::vector<VkDescriptorSet>& descriptorSets) { this->descriptorSets = descriptorSets; }

        void addChild(UIObject* child);
        void removeChild(UIObject* child);
        const std::vector<UIObject*>& getChildren() const { return children; }
        UIObject* getParent() const { return parent; }

        void setEnabled(bool enabled) { this->enabled = enabled; }
        bool isEnabled() const { return enabled; }

    private:
        std::string name;
        glm::mat4 transform;
        Corner anchorCorner;
        std::string texture;
        std::vector<VkDescriptorSet> descriptorSets;
        UIObject* parent = nullptr;
        std::vector<UIObject*> children;
        bool enabled = true;
    };

    class UIManager {
    public:
        UIManager(Renderer* renderer);
        ~UIManager();

        void addObject(UIObject* object);
        void removeObject(const std::string& name);
        UIObject* getObject(const std::string& name);
        std::map<std::string, UIObject*>& getObjects() { return objects; }
        void clear();
        void loadTextures();

    private:
        Renderer* renderer;
        std::map<std::string, UIObject*> objects;
    };
};