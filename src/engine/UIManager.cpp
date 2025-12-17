#include <engine/UIManager.h>
#include <utility>
#include <filesystem>
#include <limits>

engine::UIObject::UIObject(UIManager* uiManager, glm::mat4 transform, std::string name, glm::vec3 tint, std::string texture, Corner anchorCorner, std::function<void()>* onHover, std::function<void()>* onStopHover)
    : uiManager(uiManager), name(std::move(name)), tint(tint), transform(transform), anchorCorner(anchorCorner), texture(std::move(texture)), onHover(onHover), onStopHover(onStopHover) {
    uiManager->addObject(this);
}

engine::TextObject::TextObject(UIManager* uiManager, glm::mat4 transform, std::string name, glm::vec3 tint, std::string text, std::string font, Corner anchorCorner)
    : uiManager(uiManager), name(std::move(name)), tint(tint), text(std::move(text)), font(std::move(font)), transform(transform), anchorCorner(anchorCorner) {
    uiManager->addObject(this);
}

engine::UIObject::~UIObject() {
    for (auto& child : children) {
        if (std::holds_alternative<TextObject*>(child)) {
            delete std::get<TextObject*>(child);
        } else{
            delete std::get<UIObject*>(child);
        }
    }
    children.clear();
}

void engine::UIObject::addChild(UIObject* child) {
    if (child->getParent()) {
        child->getParent()->removeChild(child);
    }
    children.push_back(child);
    child->setParent(this);
}

void engine::UIObject::addChild(TextObject* child) {
    if (child->getParent()) {
        child->getParent()->removeChild(child);
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
}

void engine::UIObject::removeChild(TextObject* child) {
    auto it = std::remove_if(children.begin(), children.end(), [child](const auto& entry) {
        return std::holds_alternative<TextObject*>(entry) && std::get<TextObject*>(entry) == child;
    });
    children.erase(it, children.end());
    child->setParent(nullptr);
}

engine::UIManager::UIManager(Renderer* renderer, std::string& fontDirectory) : renderer(renderer), fontDirectory(std::move(fontDirectory)) {
    renderer->registerUIManager(this);
}

engine::UIManager::~UIManager() {
    clear();
    for (auto& [name, font] : fonts) {
        for (auto& [charKey, character] : font.characters) {
            if (character.texture) {
                if (character.texture->imageSampler != VK_NULL_HANDLE) {
                    vkDestroySampler(renderer->getDevice(), character.texture->imageSampler, nullptr);
                }
                if (character.texture->imageView != VK_NULL_HANDLE) {
                    vkDestroyImageView(renderer->getDevice(), character.texture->imageView, nullptr);
                }
                if (character.texture->image != VK_NULL_HANDLE) {
                    vkDestroyImage(renderer->getDevice(), character.texture->image, nullptr);
                }
                if (character.texture->imageMemory != VK_NULL_HANDLE) {
                    vkFreeMemory(renderer->getDevice(), character.texture->imageMemory, nullptr);
                }
                delete character.texture;
            }
        }
    }
}

void engine::UIManager::addObject(UIObject* object) {
    if (objects.find(object->getName()) != objects.end()) {
        std::cout << std::format("Warning: Duplicate UIObject name detected: {}. Overwriting existing object.\n", object->getName());
        if (std::holds_alternative<TextObject*>(objects[object->getName()])) {
            delete std::get<TextObject*>(objects[object->getName()]);
        } else{
            delete std::get<UIObject*>(objects[object->getName()]);
        }
    }
    objects[object->getName()] = object;
}

void engine::UIManager::addObject(TextObject* object) {
    const std::string& key = object->getName();
    if (objects.find(key) != objects.end()) {
        std::cout << std::format("Warning: Duplicate TextObject name detected: {}. Overwriting existing object.\n", key);
        if (std::holds_alternative<TextObject*>(objects[key])) {
            delete std::get<TextObject*>(objects[key]);
        } else{
            delete std::get<UIObject*>(objects[key]);
        }
    }
    objects[key] = object;
}

glm::vec2 engine::TextObject::getScale() const {
    float scaleX = transform[0][0];
    float scaleY = transform[1][1];
    return glm::vec2(scaleX, scaleY);
}

void engine::UIManager::removeObject(const std::string& name) {
    auto it = objects.find(name);
    if (it != objects.end()) {
        if (std::holds_alternative<TextObject*>(it->second)) {
            delete std::get<TextObject*>(it->second);
        } else{
            delete std::get<UIObject*>(it->second);
        }
        objects.erase(it);
    }
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
    }
    std::vector<std::string> rootKeys;
    rootKeys.reserve(objects.size());
    for (auto& [name, object] : objects) {
        bool isRoot = false;
        if (std::holds_alternative<TextObject*>(object)) {
            isRoot = (std::get<TextObject*>(object)->getParent() == nullptr);
        } else {
            isRoot = (std::get<UIObject*>(object)->getParent() == nullptr);
        }
        if (isRoot) {
            rootKeys.push_back(name);
        }
    }
    for (const auto& key : rootKeys) {
        auto it = objects.find(key);
        if (it == objects.end()) continue;
        if (std::holds_alternative<TextObject*>(it->second)) {
            delete std::get<TextObject*>(it->second);
        } else {
            delete std::get<UIObject*>(it->second);
        }
        objects.erase(it);
    }
    objects.clear();
}

