#include <engine/UIManager.h>

engine::UIObject::~UIObject() {
    for (auto& child : children) {
        delete child;
    }
    children.clear();
}

void engine::UIObject::addChild(UIObject* child) {
    if (child->parent) {
        child->parent->removeChild(child);
    }
    children.push_back(child);
    child->parent = this;
}

void engine::UIObject::removeChild(UIObject* child) {
    children.erase(std::remove(children.begin(), children.end(), child), children.end());
    child->parent = nullptr;
}

engine::UIManager::UIManager(Renderer* renderer) : renderer(renderer) {
    renderer->registerUIManager(this);
}

engine::UIManager::~UIManager() {
    clear();
}

void engine::UIManager::addObject(UIObject* object) {
    if (objects.find(object->getName()) != objects.end()) {
        std::cout << std::format("Warning: Duplicate UIObject name detected: {}. Overwriting existing object.\n", object->getName());
        delete objects[object->getName()];
    }
    objects[object->getName()] = object;
}

void engine::UIManager::removeObject(const std::string& name) {
    auto it = objects.find(name);
    if (it != objects.end()) {
        delete it->second;
        objects.erase(it);
    }
}

engine::UIObject* engine::UIManager::getObject(const std::string& name) {
    auto it = objects.find(name);
    if (it != objects.end()) {
        return it->second;
    }
    return nullptr;
}

void engine::UIManager::clear() {
    for (auto& [name, object] : objects) {
        delete object;
    }
    objects.clear();
}

void engine::UIManager::loadTextures() {
    for (auto& [name, object] : objects) {
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