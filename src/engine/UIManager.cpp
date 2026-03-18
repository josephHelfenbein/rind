#include <engine/UIManager.h>
#include <algorithm>
#include <utility>
#include <filesystem>
#include <limits>
#include <engine/Renderer.h>
#include <engine/AudioManager.h>

engine::UIObject::UIObject(
    UIManager* uiManager,
    const glm::mat4& transform,
    const std::string& name,
    const glm::vec4& tint,
    const std::string& texture,
    const Corner& anchorCorner,
    std::function<void()>* onHover,
    std::function<void()>* onStopHover,
    const UIType& type
) : uiManager(uiManager), name(name), tint(tint), transform(transform), anchorCorner(anchorCorner), texture(texture), onHover(onHover), onStopHover(onStopHover), type(type) {
        uiManager->addObject(this);
    }

engine::TextObject::TextObject(
    UIManager* uiManager,
    const glm::mat4& transform,
    const std::string& name,
    const glm::vec4& tint,
    const std::string& text,
    const std::string& font,
    const Corner& anchorCorner
) : uiManager(uiManager), name(name), tint(tint), text(text), font(font), transform(transform), anchorCorner(anchorCorner) {
        uiManager->addObject(this);
    }

engine::UIObject::~UIObject() {
    if (uiManager->getRenderer()->getHoveredObject() == this) {
        uiManager->getRenderer()->setHoveredObject(nullptr);
    }
    if (!descriptorSets.empty()) {
        engine::Renderer* renderer = uiManager->getRenderer();
        GraphicsShader* shader = renderer->getShaderManager()->getGraphicsShader("ui");
        if (shader && shader->descriptorPool != VK_NULL_HANDLE) {
            vkFreeDescriptorSets(renderer->getDevice(), shader->descriptorPool,
                static_cast<uint32_t>(descriptorSets.size()), descriptorSets.data());
        }
        descriptorSets.clear();
    }
    delete onHover;
    delete onStopHover;
    std::unordered_map<std::string, std::variant<UIObject*, TextObject*>>& roots = uiManager->getRootObjects();
    if (roots.find(name) != roots.end()) {
        roots.erase(roots.find(name));
    }
    for (auto& child : children) {
        if (std::holds_alternative<TextObject*>(child)) {
            delete std::get<TextObject*>(child);
        } else {
            delete std::get<UIObject*>(child);
        }
    }
    children.clear();
    auto it = uiManager->getObjects().find(name);
    if (it != uiManager->getObjects().end()) {
        uiManager->getObjects().erase(it);
    }
}

engine::TextObject::~TextObject() {
    auto it = uiManager->getObjects().find(name);
    if (it != uiManager->getObjects().end()) {
        uiManager->getObjects().erase(it);
    }
}

void engine::UIObject::addChild(UIObject* child) {
    if (child->getParent()) {
        child->getParent()->removeChild(child);
    } else {
        std::unordered_map<std::string, std::variant<UIObject*, TextObject*>>& roots = uiManager->getRootObjects();
        if (roots.find(child->getName()) != roots.end()) {
            roots.erase(roots.find(child->getName()));
        }
    }
    children.push_back(child);
    child->setParent(this);
}

void engine::UIObject::addChild(TextObject* child) {
    if (child->getParent()) {
        child->getParent()->removeChild(child);
    } else {
        std::unordered_map<std::string, std::variant<UIObject*, TextObject*>>& roots = uiManager->getRootObjects();
        if (roots.find(child->getName()) != roots.end()) {
            roots.erase(roots.find(child->getName()));
        }
    }
    children.push_back(child);
    child->setParent(this);
}

void engine::UIObject::removeChild(UIObject* child) {
    auto it = std::remove_if(children.begin(), children.end(), [child](const auto& entry) {
        return std::holds_alternative<UIObject*>(entry) && std::get<UIObject*>(entry) == child;
    });
    children.erase(it, children.end());
    child->setParent(nullptr);
    uiManager->getRootObjects().insert({child->getName(), child});
}

void engine::UIObject::removeChild(TextObject* child) {
    auto it = std::remove_if(children.begin(), children.end(), [child](const auto& entry) {
        return std::holds_alternative<TextObject*>(entry) && std::get<TextObject*>(entry) == child;
    });
    children.erase(it, children.end());
    child->setParent(nullptr);
    uiManager->getRootObjects().insert({child->getName(), child});
}

void engine::UIObject::loadTexture() {
    if (texture.empty()) {
        return;
    }
    engine::Renderer* renderer = getUIManager()->getRenderer();
    GraphicsShader* shader = renderer->getShaderManager()->getGraphicsShader("ui");
    engine::Texture* texture = renderer->getTextureManager()->getTexture(getTexture());
    if (!texture) {
        std::cout << "Warning: Texture " << getTexture() << " for UIObject " << name << " not found.\n";
        return;
    }
    std::vector<Texture*> textures = { texture };
    std::vector<VkBuffer> buffers;
    if (!descriptorSets.empty()) {
        shader->updateDescriptorSets(renderer, descriptorSets, textures, buffers);
    } else {
        setDescriptorSets(shader->createDescriptorSets(renderer, textures, buffers));
    }
    textureDirtyFrames = 0;
}