void engine::UIManager::loadTextures() {
    for (auto& [name, found] : objects) {
        if (!std::holds_alternative<UIObject*>(found)) continue;
        UIObject* object = std::get<UIObject*>(found);
        if (!object->getDescriptorSets().empty() || object->getTexture().empty()) continue;
        engine::Texture* texture = renderer->getTextureManager()->getTexture(object->getTexture());
        if (!texture) {
            std::cout << std::format("Warning: Texture {} for UIObject {} not found.\n", object->getTexture(), name);
            continue;
        }
        GraphicsShader* shader = renderer->getShaderManager()->getGraphicsShader("ui");
        std::vector<Texture*> textures = { texture };
        std::vector<VkBuffer> buffers;
        object->setDescriptorSets(renderer->createDescriptorSets(
            shader,
            textures,
            buffers
        ));
    }
}

void engine::UIManager::loadFonts() {
    std::function<void(const std::string& directory, std::string parentPath)> scanAndLoadFonts = [&](const std::string& directory, std::string parentPath) {
        std::vector<std::string> fontFiles = engine::scanDirectory(directory);
        for (const auto& filePath : fontFiles) {
            if (std::filesystem::is_directory(filePath)) {
                scanAndLoadFonts(filePath, parentPath + std::filesystem::path(filePath).filename().string() + "_");
                continue;
            }
            if (!std::filesystem::is_regular_file(filePath)) {
                continue;
            }
            std::string fileName = std::filesystem::path(filePath).filename().string();
            std::string fontName = parentPath + std::filesystem::path(fileName).stem().string();
            if (fonts.find(fontName) != fonts.end()) {
                std::cout << std::format("Warning: Duplicate font name detected: {}. Skipping {}\n", fontName, filePath);
                continue;
            }
            FT_Library ft;
            if (FT_Init_FreeType(&ft)) {
                std::cout << "Error: Could not init FreeType Library\n";
                continue;
            }
            FT_Face face;
            if (FT_New_Face(ft, filePath.c_str(), 0, &face)) {
                std::cout << std::format("Error: Failed to load font {}\n", filePath);
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
                    std::cout << std::format("Warning: Failed to load Glyph {} from font {}\n", c, fontName);
                    continue;
                }
                Character character;
                character.texture = new Texture();
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
                character.descriptorSets = renderer->createDescriptorSets(
                    shader,
                    textures,
                    buffers
                );
                const char glyph = static_cast<char>(c);
                const int glyphHeight = character.size.y;
                font.characters.insert_or_assign(glyph, std::move(character));
                if (glyphHeight > font.maxGlyphHeight) {
                    font.maxGlyphHeight = glyphHeight;
                }
            }
            fonts.insert_or_assign(fontName, std::move(font));
            FT_Done_Face(face);
            FT_Done_FreeType(ft);
        }
    };
    scanAndLoadFonts(fontDirectory, "");
}

