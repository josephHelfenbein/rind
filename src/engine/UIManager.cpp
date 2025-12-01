#include <engine/UIManager.h>

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
    children.erase(std::remove(children.begin(), children.end(), child), children.end());
    child->setParent(nullptr);
}

void engine::UIObject::removeChild(TextObject* child) {
    children.erase(std::remove(children.begin(), children.end(), child), children.end());
    child->setParent(nullptr);
}

engine::UIManager::UIManager(Renderer* renderer, std::string& fontDirectory) : renderer(renderer), fontDirectory(std::move(fontDirectory)) {
    renderer->registerUIManager(this);
}

engine::UIManager::~UIManager() {
    clear();
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
    if (objects.find(object->getText()) != objects.end()) {
        std::cout << std::format("Warning: Duplicate TextObject name detected: {}. Overwriting existing object.\n", object->getText());
        if (std::holds_alternative<TextObject*>(objects[object->getText()])) {
            delete std::get<TextObject*>(objects[object->getText()]);
        } else{
            delete std::get<UIObject*>(objects[object->getText()]);
        }
    }
    objects[object->getText()] = object;
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
    for (auto& [name, object] : objects) {
        if (std::holds_alternative<TextObject*>(object)) {
            delete std::get<TextObject*>(object);
        } else{
            delete std::get<UIObject*>(object);
        }
    }
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
            std::string fontName = parentPath + fileName;
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
                .ascent = face->size->metrics.ascender >> 6,
                .descent = face->size->metrics.descender >> 6,
                .lineHeight = face->size->metrics.height >> 6,
                .maxGlyphHeight = 0
            };
            font.characters.clear();
            for (unsigned char c = 0; c < 128; c++) {
                if (FT_Load_Char(face, c, FT_LOAD_RENDER)) {
                    std::cout << std::format("Warning: Failed to load Glyph {} from font {}\n", c, fontName);
                    continue;
                }
                Character character;
                character.size = glm::ivec2(face->glyph->bitmap.width, face->glyph->bitmap.rows);
                character.bearing = glm::ivec2(face->glyph->bitmap_left, face->glyph->bitmap_top);
                const FT_Pos rawAdvance = std::max<FT_Pos>(face->glyph->advance.x, static_cast<FT_Pos>(0));
                character.advance = static_cast<unsigned int>(rawAdvance >> 6);
                if (face->glyph->bitmap.width == 0 || face->glyph->bitmap.rows == 0) {
                    unsigned char emptyPixel = 0;
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
                character.texture->imageView = renderer->createImageView(
                    character.texture->image,
                    VK_FORMAT_R8_UNORM,
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