void engine::UIObject::loadTextureForFrame(uint32_t frame) {
    if (texture.empty()) {
        return;
    }
    engine::Renderer* renderer = getUIManager()->getRenderer();
    GraphicsShader* shader = renderer->getShaderManager()->getGraphicsShader("ui");
    engine::Texture* texture = renderer->getTextureManager()->getTexture(getTexture());
    if (!texture) {
        return;
    }
    std::vector<Texture*> textures = { texture };
    std::vector<VkBuffer> buffers;
    if (!descriptorSets.empty()) {
        shader->updateDescriptorSets(renderer, descriptorSets, textures, buffers, static_cast<int>(frame));
    } else {
        setDescriptorSets(shader->createDescriptorSets(renderer, textures, buffers));
    }
    if (textureDirtyFrames < 0) {
        textureDirtyFrames = renderer->getMaxFramesInFlight() - 1;
    } else if (textureDirtyFrames > 0) {
        textureDirtyFrames--;
    }
}

engine::ButtonObject::ButtonObject(
    UIManager* uiManager,
    const glm::mat4& transform,
    const std::string& name,
    const glm::vec4& tint,
    const glm::vec4& textColor,
    const std::string& texture,
    const std::string& text,
    const std::string& font,
    std::function<void()> onClick,
    const Corner& anchorCorner
) : UIObject(uiManager, transform, name, tint, texture, anchorCorner, nullptr, nullptr, UIType::Button), onClick(onClick) {
        TextObject* textObj = new TextObject(uiManager, glm::mat4(1.0f), name + "_text", textColor, text, font, Corner::Center);
        this->addChild(textObj);
        setOnHover(new std::function<void()>([this]() {
            glm::vec4 currentTint = this->getTint();
            this->setTint(currentTint + glm::vec4(0.5f, 0.5f, 0.5f, 0.0f));
            this->loadTexture();
        }));
        setOnStopHover(new std::function<void()>([this]() {
            glm::vec4 currentTint = this->getTint();
            this->setTint(currentTint - glm::vec4(0.5f, 0.5f, 0.5f, 0.0f));
            this->loadTexture();
        }));
        audioManager = uiManager->getRenderer()->getAudioManager();
    }

void engine::ButtonObject::click() {
    if (onClick) {
        audioManager->playSound("ui_press", 0.5f, 0.0f, true);
        onClick();
    }
}

