#pragma once

#include <vulkan/vulkan.h>

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <engine/Renderer.h>
#include <engine/TextureManager.h>
#include <engine/ShaderManager.h>
#include <engine/PushConstants.h>
#include <external/freetype/include/ft2build.h>
#include FT_FREETYPE_H
#include <engine/io.h>
#include <variant>
#include <functional>
#include <string>
#include <vector>
#include <map>

namespace engine {
    class UIManager;
    class UIObject;
    class TextObject;

    enum class Corner {
        TopLeft,
        TopRight,
        BottomLeft,
        BottomRight,
        Center
    };

    class TextObject {
    public:
        TextObject(UIManager* uiManager, glm::mat4 transform, std::string name, glm::vec3 tint, std::string text, std::string font, Corner anchorCorner = Corner::Center);
        ~TextObject() = default;

        const std::string& getText() const { return text; }
        void setText(const std::string& text) { this->text = text; }
        const std::string& getName() const { return name; }
        const glm::mat4& getTransform() const { return transform; }
        void setTransform(const glm::mat4& transform) { this->transform = transform; }
        class UIObject* getParent() const { return parent; }
        void setParent(UIObject* parent) { this->parent = parent; }
        const std::string& getFont() const { return font; }
        bool isEnabled() const { return enabled; }
        void setEnabled(bool enabled) { this->enabled = enabled; }
        glm::vec3 getTint() const { return tint; }
        Corner getAnchorCorner() const { return anchorCorner; }
        glm::vec2 getScale() const;
        float getVerticalOffsetRatio() const { return verticalOffsetRatio; }
        void setVerticalOffsetRatio(float ratio) { verticalOffsetRatio = ratio; }

    private:
        UIManager* uiManager;
        std::string name;
        glm::vec3 tint;
        std::string text;
        std::string font;
        glm::mat4 transform;
        Corner anchorCorner;
        float verticalOffsetRatio = 0.0f;
        class UIObject* parent = nullptr;
        bool enabled = true;
    };

    class UIObject {
    public:
        UIObject(UIManager* uiManager, glm::mat4 transform, std::string name, glm::vec3 tint, std::string texture, Corner anchorCorner = Corner::Center, std::function<void()>* onHover = nullptr, std::function<void()>* onStopHover = nullptr);
        virtual ~UIObject();

        const std::string& getName() const { return name; }
        const glm::mat4& getTransform() const { return transform; }
        void setTransform(const glm::mat4& transform) { this->transform = transform; }

        const std::string& getTexture() const { return texture; }

        const std::vector<VkDescriptorSet>& getDescriptorSets() const { return descriptorSets; }
        void setDescriptorSets(const std::vector<VkDescriptorSet>& descriptorSets) { this->descriptorSets = descriptorSets; }

        void addChild(UIObject* child);
        void addChild(TextObject* child);
        void removeChild(UIObject* child);
        void removeChild(TextObject* child);
        const std::vector<std::variant<UIObject*, TextObject*>>& getChildren() const { return children; }
        UIObject* getParent() const { return parent; }
        void setParent(UIObject* parent) { this->parent = parent; }

        void setEnabled(bool enabled) { this->enabled = enabled; }
        bool isEnabled() const { return enabled; }
        glm::vec3 getTint() const { return tint; }
        Corner getAnchorCorner() const { return anchorCorner; }
        std::function<void()>* getOnHover() const { return onHover; }
        std::function<void()>* getOnStopHover() const { return onStopHover; }

    private:
        UIManager* uiManager;
        std::string name;
        glm::vec3 tint;
        glm::mat4 transform;
        Corner anchorCorner;
        std::string texture;
        std::vector<VkDescriptorSet> descriptorSets;
        UIObject* parent = nullptr;
        std::vector<std::variant<UIObject*, TextObject*>> children;
        std::function<void()>* onHover;
        std::function<void()>* onStopHover;
        bool enabled = true;
    };

    class ButtonObject : public UIObject {
    public:
        ButtonObject(UIManager* uiManager, glm::mat4 transform, std::string name, glm::vec3 tint, glm::vec3 textColor, std::string texture, std::string text, std::string font, std::function<void()> onClick, Corner anchorCorner = Corner::Center)
            : UIObject(uiManager, transform, name, tint, texture, anchorCorner), onClick(onClick) {
            constexpr float kEpsilon = 1e-6f;
            glm::vec3 right = glm::vec3(transform[0]);
            glm::vec3 up = glm::vec3(transform[1]);
            glm::vec3 forward = glm::vec3(transform[2]);
            glm::vec3 translation = glm::vec3(transform[3]);

            float lenX = glm::length(right);
            right = (lenX > kEpsilon) ? right / lenX : right;
            float lenY = glm::length(up);
            up = (lenY > kEpsilon) ? up / lenY : up;
            float lenZ = glm::length(forward);
            forward = (lenZ > kEpsilon) ? forward / lenZ : forward;

            glm::mat4 textTransform(1.0f);
            textTransform[0] = glm::vec4(right, 0.0f);
            textTransform[1] = glm::vec4(up, 0.0f);
            textTransform[2] = glm::vec4(forward, 0.0f);
            textTransform[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            TextObject* textObj = new TextObject(uiManager, textTransform, name + "_text", textColor, text, font, Corner::Center);
            textObj->setVerticalOffsetRatio(0.3f);
            this->addChild(textObj);
        }

        void click() {
            if (onClick) {
                onClick();
            }
        }

    private:
        std::function<void()> onClick;
    };

    struct Character {
        glm::ivec2 size;
        glm::ivec2 bearing;
        unsigned int advance;
        Texture* texture;
        std::vector<VkDescriptorSet> descriptorSets;
    };
    struct Font {
        std::string name;
        int fontSize = 0;
        int ascent = 0;
        int descent = 0;
        int lineHeight = 0;
        int maxGlyphHeight = 0;
        std::map<char, Character> characters;
    };

    struct LayoutRect {
        glm::vec2 position;
        glm::vec2 size;
    };

    class UIManager {
    public:
        UIManager(Renderer* renderer, std::string& fontDirectory);
        ~UIManager();

        void addObject(UIObject* object);
        void addObject(TextObject* object);
        void removeObject(const std::string& name);
        UIObject* getObject(const std::string& name);
        TextObject* getTextObject(const std::string& name);
        std::map<std::string, std::variant<UIObject*, TextObject*>>& getObjects() { return objects; }
        void renderUI(VkCommandBuffer commandBuffer, RenderNode& node, uint32_t frameIndex);
        void clear();
        void loadTextures();
        void loadFonts();
        UIObject* processMouseMovement(GLFWwindow* window, double xpos, double ypos);

        LayoutRect resolveDesignRect(std::variant<UIObject*, TextObject*> node, const LayoutRect& parentRect);
        LayoutRect toPixelRect(const LayoutRect& designRect, const glm::vec2& canvasOrigin, float layoutScale);

    private:
        Renderer* renderer;
        std::map<std::string, std::variant<UIObject*, TextObject*>> objects;
        std::map<std::string, Font> fonts;
        std::string fontDirectory = "";
    };
};