engine::LayoutRect engine::UIManager::resolveDesignRect(std::variant<UIObject*, TextObject*> node, const LayoutRect& parentRect) {
    glm::vec2 size, position;
    Corner anchor;
    if (std::holds_alternative<TextObject*>(node)) {
        TextObject* textObj = std::get<TextObject*>(node);
        const auto& fontInfo = fonts[textObj->getFont()];
        const float scaleX = textObj->getScale().x;
        const float scaleY = textObj->getScale().y;
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
            position += glm::vec2(0.0f, 0.0f);
            break;
        case Corner::TopRight:
            position += glm::vec2(parentRect.size.x - size.x, 0.0f);
            break;
        case Corner::BottomLeft:
            position += glm::vec2(0.0f, parentRect.size.y - size.y);
            break;
        case Corner::BottomRight:
            position += glm::vec2(parentRect.size.x - size.x, parentRect.size.y - size.y);
            break;
        case Corner::Center:
            position += glm::vec2((parentRect.size.x - size.x) / 2.0f, (parentRect.size.y - size.y) / 2.0f);
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

void engine::UIManager::renderUI(VkCommandBuffer commandBuffer, RenderNode& node, uint32_t frameIndex) {
    std::set<GraphicsShader*>& shaders = node.shaders;
    const glm::vec2 swapExtentF = glm::vec2(renderer->getSwapChainExtent().width, renderer->getSwapChainExtent().height);
    constexpr glm::vec2 designResolution(800.0f, 600.0f);
    float layoutScale = std::max(renderer->getUIScale(), 0.0001f);
    glm::vec2 canvasSize = designResolution * layoutScale;
    glm::vec2 canvasOrigin = 0.5f * (swapExtentF - canvasSize);
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

    std::function<void(UIObject*, const LayoutRect&)> drawUIObject = [&](UIObject* object, const LayoutRect& rect) {
        if (!object->isEnabled()) return;
        GraphicsShader* shader = renderer->getShaderManager()->getGraphicsShader("ui");
        if (shaders.find(shader) == shaders.end()) {
            return;
        }
        
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
        pushConstants.model = pixelToNdc * pixelModel;
        vkCmdPushConstants(commandBuffer, shader->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(UIPC), &pushConstants);
        vkCmdDrawIndexed(commandBuffer, 6, 1, 0, 0, 0);
    };
    std::function<void(TextObject*, const LayoutRect&, const LayoutRect&)> drawTextObject = [&](TextObject* object, const LayoutRect& rect, const LayoutRect& pixelRect) {
        if (!object->isEnabled() || object->getText().empty()) return;
        GraphicsShader* shader = renderer->getShaderManager()->getGraphicsShader("text");
        if (shaders.find(shader) == shaders.end()) {
            return;
        }
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shader->pipeline);

        const float scaleX = object->getScale().x * layoutScale;
        const float scaleY = object->getScale().y * layoutScale;

        const auto& fontInfo = fonts[object->getFont()];
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
        float x = pixelRect.position.x - (minX == std::numeric_limits<float>::max() ? 0.0f : minX);

        const float layoutHeightPx = pixelRect.size.y;
        const float glyphHeightPx = (fontInfo.ascent - fontInfo.descent) * scaleY;
        const float topMarginPx = 0.5f * std::max(layoutHeightPx - glyphHeightPx, 0.0f);
        float y = pixelRect.position.y + topMarginPx + fontInfo.ascent * scaleY;

        const std::string& text = object->getText();
        for (const char& c : text) {
            const Character& ch = fonts[object->getFont()].characters[c];
            float xpos = x + ch.bearing.x * scaleX;
            float ypos = y - (ch.size.y - ch.bearing.y) * scaleY;
            float w = ch.size.x * scaleX;
            float h = ch.size.y * scaleY;
            glm::mat4 pixelModel(1.0f);
            pixelModel = glm::translate(pixelModel, glm::vec3(xpos + w / 2.0f, ypos + h / 2.0f, 0.0f));
            pixelModel = glm::scale(pixelModel, glm::vec3(w, h, 1.0f));
            
            UIPC pushConstants{};
            pushConstants.tint = object->getTint();
            pushConstants.model = pixelToNdc * pixelModel;
            
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

    std::function<void(std::variant<UIObject*, TextObject*>, const LayoutRect&)> traverse = [&](std::variant<UIObject*, TextObject*> node, const LayoutRect& parentRect) {
        if (std::holds_alternative<UIObject*>(node)) {
            UIObject* obj = std::get<UIObject*>(node);
            LayoutRect designRect = resolveDesignRect(obj, parentRect);
            LayoutRect pixelRect = toPixelRect(designRect, canvasOrigin, layoutScale);
            drawUIObject(obj, pixelRect);
            for (const auto& child : obj->getChildren()) {
                traverse(child, designRect);
            }
        } else {
            TextObject* obj = std::get<TextObject*>(node);
            LayoutRect designRect = resolveDesignRect(obj, parentRect);
            LayoutRect pixelRect = toPixelRect(designRect, canvasOrigin, layoutScale);
            drawTextObject(obj, designRect, pixelRect);
        }
    };

    LayoutRect rootRect = {
        .position = glm::vec2(0.0f, 0.0f),
        .size = designResolution
    };
    for (auto& [name, obj] : objects) {
        bool traversing = std::holds_alternative<TextObject*>(obj) ? (std::get<TextObject*>(obj)->getParent() == nullptr) : (std::get<UIObject*>(obj)->getParent() == nullptr);
        if (traversing) {
            traverse(obj, rootRect);
        }
    }
}

engine::UIObject* engine::UIManager::processMouseMovement(GLFWwindow* window, double xpos, double ypos) {
    const glm::vec2 swapExtentF = glm::vec2(renderer->getSwapChainExtent().width, renderer->getSwapChainExtent().height);
    constexpr glm::vec2 designResolution(800.0f, 600.0f);
    float layoutScale = std::max(renderer->getUIScale(), 0.0001f);
    glm::vec2 canvasSize = designResolution * layoutScale;
    glm::vec2 canvasOrigin = 0.5f * (swapExtentF - canvasSize);
    float xscale = 1.0f, yscale = 1.0f;
    glfwGetWindowContentScale(window, &xscale, &yscale);
    glm::vec2 mousePosF(static_cast<float>(xpos) * std::max(xscale, 1.0f), static_cast<float>(ypos) * std::max(yscale, 1.0f));
    mousePosF.y = swapExtentF.y - mousePosF.y;

    bool foundHover = false;
    UIObject* hoveredObject = nullptr;
    UIObject* lastHovered = renderer->getHoveredObject();
    std::function<void(UIObject*, const LayoutRect&)> traverse = [&](UIObject* node, const LayoutRect& parentRect) {
        if(!node || !node->isEnabled()) return;
        LayoutRect designRect = resolveDesignRect(node, parentRect);
        const float invLayout = layoutScale > 0.0f ? 1.0f / layoutScale : 0.0f;
        glm::vec2 mouseDesign = (mousePosF - canvasOrigin) * invLayout;
        if (mouseDesign.x >= designRect.position.x
        && mouseDesign.x <= designRect.position.x + designRect.size.x
        && mouseDesign.y >= designRect.position.y
        && mouseDesign.y <= designRect.position.y + designRect.size.y) {
            hoveredObject = node;
            foundHover = true;
            return;
        }
        for (const auto& child : node->getChildren()) {
            if (std::holds_alternative<UIObject*>(child)) {
                traverse(std::get<UIObject*>(child), designRect);
            }
        }
    };
    LayoutRect rootRect = {
        .position = glm::vec2(0.0f, 0.0f),
        .size = designResolution
    };
    for (auto& [name, obj] : objects) {
        if (std::holds_alternative<UIObject*>(obj) && std::get<UIObject*>(obj)->getParent() == nullptr) {
            traverse(std::get<UIObject*>(obj), rootRect);
        }
    }
    if (hoveredObject && hoveredObject != lastHovered) {
        hoveredObject->getOnHover() ? (*hoveredObject->getOnHover())() : void();
    } else if (!hoveredObject && lastHovered) {
        lastHovered->getOnStopHover() ? (*lastHovered->getOnStopHover())() : void();
    }
    return hoveredObject;
}