engine::CheckboxObject::CheckboxObject(
    UIManager* uiManager,
    const glm::mat4& transform,
    const std::string& name,
    const glm::vec4& tint,
    bool initialState,
    bool& toggleBool,
    const Corner& anchorCorner,
    std::vector<CheckboxObject*> boundBools
) : UIObject(uiManager, transform, name, tint, "", anchorCorner, nullptr, nullptr, UIType::Checkbox), checkState(initialState), checked(toggleBool), boundBools(boundBools) {
        if (initialState) {
            setTexture(checkedTexture);
        } else {
            setTexture(uncheckedTexture);
        }
        setOnHover(new std::function<void()>([this]() {
            glm::vec4 currentTint = this->getTint();
            this->setTint(currentTint + glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
            this->loadTexture();
        }));
        setOnStopHover(new std::function<void()>([this]() {
            glm::vec4 currentTint = this->getTint();
            this->setTint(currentTint - glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
            this->loadTexture();
        }));
    }

 void engine::CheckboxObject::toggle() {
    checked = !checked;
    checkState = !checkState;
    setTexture(checkState ? checkedTexture : uncheckedTexture);
    loadTexture();

    if (checkState) {
        for (auto& boundCheckbox : boundBools) {
            if (boundCheckbox->isChecked()) {
                boundCheckbox->toggle();
            }
        }
    } else {
        bool anyChecked = false;
        for (auto& boundCheckbox : boundBools) {
            if (boundCheckbox->isChecked()) {
                anyChecked = true;
                break;
            }
        }
        if (!anyChecked && !boundBools.empty()) {
            boundBools[0]->toggle();
        }
    }
}

void engine::SliderObject::computeSliderDesignWidth() {
    Renderer* renderer = getUIManager()->getRenderer();
    glm::vec2 swapExtentF = glm::vec2(static_cast<float>(renderer->getSwapChainExtent().width), static_cast<float>(renderer->getSwapChainExtent().height));
    float xscale = 1.0f, yscale = 1.0f;
    glfwGetWindowContentScale(renderer->getWindow(), &xscale, &yscale);
    float contentScale = std::max(xscale, yscale);
    float layoutScale = std::max(renderer->getUIScale() * contentScale, 0.0001f);
    LayoutRect rootAnchorRect = {
        .position = glm::vec2(0.0f, 0.0f),
        .size = swapExtentF / layoutScale
    };
    std::vector<UIObject*> parentChain;
    UIObject* current = this;
    while (current != nullptr) {
        parentChain.push_back(current);
        current = current->getParent();
    }
    std::reverse(parentChain.begin(), parentChain.end());
    LayoutRect anchorRect = rootAnchorRect;
    for (size_t i = 0; i < parentChain.size(); ++i) {
        bool isRoot = (parentChain[i]->getParent() == nullptr);
        const LayoutRect& parentRect = isRoot ? rootAnchorRect : anchorRect;
        anchorRect = getUIManager()->resolveDesignRect(parentChain[i], parentRect);
    }
    sliderDesignWidth = anchorRect.size.x;
    sliderDesignPosX = anchorRect.position.x;
}

float engine::SliderObject::getSliderValueFromMouse(GLFWwindow* window) {
    computeSliderDesignWidth();
    double mouseX, mouseY;
    Renderer* renderer = getUIManager()->getRenderer();
    InputManager* inputManager = renderer->getInputManager();
    if (inputManager->isControllerMode()) {
        const glm::dvec2& pos = inputManager->getFakeControllerCursor();
        mouseX = pos.x;
        mouseY = pos.y;
    } else {
        glfwGetCursorPos(window, &mouseX, &mouseY);
    }
    glm::vec2 swapExtentF = glm::vec2(static_cast<float>(renderer->getSwapChainExtent().width), static_cast<float>(renderer->getSwapChainExtent().height));
    float xscale = 1.0f, yscale = 1.0f;
    glfwGetWindowContentScale(window, &xscale, &yscale);
    float contentScale = std::max(xscale, yscale);
    float layoutScale = std::max(renderer->getUIScale() * contentScale, 0.0001f);
    glm::vec2 mousePosF(static_cast<float>(mouseX) * std::max(xscale, 1.0f), static_cast<float>(mouseY) * std::max(yscale, 1.0f));
    mousePosF.y = swapExtentF.y - mousePosF.y;
    const float invLayout = layoutScale > 0.0f ? 1.0f / layoutScale : 0.0f;
    glm::vec2 mouseDesign = mousePosF * invLayout;
    float relativeX = mouseDesign.x - sliderDesignPosX;
    float ratio = glm::clamp(relativeX / sliderDesignWidth, 0.0f, 1.0f);
    return minValue + ratio * (maxValue - minValue);
}

engine::UIManager::UIManager(Renderer* renderer, const std::string& fontDirectory)
    : renderer(renderer), fontDirectory(fontDirectory) {
        renderer->registerUIManager(this);
    }

engine::UIManager::~UIManager() {
    clear();
    VkDevice device = renderer->getDevice();
    for (auto& [name, font] : fonts) {
        for (auto& [charKey, character] : font.characters) {
            if (character.texture) {
                if (character.texture->imageSampler != VK_NULL_HANDLE) {
                    vkDestroySampler(device, character.texture->imageSampler, nullptr);
                }
                if (character.texture->imageView != VK_NULL_HANDLE) {
                    vkDestroyImageView(device, character.texture->imageView, nullptr);
                }
                if (character.texture->image != VK_NULL_HANDLE) {
                    vkDestroyImage(device, character.texture->image, nullptr);
                }
                if (character.texture->imageMemory != VK_NULL_HANDLE) {
                    vkFreeMemory(device, character.texture->imageMemory, nullptr);
                }
                delete character.texture;
            }
        }
    }
}

void engine::UIManager::addObject(UIObject* object) {
    if (objects.find(object->getName()) != objects.end()) {
        std::cout << "Warning: Duplicate UIObject name detected: " << object->getName() << ". Overwriting existing object.\n";
        if (std::holds_alternative<TextObject*>(objects[object->getName()])) {
            delete std::get<TextObject*>(objects[object->getName()]);
        } else{
            delete std::get<UIObject*>(objects[object->getName()]);
        }
    }
    objects[object->getName()] = object;
    if (object->getParent() == nullptr) {
        rootObjects.insert({object->getName(), object});
    }
}

void engine::UIManager::addObject(TextObject* object) {
    const std::string& key = object->getName();
    if (objects.find(key) != objects.end()) {
        std::cout << "Warning: Duplicate TextObject name detected: " << key << ". Overwriting existing object.\n";
        removeObject(key);
    }
    objects[key] = object;
    if (object->getParent() == nullptr) {
        rootObjects.insert({object->getName(), object});
    }
}

glm::vec2 engine::TextObject::getScale() const {
    float scaleX = transform[0][0];
    float scaleY = transform[1][1];
    return glm::vec2(scaleX, scaleY);
}

void engine::UIManager::removeObject(const std::string& name) {
    auto it = objects.find(name);
    if (it != objects.end()) {
        if (rootObjects.find(it->first) != rootObjects.end()) {
            rootObjects.erase(rootObjects.find(it->first));
        }
        if (std::holds_alternative<TextObject*>(it->second)) {
            TextObject* obj = std::get<TextObject*>(it->second);
            if (obj->getParent()) {
                obj->getParent()->removeChild(obj);
            }
            delete obj;
        } else {
            UIObject* obj = std::get<UIObject*>(it->second);
            if (obj->getParent()) {
                obj->getParent()->removeChild(obj);
            }
            delete obj;
        }
    }
}

void engine::UIManager::removeObjectDeferred(const std::string& name) {
    pendingRemovals.push_back(name);
}

void engine::UIManager::processPendingRemovals() {
    for (const auto& name : pendingRemovals) {
        removeObject(name);
    }
    pendingRemovals.clear();
}

engine::UIObject* engine::UIManager::getObject(const std::string& name) {
    auto it = objects.find(name);
    if (it != objects.end()) {
        if (std::holds_alternative<TextObject*>(it->second)) {
            return nullptr;
        }
        return std::get<UIObject*>(it->second);
    }
    return nullptr;
}

engine::TextObject* engine::UIManager::getTextObject(const std::string& name) {
    auto it = objects.find(name);
    if (it != objects.end()) {
        if (std::holds_alternative<UIObject*>(it->second)) {
            return nullptr;
        }
        return std::get<TextObject*>(it->second);
    }
    return nullptr;
}

void engine::UIManager::clear() {
    if (renderer) {
        renderer->setHoveredObject(nullptr);
        if (renderer->getFPSCounter()) {
            renderer->setFPSCounter(nullptr);
        }
    }
    auto roots = std::move(rootObjects);
    rootObjects.clear();
    objects.clear();
    cursor = nullptr;
    for (const auto& [name, obj] : roots) {
        if (std::holds_alternative<TextObject*>(obj)) {
            delete std::get<TextObject*>(obj);
        } else {
            delete std::get<UIObject*>(obj);
        }
    }
    for (auto& [name, font] : fonts) {
        for (auto& [ch, character] : font.characters) {
            character.descriptorSets.clear();
        }
    }
}

void engine::UIManager::loadTextures() {
    for (auto& [name, found] : objects) {
        if (!std::holds_alternative<UIObject*>(found)) continue;
        UIObject* object = std::get<UIObject*>(found);
        object->loadTexture();
    }
}

void engine::UIManager::reloadFontDescriptorSets() {
    GraphicsShader* shader = renderer->getShaderManager()->getGraphicsShader("text");
    if (!shader) return;
    for (auto& [name, font] : fonts) {
        for (auto& [ch, character] : font.characters) {
            if (!character.descriptorSets.empty()) continue;
            std::vector<Texture*> textures = { character.texture };
            std::vector<VkBuffer> buffers;
            character.descriptorSets = shader->createDescriptorSets(renderer, textures, buffers);
        }
    }
}

void engine::UIManager::loadFonts() {
    FT_Library ft;
    if (FT_Init_FreeType(&ft)) {
        std::cout << "Error: Could not init FreeType Library\n";
        return;
    }
    auto scanAndLoadFonts = [&](auto& self, const std::string& directory, std::string parentPath) -> void {
        std::vector<std::string> fontFiles = engine::scanDirectory(directory);
        for (const auto& filePath : fontFiles) {
            if (std::filesystem::is_directory(filePath)) {
                self(self, filePath, parentPath + std::filesystem::path(filePath).filename().string() + "_");
                continue;
            }
            if (!std::filesystem::is_regular_file(filePath)) {
                continue;
            }
            std::string fileName = std::filesystem::path(filePath).filename().string();
            std::string fontName = parentPath + std::filesystem::path(fileName).stem().string();
            if (fonts.find(fontName) != fonts.end()) {
                std::cout << "Warning: Duplicate font name detected: " << fontName << ". Skipping " << filePath << "\n";
                continue;
            }
            FT_Face face;
            if (FT_New_Face(ft, filePath.c_str(), 0, &face)) {
                std::cout << "Error: Failed to load font " << filePath << "\n";
                FT_Done_FreeType(ft);
                continue;
            }
            FT_Set_Pixel_Sizes(face, 0, 48);
            Font font = {
                .name = fontName,
                .fontSize = 48,
                .ascent = static_cast<int>(face->size->metrics.ascender >> 6),
                .descent = static_cast<int>(face->size->metrics.descender >> 6),
                .lineHeight = static_cast<int>(face->size->metrics.height >> 6),
                .maxGlyphHeight = 0
            };
            font.characters.clear();
            for (unsigned char c = 0; c < 128; c++) {
                if (FT_Load_Char(face, c, FT_LOAD_RENDER)) {
                    std::cout << "Warning: Failed to load Glyph " << c << " from font " << fontName << "\n";
                    continue;
                }
                Character character;
                character.texture = new Texture({ .name = fontName + std::to_string(c) });
                character.size = glm::ivec2(face->glyph->bitmap.width, face->glyph->bitmap.rows);
                character.bearing = glm::ivec2(face->glyph->bitmap_left, face->glyph->bitmap_top);
                const FT_Pos rawAdvance = std::max<FT_Pos>(face->glyph->advance.x, static_cast<FT_Pos>(0));
                character.advance = static_cast<unsigned int>(rawAdvance >> 6);
                if (face->glyph->bitmap.width == 0 || face->glyph->bitmap.rows == 0) {
                    unsigned char emptyPixel = 0;
                    character.texture->width = 1;
                    character.texture->height = 1;
                    character.texture->format = VK_FORMAT_R8_UNORM;
                    std::tie(character.texture->image, character.texture->imageMemory) = renderer->createImageFromPixels(
                        &emptyPixel,
                        sizeof(unsigned char),
                        1,
                        1,
                        1,
                        VK_SAMPLE_COUNT_1_BIT,
                        VK_FORMAT_R8_UNORM,
                        VK_IMAGE_TILING_OPTIMAL,
                        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        1,
                        0
                    );
                } else {
                    character.texture->width = face->glyph->bitmap.width;
                    character.texture->height = face->glyph->bitmap.rows;
                    character.texture->format = VK_FORMAT_R8_UNORM;
                    std::tie(character.texture->image, character.texture->imageMemory) = renderer->createImageFromPixels(
                        face->glyph->bitmap.buffer,
                        static_cast<VkDeviceSize>(face->glyph->bitmap.width) * static_cast<VkDeviceSize>(face->glyph->bitmap.rows) * sizeof(unsigned char),
                        face->glyph->bitmap.width,
                        face->glyph->bitmap.rows,
                        1,
                        VK_SAMPLE_COUNT_1_BIT,
                        VK_FORMAT_R8_UNORM,
                        VK_IMAGE_TILING_OPTIMAL,
                        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        1,
                        0
                    );
                }
                renderer->transitionImageLayout(
                    character.texture->image,
                    character.texture->format,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    1,
                    1
                );
                character.texture->imageView = renderer->createImageView(
                    character.texture->image,
                    character.texture->format,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    1,
                    VK_IMAGE_VIEW_TYPE_2D,
                    1
                );
                character.texture->imageSampler = renderer->createTextureSampler(
                    VK_FILTER_LINEAR,
                    VK_FILTER_LINEAR,
                    VK_SAMPLER_MIPMAP_MODE_LINEAR,
                    VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                    VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                    VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                    0.0f,
                    VK_FALSE,
                    1.0f,
                    VK_FALSE,
                    VK_COMPARE_OP_ALWAYS,
                    0.0f,
                    0.0f,
                    VK_BORDER_COLOR_INT_OPAQUE_BLACK,
                    VK_FALSE
                );
                GraphicsShader* shader = renderer->getShaderManager()->getGraphicsShader("text");
                std::vector<Texture*> textures = { character.texture };
                std::vector<VkBuffer> buffers;
                character.descriptorSets = shader->createDescriptorSets(renderer, textures, buffers);
                const char glyph = static_cast<char>(c);
                const int glyphHeight = character.size.y;
                font.characters.insert_or_assign(glyph, std::move(character));
                if (glyphHeight > font.maxGlyphHeight) {
                    font.maxGlyphHeight = glyphHeight;
                }
            }
            fonts.insert_or_assign(fontName, std::move(font));
            FT_Done_Face(face);
        }
    };
    scanAndLoadFonts(scanAndLoadFonts, fontDirectory, "");
    FT_Done_FreeType(ft);
}

engine::LayoutRect engine::UIManager::resolveDesignRect(std::variant<UIObject*, TextObject*> node, const LayoutRect& parentRect) {
    glm::vec2 size, position;
    Corner anchor;
    if (std::holds_alternative<TextObject*>(node)) {
        TextObject* textObj = std::get<TextObject*>(node);
        const auto& fontInfo = fonts[textObj->getFont()];
        float scaleX = textObj->getScale().x;
        float scaleY = textObj->getScale().y;
        if (textObj->getParent() != nullptr) {
            const float baseGlyphHeight = static_cast<float>(fontInfo.ascent - fontInfo.descent);
            const float targetHeight = parentRect.size.y * 0.6f;
            const float fitScale = targetHeight / std::max(baseGlyphHeight, 1.0f);
            scaleX *= fitScale;
            scaleY *= fitScale;
        }
        float penX = 0.0f;
        float minX = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest();
        for (const char& c : textObj->getText()) {
            const Character& ch = fontInfo.characters.at(c);
            float xpos = penX + ch.bearing.x * scaleX;
            float w = ch.size.x * scaleX;
            minX = std::min(minX, xpos);
            maxX = std::max(maxX, xpos + w);
            penX += ch.advance * scaleX;
        }
        float textWidth = (minX <= maxX) ? (maxX - minX) : 0.0f;
        const float glyphHeight = (fontInfo.ascent - fontInfo.descent) * scaleY;
        size = glm::vec2(textWidth, glyphHeight);
        position = glm::vec2(textObj->getTransform()[3][0], textObj->getTransform()[3][1]);
        anchor = textObj->getAnchorCorner();
    } else {
        UIObject* uiObj = std::get<UIObject*>(node);
        glm::vec2 scale = glm::vec2(uiObj->getTransform()[0][0], uiObj->getTransform()[1][1]);
        size = scale;
        if (!uiObj->getTexture().empty()) {
            if (Texture* tex = renderer->getTextureManager()->getTexture(uiObj->getTexture())) {
                glm::vec2 texSize = glm::vec2(static_cast<float>(tex->width), static_cast<float>(tex->height));
                size = texSize * scale;
            }
        }
        position = glm::vec2(uiObj->getTransform()[3][0], uiObj->getTransform()[3][1]);
        anchor = uiObj->getAnchorCorner();
    }
    switch (anchor) {
        case Corner::TopLeft:
            position += glm::vec2(0.0f, parentRect.size.y - size.y);
            break;
        case Corner::TopRight:
            position += glm::vec2(parentRect.size.x - size.x, parentRect.size.y - size.y);
            break;
        case Corner::BottomLeft:
            position += glm::vec2(0.0f, 0.0f);
            break;
        case Corner::BottomRight:
            position += glm::vec2(parentRect.size.x - size.x, 0.0f);
            break;
        case Corner::Center:
            position += glm::vec2((parentRect.size.x - size.x) / 2.0f, (parentRect.size.y - size.y) / 2.0f);
            break;
        case Corner::Top:
            position += glm::vec2((parentRect.size.x - size.x) / 2.0f, parentRect.size.y - size.y);
            break;
        case Corner::Bottom:
            position += glm::vec2((parentRect.size.x - size.x) / 2.0f, 0.0f);
            break;
        case Corner::Left:
            position += glm::vec2(0.0f, (parentRect.size.y - size.y) / 2.0f);
            break;
        case Corner::Right:
            position += glm::vec2(parentRect.size.x - size.x, (parentRect.size.y - size.y) / 2.0f);
            break;
    }
    if (std::holds_alternative<TextObject*>(node)) {
        TextObject* textObj = std::get<TextObject*>(node);
        position += glm::vec2(0.0f, -textObj->getVerticalOffsetRatio() * parentRect.size.y);
    }
    position += parentRect.position;
    return LayoutRect{ position, size };
};

engine::LayoutRect engine::UIManager::toPixelRect(const LayoutRect& designRect, const glm::vec2& canvasOrigin, float layoutScale) {
    glm::vec2 pixelPosition = canvasOrigin + designRect.position * layoutScale;
    glm::vec2 pixelSize = designRect.size * layoutScale;
    return LayoutRect{ pixelPosition, pixelSize };
};

void engine::UIManager::renderUI(VkCommandBuffer commandBuffer, uint32_t frameIndex) {
    const glm::vec2 swapExtentF = glm::vec2(renderer->getSwapChainExtent().width, renderer->getSwapChainExtent().height);
    float contentScale = 1.0f;
#ifdef __APPLE__
    float xscale = 1.0f, yscale = 1.0f;
    glfwGetWindowContentScale(renderer->getWindow(), &xscale, &yscale);
    contentScale = std::max(xscale, yscale);
#endif
    float layoutScale = std::max(renderer->getUIScale() * contentScale, 0.0001f);
    glm::mat4 pixelToNdc(1.0f);
    pixelToNdc[0][0] = 2.0f / std::max(swapExtentF.x, 0.0001f);
    pixelToNdc[1][1] = -2.0f / std::max(swapExtentF.y, 0.0001f);
    pixelToNdc[3][0] = -1.0f;
    pixelToNdc[3][1] = 1.0f;
    VkBuffer vb, ib;
    std::tie(vb, ib) = renderer->getUIBuffers();
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vb, offsets);
    vkCmdBindIndexBuffer(commandBuffer, ib, 0, VK_INDEX_TYPE_UINT16);

    auto drawUIObject = [&](UIObject* object, const LayoutRect& rect) -> void {
        if (!object->isEnabled()) return;
        if (object->isTextureDirty()) {
            object->loadTextureForFrame(frameIndex);
        }
        GraphicsShader* shader = renderer->getShaderManager()->getGraphicsShader("ui");
        
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shader->pipeline);
        const auto& descriptorSets = object->getDescriptorSets();
        if (!descriptorSets.empty()) {
            const uint32_t dsIndex = std::min<uint32_t>(frameIndex, static_cast<uint32_t>(descriptorSets.size() - 1));
            vkCmdBindDescriptorSets(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                shader->pipelineLayout,
                0,
                1,
                &descriptorSets[dsIndex],
                0,
                nullptr
            );
        }
        glm::vec2 center = rect.position + 0.5f * rect.size;
        glm::mat4 pixelModel(1.0f);
        pixelModel = glm::translate(pixelModel, glm::vec3(center, 0.0f));
        pixelModel = glm::scale(pixelModel, glm::vec3(rect.size, 1.0f));
        
        UIPC pushConstants{};
        pushConstants.tint = object->getTint();
        pushConstants.uvClip = object->getUVClip();
        pushConstants.model = pixelToNdc * pixelModel;
        vkCmdPushConstants(commandBuffer, shader->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(UIPC), &pushConstants);
        vkCmdDrawIndexed(commandBuffer, 6, 1, 0, 0, 0);
    };
    auto drawTextObject = [&](TextObject* object, const LayoutRect& rect, const LayoutRect& pixelRect, const LayoutRect& parentRect) -> void {
        if (!object->isEnabled() || object->getText().empty()) return;
        GraphicsShader* shader = renderer->getShaderManager()->getGraphicsShader("text");
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shader->pipeline);

        const auto& fontInfo = fonts[object->getFont()];
        float scaleX = object->getScale().x * layoutScale;
        float scaleY = object->getScale().y * layoutScale;
        const float textScaleFactor = 0.9f;
        if (object->getParent() != nullptr) {
            const float baseGlyphHeight = static_cast<float>(fontInfo.ascent - fontInfo.descent);
            const float targetHeight = parentRect.size.y * 0.6f;
            const float fitScale = targetHeight / std::max(baseGlyphHeight, 1.0f);
            scaleX *= fitScale * textScaleFactor;
            scaleY *= fitScale * textScaleFactor;
        }
        float penX = 0.0f;
        float minX = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest();
        for (const char& c : object->getText()) {
            const Character& ch = fontInfo.characters.at(c);
            float xpos = penX + ch.bearing.x * scaleX;
            float w = ch.size.x * scaleX;
            minX = std::min(minX, xpos);
            maxX = std::max(maxX, xpos + w);
            penX += ch.advance * scaleX;
        }
        float actualTextWidth = (minX <= maxX) ? (maxX - minX) : 0.0f;
        float actualTextHeight = (fontInfo.ascent - fontInfo.descent) * scaleY;
        float xOffset = (pixelRect.size.x - actualTextWidth) * 0.5f;
        float yOffset = (pixelRect.size.y - actualTextHeight) * 0.5f;
        float x = pixelRect.position.x + xOffset - (minX == std::numeric_limits<float>::max() ? 0.0f : minX);
        float y = pixelRect.position.y + yOffset + actualTextHeight - fontInfo.ascent * scaleY;

        const std::string& text = object->getText();
        for (const char& c : text) {
            const Character& ch = fontInfo.characters.at(c);
            float xpos = x + ch.bearing.x * scaleX;
            float ypos = y - (ch.size.y - ch.bearing.y) * scaleY;
            float w = ch.size.x * scaleX;
            float h = ch.size.y * scaleY;
            glm::mat4 pixelModel(1.0f);
            pixelModel = glm::translate(pixelModel, glm::vec3(xpos + w / 2.0f, ypos + h / 2.0f, 0.0f));
            pixelModel = glm::scale(pixelModel, glm::vec3(w, h, 1.0f));

            UIPC pushConstants = {
                .model = pixelToNdc * pixelModel,
                .tint = object->getTint(),
                .uvClip = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f)
            };

            if (!ch.descriptorSets.empty()) {
                const uint32_t dsIndex = std::min<uint32_t>(frameIndex, static_cast<uint32_t>(ch.descriptorSets.size() - 1));
                vkCmdBindDescriptorSets(
                    commandBuffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    shader->pipelineLayout,
                    0,
                    1,
                    &ch.descriptorSets[dsIndex],
                    0,
                    nullptr
                );
            }
            vkCmdPushConstants(commandBuffer, shader->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(UIPC), &pushConstants);
            vkCmdDrawIndexed(commandBuffer, 6, 1, 0, 0, 0);
            x += (ch.advance) * scaleX;
        };
    };

    LayoutRect rootAnchorRect = {
        .position = glm::vec2(0.0f, 0.0f),
        .size = swapExtentF / layoutScale
    };

    auto traverse = [&](auto& self, std::variant<UIObject*, TextObject*> node, const LayoutRect& parentRect, bool isRoot) -> void {
        const LayoutRect& anchorRect = isRoot ? rootAnchorRect : parentRect;
        
        if (std::holds_alternative<UIObject*>(node)) {
            UIObject* obj = std::get<UIObject*>(node);
            LayoutRect designRect = resolveDesignRect(obj, anchorRect);
            LayoutRect pixelRect = toPixelRect(designRect, glm::vec2(0.0f), layoutScale);
            drawUIObject(obj, pixelRect);
            std::vector<std::variant<UIObject*, TextObject*>>& children = obj->getChildren();
            std::sort(children.begin(), children.end(), [](const auto& a, const auto& b) {
                auto getZ = [](const std::variant<UIObject*, TextObject*>& v) -> float {
                    if (std::holds_alternative<UIObject*>(v))
                        return std::get<UIObject*>(v)->getTransform()[3][2];
                    return std::get<TextObject*>(v)->getTransform()[3][2];
                };
                return getZ(a) > getZ(b);
            });
            for (const auto& child : children) {
                self(self, child, designRect, false);
            }
        } else {
            TextObject* obj = std::get<TextObject*>(node);
            LayoutRect designRect = resolveDesignRect(obj, anchorRect);
            LayoutRect pixelRect = toPixelRect(designRect, glm::vec2(0.0f), layoutScale);
            drawTextObject(obj, designRect, pixelRect, anchorRect);
        }
    };

    static thread_local std::vector<std::variant<UIObject*, TextObject*>> sortedRoots;
    sortedRoots.clear();
    sortedRoots.reserve(rootObjects.size());
    for (auto& [name, obj] : rootObjects) {
        sortedRoots.push_back(obj);
    }
    std::sort(sortedRoots.begin(), sortedRoots.end(), [](const auto& a, const auto& b) {
        auto getZ = [](const std::variant<UIObject*, TextObject*>& v) -> float {
            if (std::holds_alternative<UIObject*>(v))
                return std::get<UIObject*>(v)->getTransform()[3][2];
            return std::get<TextObject*>(v)->getTransform()[3][2];
        };
        return getZ(a) > getZ(b);
    });
    for (auto& obj : sortedRoots) {
        traverse(traverse, obj, rootAnchorRect, true);
    }
}

