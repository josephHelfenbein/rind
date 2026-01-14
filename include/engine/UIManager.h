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
        Center,
        Top,
        Bottom,
        Left,
        Right
    };

    class TextObject {
    public:
        TextObject(UIManager* uiManager, glm::mat4 transform, std::string name, glm::vec4 tint, std::string text, std::string font, Corner anchorCorner = Corner::Center);
        ~TextObject();

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
        glm::vec4 getTint() const { return tint; }
        Corner getAnchorCorner() const { return anchorCorner; }
        glm::vec2 getScale() const;
        float getVerticalOffsetRatio() const { return verticalOffsetRatio; }
        void setVerticalOffsetRatio(float ratio) { verticalOffsetRatio = ratio; }

    private:
        UIManager* uiManager;
        std::string name;
        glm::vec4 tint;
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
        UIObject(UIManager* uiManager, glm::mat4 transform, std::string name, glm::vec4 tint, std::string texture, Corner anchorCorner = Corner::Center, std::function<void()>* onHover = nullptr, std::function<void()>* onStopHover = nullptr);
        virtual ~UIObject();

        const std::string& getName() const { return name; }
        const glm::mat4& getTransform() const { return transform; }
        void setTransform(const glm::mat4& transform) { this->transform = transform; }

        const std::string& getTexture() const { return texture; }
        void setTexture(const std::string& texture) { this->texture = texture; descriptorSets.clear(); }

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
        glm::vec4 getTint() const { return tint; }
        Corner getAnchorCorner() const { return anchorCorner; }
        std::function<void()>* getOnHover() const { return onHover; }
        std::function<void()>* getOnStopHover() const { return onStopHover; }

        UIManager* getUIManager() const { return uiManager; }

    private:
        UIManager* uiManager;
        std::string name;
        glm::vec4 tint;
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
        ButtonObject(UIManager* uiManager, glm::mat4 transform, std::string name, glm::vec4 tint, glm::vec4 textColor, std::string texture, std::string text, std::string font, std::function<void()> onClick, Corner anchorCorner = Corner::Center)
            : UIObject(uiManager, transform, name, tint, texture, anchorCorner), onClick(onClick) {
                TextObject* textObj = new TextObject(uiManager, glm::mat4(1.0f), name + "_text", textColor, text, font, Corner::Center);
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

    class CheckboxObject : public UIObject {
    public:
        CheckboxObject(UIManager* uiManager, glm::mat4 transform, std::string name, glm::vec4 tint, bool initialState, bool& toggleBool, Corner anchorCorner = Corner::Center, std::vector<CheckboxObject*> boundBools = {})
            : UIObject(uiManager, transform, name, tint, "", anchorCorner), checkState(initialState), checked(toggleBool), boundBools(boundBools) {
                if (initialState) {
                    setTexture(checkedTexture);
                } else {
                    setTexture(uncheckedTexture);
                }
            }

        bool isChecked() const { return checked; }

        void setBoundBools(const std::vector<CheckboxObject*>& boundBools) {
            this->boundBools = boundBools;
        }

        void toggle();

    private:
        bool& checked;
        bool checkState = false;
        std::vector<CheckboxObject*> boundBools;
        std::string checkedTexture = "ui_checkbox_checked";
        std::string uncheckedTexture = "ui_checkbox_unchecked";
    };

    class SliderObject : public UIObject {
    public:
        SliderObject(UIManager* uiManager, glm::mat4 transform, std::string name, float minValue, float maxValue, float& boundValue, Corner anchorCorner = Corner::Center, std::string textSuffix = "", bool isInteger = false, float textMultiplier = 1.0f)
            : UIObject(uiManager, transform, name, glm::vec4(1.0f), "ui_slider_background", anchorCorner), minValue(minValue), maxValue(maxValue), boundValue(boundValue), isInteger(isInteger), textSuffix(textSuffix), textMultiplier(textMultiplier) {
                knobObject = new UIObject(
                    uiManager,
                    glm::scale(glm::mat4(1.0f), glm::vec3(0.04f, 0.04f, 1.0f)),
                    name + "_knob",
                    glm::vec4(1.0f),
                    "ui_slider_knob",
                    Corner::Center
                );
                this->addChild(knobObject);
                valueTextObject = new TextObject(
                    uiManager,
                    glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(1.5f, 1.5f, 1.0f)), glm::vec3(-30.0f, 0.0f, 0.0f)),
                    name + "_valueText",
                    glm::vec4(1.0f),
                    "",
                    "Lato",
                    Corner::Left
                );
                this->addChild(valueTextObject);
                if (isInteger) {
                    valueTextObject->setText(std::to_string(static_cast<int>(boundValue * textMultiplier + 0.5f)) + textSuffix);
                } else {
                    valueTextObject->setText(std::to_string(boundValue * textMultiplier) + textSuffix);
                }
                computeSliderDesignWidth();
                updateKnobPosition();
            }

        void setValue(float value) {
            boundValue = glm::clamp(value, minValue, maxValue);
            updateKnobPosition();
            if (isInteger) {
                valueTextObject->setText(std::to_string(static_cast<int>(boundValue * textMultiplier + 0.5f)) + textSuffix);
            } else {
                valueTextObject->setText(std::to_string(boundValue * textMultiplier) + textSuffix);
            }
        }

        float getValue() const {
            return boundValue;
        }

        float getSliderValueFromMouse(GLFWwindow* window);
        void computeSliderDesignWidth();
    private:
        float minValue;
        float maxValue;
        float& boundValue;
        UIObject* knobObject = nullptr;
        TextObject* valueTextObject = nullptr;
        bool isInteger = false;
        std::string textSuffix;
        float textMultiplier = 1.0f;
        float sliderDesignWidth = 1.0f;
        float sliderDesignPosX = 0.0f;
        
        void updateKnobPosition() {
            float ratio = (boundValue - minValue) / (maxValue - minValue);
            float knobScaleX = knobObject->getTransform()[0][0];
            float knobX = ratio * sliderDesignWidth / knobScaleX - (sliderDesignWidth / (2.0f * knobScaleX));
            glm::mat4 knobTransform = glm::translate(
                glm::scale(glm::mat4(1.0f), glm::vec3(0.04f, 0.04f, 1.0f)),
                glm::vec3(knobX, 0.0f, 0.0f)
            );
            knobObject->setTransform(knobTransform);
        }
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
        UIManager(Renderer* renderer, std::string fontDirectory);
        ~UIManager();

        void addObject(UIObject* object);
        void addObject(TextObject* object);
        void removeObject(const std::string& name);
        void removeObjectDeferred(const std::string& name);
        void processPendingRemovals();
        UIObject* getObject(const std::string& name);
        TextObject* getTextObject(const std::string& name);
        std::map<std::string, std::variant<UIObject*, TextObject*>>& getObjects() { return objects; }
        void renderUI(VkCommandBuffer commandBuffer, RenderNode& node, uint32_t frameIndex);
        void clear();
        void loadTextures();
        void loadFonts();
        UIObject* processMouseMovement(GLFWwindow* window, double xpos, double ypos);

        Renderer* getRenderer() const { return renderer; }

        LayoutRect resolveDesignRect(std::variant<UIObject*, TextObject*> node, const LayoutRect& parentRect);
        LayoutRect toPixelRect(const LayoutRect& designRect, const glm::vec2& canvasOrigin, float layoutScale);

    private:
        Renderer* renderer;
        std::map<std::string, std::variant<UIObject*, TextObject*>> objects;
        std::map<std::string, Font> fonts;
        std::string fontDirectory = "";
        std::vector<std::string> pendingRemovals;
    };
};
