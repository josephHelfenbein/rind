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
#include <variant>
#include <functional>
#include <string>
#include <vector>
#include <unordered_map>

namespace engine {
    class UIManager;
    enum class UIType {
        Generic,
        Button,
        Checkbox,
        Slider,
        Text
    };
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
        TextObject(
            UIManager* uiManager,
            const glm::mat4& transform,
            const std::string& name,
            const glm::vec4& tint,
            const std::string& text,
            const std::string& font,
            const Corner& anchorCorner = Corner::Center
        );
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
        const glm::vec4& getTint() const { return tint; }
        void setTint(const glm::vec4& tint) { this->tint = tint; }
        const Corner& getAnchorCorner() const { return anchorCorner; }
        glm::vec2 getScale() const;
        const UIType& getType() const { return type; }
        float getVerticalOffsetRatio() const { return verticalOffsetRatio; }
        void setVerticalOffsetRatio(float ratio) { verticalOffsetRatio = ratio; }

    private:
        UIManager* uiManager;
        std::string name;
        UIType type = UIType::Text;
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
        UIObject(
            UIManager* uiManager,
            const glm::mat4& transform,
            const std::string& name,
            const glm::vec4& tint,
            const std::string& texture,
            const Corner& anchorCorner = Corner::Center,
            std::function<void()>* onHover = nullptr,
            std::function<void()>* onStopHover = nullptr,
            const UIType& type = UIType::Generic
        );
        virtual ~UIObject();

        const std::string& getName() const { return name; }
        const glm::mat4& getTransform() const { return transform; }
        void setTransform(const glm::mat4& transform) { this->transform = transform; }

        const std::string& getTexture() const { return texture; }
        void setTexture(const std::string& texture) { this->texture = texture; textureDirtyFrames = -1; }

        const std::vector<VkDescriptorSet>& getDescriptorSets() const { return descriptorSets; }
        void setDescriptorSets(const std::vector<VkDescriptorSet>& descriptorSets) { this->descriptorSets = descriptorSets; }

        void addChild(UIObject* child);
        void addChild(TextObject* child);
        void removeChild(UIObject* child);
        void removeChild(TextObject* child);
        std::vector<std::variant<UIObject*, TextObject*>>& getChildren() { return children; }
        UIObject* getParent() const { return parent; }
        void setParent(UIObject* parent) { this->parent = parent; }

        void loadTexture();
        void loadTextureForFrame(uint32_t frame);
        bool isTextureDirty() const { return textureDirtyFrames != 0; }

        void setEnabled(bool enabled) { this->enabled = enabled; }
        bool isEnabled() const { return enabled; }
        const UIType& getType() const { return type; }
        const glm::vec4& getTint() const { return tint; }
        void setTint(const glm::vec4& tint) { this->tint = tint; }
        const glm::vec4& getUVClip() const { return uvClip; }
        void setUVClip(const glm::vec4& uvClip) { this->uvClip = uvClip; }
        const Corner& getAnchorCorner() const { return anchorCorner; }
        std::function<void()>* getOnHover() const { return onHover; }
        std::function<void()>* getOnStopHover() const { return onStopHover; }
        void setOnHover(std::function<void()>* onHover) {
            if (this->onHover) {
                delete this->onHover;
            }
            this->onHover = onHover;
        }
        void setOnStopHover(std::function<void()>* onStopHover) {
            if (this->onStopHover) {
                delete this->onStopHover;
            }
            this->onStopHover = onStopHover;
        }

        UIManager* getUIManager() const { return uiManager; }