engine::UIObject* engine::UIManager::processMouseMovement(GLFWwindow* window, double xpos, double ypos) {
    const glm::vec2 swapExtentF = glm::vec2(renderer->getSwapChainExtent().width, renderer->getSwapChainExtent().height);
    float xscale = 1.0f, yscale = 1.0f;
    glfwGetWindowContentScale(window, &xscale, &yscale);
    float contentScale = std::max(xscale, yscale);
    float layoutScale = std::max(renderer->getUIScale() * contentScale, 0.0001f);
    glm::vec2 mousePosF(static_cast<float>(xpos) * std::max(xscale, 1.0f), static_cast<float>(ypos) * std::max(yscale, 1.0f));
    mousePosF.y = swapExtentF.y - mousePosF.y;
    LayoutRect rootAnchorRect = {
        .position = glm::vec2(0.0f, 0.0f),
        .size = swapExtentF / layoutScale
    };

    bool foundHover = false;
    UIObject* hoveredObject = nullptr;
    UIObject* lastHovered = renderer->getHoveredObject();
    auto traverse = [&](auto& self, UIObject* node, const LayoutRect& parentRect, bool isRoot) -> void {
        if (!node || !node->isEnabled()) return;
        const LayoutRect& anchorRect = isRoot ? rootAnchorRect : parentRect;
        LayoutRect designRect = resolveDesignRect(node, anchorRect);
        const float invLayout = layoutScale > 0.0f ? 1.0f / layoutScale : 0.0f;
        glm::vec2 mouseDesign = mousePosF * invLayout;
        bool isOverNode = mouseDesign.x >= designRect.position.x
            && mouseDesign.x <= designRect.position.x + designRect.size.x
            && mouseDesign.y >= designRect.position.y
            && mouseDesign.y <= designRect.position.y + designRect.size.y;
        for (const auto& child : node->getChildren()) {
            if (std::holds_alternative<UIObject*>(child)) {
                self(self, std::get<UIObject*>(child), designRect, false);
                if (foundHover) return;
            }
        }
        if (isOverNode && !foundHover &&
            (node->getOnHover() 
            || node->getType() == engine::UIType::Button
            || node->getType() == engine::UIType::Checkbox
            || node->getType() == engine::UIType::Slider)
        ) {
            hoveredObject = node;
            foundHover = true;
        }
    };
    for (auto& [name, obj] : objects) {
        if (std::holds_alternative<UIObject*>(obj) && std::get<UIObject*>(obj)->getParent() == nullptr) {
            traverse(traverse, std::get<UIObject*>(obj), rootAnchorRect, true);
        }
    }
    if (hoveredObject && hoveredObject != lastHovered) {
        hoveredObject->getOnHover() ? (*hoveredObject->getOnHover())() : void();
    }
    if (hoveredObject != lastHovered) {
        lastHovered ? (lastHovered->getOnStopHover() ? (*lastHovered->getOnStopHover())() : void()) : void();
    }
    return hoveredObject;
}
