#include <engine/ModelManager.h>

engine::Model::Model(std::string name, Renderer* renderer) : name(name), renderer(renderer) {}

engine::Model::~Model() {
    if (vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(renderer->getDevice(), vertexBuffer, nullptr);
    }
    if (vertexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(renderer->getDevice(), vertexBufferMemory, nullptr);
    }
    if (indexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(renderer->getDevice(), indexBuffer, nullptr);
    }
    if (indexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(renderer->getDevice(), indexBufferMemory, nullptr);
    }
}

void engine::Model::loadFromFile(std::string filepath) {
    const std::filesystem::path pathObj(filepath);
    auto dataResult = fastgltf::GltfDataBuffer::FromPath(pathObj);
    if (!dataResult) {
        throw std::runtime_error("Failed to load model file: " + filepath + " Error: " + fastgltf::getErrorName(dataResult.error()).data());
    }
    fastgltf::GltfDataBuffer data = std::move(dataResult.get());
    fastgltf::Parser parser{};
    constexpr auto gltfOptions = fastgltf::Options::LoadGLBBuffers | fastgltf::Options::LoadExternalBuffers;
    auto load = parser.loadGltfBinary(data, pathObj.parent_path(), gltfOptions);
    if (!load) {
        throw std::runtime_error("Failed to parse model file: " + filepath + " Error: " + fastgltf::getErrorName(load.error()).data());
    }
    fastgltf::Asset gltf = std::move(load.get());
    if (gltf.meshes.empty()) {
        throw std::runtime_error("Model file contains no meshes: " + filepath);
    }
    const auto& mesh = gltf.meshes[0];
    if (mesh.primitives.empty()) {
        throw std::runtime_error("Mesh contains no primitives: " + filepath);
    }
    constexpr std::size_t floatsPerVertex = 11; // pos(3), normal(3), uv(2), tangent(4)
    std::vector<float> tempVertices;
    std::vector<uint32_t> tempIndices;
    for (const auto& primitive : mesh.primitives) {
        if (!primitive.indicesAccessor.has_value()) {
            std::cerr<<std::format("Warning: Primitive in model {} has no indices. Skipping.\n", filepath);
            continue;
        }
        const auto possitionAttr = primitive.findAttribute("POSITION");
        if (possitionAttr == primitive.attributes.end()) {
            std::cerr<<std::format("Warning: Primitive in model {} has no POSITION attribute. Skipping.\n", filepath);
            continue;
        }
        const fastgltf::Accessor& indexAccessor = gltf.accessors[primitive.indicesAccessor.value()];
        const fastgltf::Accessor& positionAccessor = gltf.accessors[possitionAttr->accessorIndex];
        const std::size_t vertexCount = positionAccessor.count;
        const std::size_t initialVertexCount = tempVertices.size() / floatsPerVertex;
        const std::size_t startFloat = tempVertices.size();
        tempVertices.resize(tempVertices.size() + vertexCount * floatsPerVertex, 0.0f);
        for (std::size_t i = 0; i < vertexCount; ++i) {
            const std::size_t base = startFloat + i * floatsPerVertex;
            tempVertices[base + 3] = 0.0f; // normal.x
            tempVertices[base + 4] = 0.0f; // normal.y
            tempVertices[base + 5] = 0.0f; // normal.z
            tempVertices[base + 6] = 0.0f; // uv.x
            tempVertices[base + 7] = 0.0f; // uv.y
            tempVertices[base + 8] = 0.0f; // tangent.x
            tempVertices[base + 9] = 0.0f; // tangent.y
            tempVertices[base + 10] = 0.0f; // tangent.z
        }
        tempIndices.reserve(tempIndices.size() + indexAccessor.count);
        fastgltf::iterateAccessor<std::uint32_t>(gltf, gltf.accessors[primitive.indicesAccessor.value()], 
            [&](std::uint32_t index) {
                tempIndices.push_back(static_cast<uint32_t>(initialVertexCount + index));
            });
        fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, positionAccessor,
            [&](glm::vec3 v, std::size_t index) {
                const std::size_t base = startFloat + index * floatsPerVertex;
                tempVertices[base + 0] = v.x;
                tempVertices[base + 1] = v.y;
                tempVertices[base + 2] = v.z;
                // Update AABB
                if (index == 0 && aabb.min == glm::vec3(FLT_MAX)) {
                    aabb.min = v;
                    aabb.max = v;
                } else {
                    aabb.min = glm::min(aabb.min, v);
                    aabb.max = glm::max(aabb.max, v);
                }
            });
        const auto normalAttr = primitive.findAttribute("NORMAL");
        if (normalAttr != primitive.attributes.end()) {
            const fastgltf::Accessor& normalAccessor = gltf.accessors[normalAttr->accessorIndex];
            fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, normalAccessor,
                [&](glm::vec3 n, std::size_t index) {
                    const std::size_t base = startFloat + index * floatsPerVertex;
                    tempVertices[base + 3] = n.x;
                    tempVertices[base + 4] = n.y;
                    tempVertices[base + 5] = n.z;
                });
        }
        const auto texcoordAttr = primitive.findAttribute("TEXCOORD_0");
        if (texcoordAttr != primitive.attributes.end()) {
            const fastgltf::Accessor& texcoordAccessor = gltf.accessors[texcoordAttr->accessorIndex];
            fastgltf::iterateAccessorWithIndex<glm::vec2>(gltf, texcoordAccessor,
                [&](glm::vec2 uv, std::size_t index) {
                    const std::size_t base = startFloat + index * floatsPerVertex;
                    tempVertices[base + 6] = uv.x;
                    tempVertices[base + 7] = uv.y;
                });
        }
        bool hasTangents = false;
        const auto tangentAttr = primitive.findAttribute("TANGENT");
        if (tangentAttr != primitive.attributes.end()) {
            const fastgltf::Accessor& tangentAccessor = gltf.accessors[tangentAttr->accessorIndex];
            fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, tangentAccessor,
                [&](glm::vec4 t, std::size_t index) {
                    const std::size_t base = startFloat + index * floatsPerVertex;
                    tempVertices[base + 8] = t.x;
                    tempVertices[base + 9] = t.y;
                    tempVertices[base + 10] = t.z;
                });
            hasTangents = true;
        }
        if (!hasTangents) {
            std::vector<glm::vec3> tangents(vertexCount, glm::vec3(0.0f));
            for (std::size_t i = initialVertexCount * 3; i < tempIndices.size(); i += 3) {
                const uint32_t i0 = tempIndices[i + 0] - static_cast<uint32_t>(initialVertexCount);
                const uint32_t i1 = tempIndices[i + 1] - static_cast<uint32_t>(initialVertexCount);
                const uint32_t i2 = tempIndices[i + 2] - static_cast<uint32_t>(initialVertexCount);
                if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount) {
                    continue;
                }
                const std::size_t b0 = startFloat + i0 * floatsPerVertex;
                const std::size_t b1 = startFloat + i1 * floatsPerVertex;
                const std::size_t b2 = startFloat + i2 * floatsPerVertex;
                const glm::vec3 v0(tempVertices[b0 + 0], tempVertices[b0 + 1], tempVertices[b0 + 2]);
                const glm::vec3 v1(tempVertices[b1 + 0], tempVertices[b1 + 1], tempVertices[b1 + 2]);
                const glm::vec3 v2(tempVertices[b2 + 0], tempVertices[b2 + 1], tempVertices[b2 + 2]);
                const glm::vec2 uv0(tempVertices[b0 + 6], tempVertices[b0 + 7]);
                const glm::vec2 uv1(tempVertices[b1 + 6], tempVertices[b1 + 7]);
                const glm::vec2 uv2(tempVertices[b2 + 6], tempVertices[b2 + 7]);
                const glm::vec3 edge1 = v1 - v0;
                const glm::vec3 edge2 = v2 - v0;
                const glm::vec2 deltaUV1 = uv1 - uv0;
                const glm::vec2 deltaUV2 = uv2 - uv0;
                const float det = deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y;
                glm::vec3 tangent(1.0f, 0.0f, 0.0f);
                if (std::abs(det) > 1e-6f) {
                    const float invDet = 1.0f / det;
                    tangent = invDet * (edge1 * deltaUV2.y - edge2 * deltaUV1.y);
                }
                tangents[i0] += tangent;
                tangents[i1] += tangent;
                tangents[i2] += tangent;
            }
            for (std::size_t i = 0; i < vertexCount; ++i) {
                const std::size_t base = startFloat + i * floatsPerVertex;
                glm::vec3 n(
                    tempVertices[base + 3],
                    tempVertices[base + 4],
                    tempVertices[base + 5]
                );
                glm::vec3 t = tangents[i];
                t = glm::normalize(t - n * glm::dot(n, t));
                if (glm::length(t) < 1e-6f) {
                    t = glm::vec3(1.0f, 0.0f, 0.0f);
                }
                tempVertices[base + 8] = t.x;
                tempVertices[base + 9] = t.y;
                tempVertices[base + 10] = t.z;
            }
        }
    }
    if (tempVertices.empty() || tempIndices.empty()) {
        throw std::runtime_error("No valid geometry found in model: " + filepath);
    }
    std::tie(vertexBuffer, vertexBufferMemory) = renderer->createBuffer(
        sizeof(float) * tempVertices.size(),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    std::tie(indexBuffer, indexBufferMemory) = renderer->createBuffer(
        sizeof(uint32_t) * tempIndices.size(),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    renderer->copyDataToBuffer(
        tempVertices.data(),
        sizeof(float) * tempVertices.size(),
        vertexBuffer,
        vertexBufferMemory
    );
    renderer->copyDataToBuffer(
        tempIndices.data(),
        sizeof(uint32_t) * tempIndices.size(),
        indexBuffer,
        indexBufferMemory
    );
    indexCount = static_cast<uint32_t>(tempIndices.size());
}

engine::ModelManager::ModelManager(Renderer* renderer, std::string modelDirectory) :  renderer(renderer), modelDirectory(modelDirectory) {
    renderer->registerModelManager(this);
}

engine::ModelManager::~ModelManager() {
    for (auto& [name, model] : models) {
        delete model;
    }
    models.clear();
}

void engine::ModelManager::init() {
    std::function<void(const std::string& directory, std::string parentPath)> scanAndLoadModels = [&](const std::string& directory, std::string parentPath) {
        std::vector<std::string> modelFiles = engine::scanDirectory(directory);
        for (const auto& filePath : modelFiles) {
            if (std::filesystem::is_directory(filePath)) {
                scanAndLoadModels(filePath, parentPath + std::filesystem::path(filePath).filename().string() + "_");
                continue;
            }
            if (!std::filesystem::is_regular_file(filePath)) {
                continue;
            }
            std::string fileName = std::filesystem::path(filePath).filename().string();
            std::string modelName = parentPath + fileName;
            if (models.find(modelName) != models.end()) {
                std::cout << std::format("Warning: Duplicate model name detected: {}. Skipping {}\n", modelName, filePath);
                continue;
            }
            Model* model = new Model(modelName, renderer);
            model->loadFromFile(filePath);
            models[modelName] = model;
        }
    };
    scanAndLoadModels(modelDirectory, "");
}