    private:
        UIManager* uiManager;
        std::string name;
        UIType type = UIType::Generic;
        glm::vec4 tint;
        glm::vec4 uvClip = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
        glm::mat4 transform;
        Corner anchorCorner;
        std::string texture;
        std::vector<VkDescriptorSet> descriptorSets;
        UIObject* parent = nullptr;
        std::vector<std::variant<UIObject*, TextObject*>> children;
        std::function<void()>* onHover;
        std::function<void()>* onStopHover;
        bool enabled = true;
        int textureDirtyFrames = 0;
    };

    class ButtonObject : public UIObject {
    public:
        ButtonObject(
            UIManager* uiManager,
            const glm::mat4& transform,
            const std::string& name,
            const glm::vec4& tint,
            const glm::vec4& textColor,
            const std::string& texture,
            const std::string& text,
            const std::string& font,
            std::function<void()> onClick,
            const Corner& anchorCorner = Corner::Center
        );

        void click();

    private:
        std::function<void()> onClick;
        engine::AudioManager* audioManager = nullptr;
    };

    class CheckboxObject : public UIObject {
    public:
        CheckboxObject(
            UIManager* uiManager,
            const glm::mat4& transform,
            const std::string& name,
            const glm::vec4& tint,
            bool initialState,
            bool& toggleBool,
            const Corner& anchorCorner = Corner::Center,
            std::vector<CheckboxObject*> boundBools = {}
        ); 

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
        SliderObject(
            UIManager* uiManager,
            const glm::mat4& transform,
            const std::string& name,
            float minValue,
            float maxValue,
            float& boundValue,
            const Corner& anchorCorner = Corner::Center,
            std::string textSuffix = "",
            bool isInteger = false,
            float textMultiplier = 1.0f,
            float overrideValue = 0.0f,
            std::string overrideText = ""
        ) : UIObject(uiManager, transform, name, glm::vec4(1.0f), "ui_slider_background", anchorCorner, nullptr, nullptr, UIType::Slider), minValue(minValue), maxValue(maxValue), boundValue(boundValue), isInteger(isInteger), textSuffix(textSuffix), textMultiplier(textMultiplier), overrideValue(overrideValue), overrideText(std::move(overrideText)) {
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
                    glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(1.5f, 1.5f, 1.0f)), glm::vec3(-165.0f, 0.0f, 0.0f)),
                    name + "_valueText",
                    glm::vec4(1.0f),
                    "",
                    "Lato",
                    Corner::Right
                );
                this->addChild(valueTextObject);
                refreshValueText();
                computeSliderDesignWidth();
                updateKnobPosition();
            }

        void setValue(float value) {
            boundValue = glm::clamp(value, minValue, maxValue);
            updateKnobPosition();
            refreshValueText();
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
        float overrideValue = 0.0f;
        std::string overrideText;

        void refreshValueText() {
            float compareValue = isInteger ? static_cast<float>(static_cast<int>(boundValue + 0.5f)) : boundValue;
            if (!overrideText.empty() && compareValue <= overrideValue) {
                valueTextObject->setText(overrideText);
            } else if (isInteger) {
                valueTextObject->setText(std::to_string(static_cast<int>(boundValue * textMultiplier + 0.5f)) + textSuffix);
            } else {
                valueTextObject->setText(std::to_string(boundValue * textMultiplier) + textSuffix);
            }
        }

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
        std::unordered_map<char, Character> characters;
    };

    struct LayoutRect {
        glm::vec2 position;
        glm::vec2 size;
    };

    class UIManager {
    public:
        UIManager(Renderer* renderer);
        ~UIManager();

        void addObject(UIObject* object);
        void addObject(TextObject* object);
        void removeObject(const std::string& name);
        void removeObjectDeferred(const std::string& name);
        void processPendingRemovals();
        UIObject* getObject(const std::string& name);
        TextObject* getTextObject(const std::string& name);
        std::unordered_map<std::string, std::variant<UIObject*, TextObject*>>& getObjects() { return objects; }
        void renderUI(VkCommandBuffer commandBuffer, uint32_t frameIndex);
        void clear();
        void loadTextures();
        void reloadFontDescriptorSets();
        void loadFonts();
        UIObject* processMouseMovement(GLFWwindow* window, double xpos, double ypos);

        Renderer* getRenderer() const { return renderer; }

        LayoutRect resolveDesignRect(std::variant<UIObject*, TextObject*> node, const LayoutRect& parentRect);
        LayoutRect toPixelRect(const LayoutRect& designRect, const glm::vec2& canvasOrigin, float layoutScale);
        std::unordered_map<std::string, std::variant<UIObject*, TextObject*>>& getRootObjects() { return rootObjects; }

        void createCursorObject() {
            cursor = new UIObject(
                this,
                glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(0.1f, 0.1f, 1.0f)), glm::vec3(0.0f, 0.0f, -10.0f)),
                "controllerCursor",
                glm::vec4(1.0f),
                "ui_cursor_cursor",
                Corner::TopLeft
            );
            cursor->setEnabled(false);
        }
        void setFakeCursorPosition(const glm::dvec2& mousePos) {
            float xscale = 1.0f, yscale = 1.0f;
            glfwGetWindowContentScale(renderer->getWindow(), &xscale, &yscale);
            float contentScale = std::max(xscale, yscale);
            float layoutScale = std::max(renderer->getUIScale() * contentScale, 0.0001f);
            const float cursorScale = 0.1f;
            glm::vec2 texOffset(0.0f);
            if (Texture* tex = renderer->getTextureManager()->getTexture(cursor->getTexture())) {
                texOffset = glm::vec2(static_cast<float>(tex->width), static_cast<float>(tex->height)) * 0.5f;
            }
            glm::vec3 designPos(
                static_cast<float>(mousePos.x) * std::max(xscale, 1.0f) / (layoutScale * cursorScale) - texOffset.x,
                -static_cast<float>(mousePos.y) * std::max(yscale, 1.0f) / (layoutScale * cursorScale) + texOffset.y,
                -10.0f
            );
            cursor->setTransform(
                glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(cursorScale, cursorScale, 1.0f)), designPos)
            );
        }
        void setCursorEnabled(bool enabled) {
            showCursor = enabled;
            cursor->setEnabled(enabled);
        }

    private:
        Renderer* renderer;
        std::unordered_map<std::string, std::variant<UIObject*, TextObject*>> objects;
        std::unordered_map<std::string, std::variant<UIObject*, TextObject*>> rootObjects;
        std::unordered_map<std::string, Font> fonts;
        UIObject* cursor = nullptr;
        bool showCursor = false;
        std::vector<std::string> pendingRemovals;
    };
};
