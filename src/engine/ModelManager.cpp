#include <engine/ModelManager.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <unordered_map>

engine::Model::Model(std::string name, std::string filepath, Renderer* renderer) : name(name), filepath(filepath), renderer(renderer) {}

engine::Model::~Model() {
    VkDevice device = renderer->getDevice();
    if (vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, vertexBuffer, nullptr);
        vertexBuffer = VK_NULL_HANDLE;
    }
    if (vertexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, vertexBufferMemory, nullptr);
        vertexBufferMemory = VK_NULL_HANDLE;
    }
    if (indexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, indexBuffer, nullptr);
        indexBuffer = VK_NULL_HANDLE;
    }
    if (indexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, indexBufferMemory, nullptr);
        indexBufferMemory = VK_NULL_HANDLE;
    }
    if (skinningBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, skinningBuffer, nullptr);
        skinningBuffer = VK_NULL_HANDLE;
    }
    if (skinningBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, skinningBufferMemory, nullptr);
        skinningBufferMemory = VK_NULL_HANDLE;
    }
}

void engine::Model::loadFromFile() {
    const std::filesystem::path pathObj(filepath);
    auto dataResult = fastgltf::GltfDataBuffer::FromPath(pathObj);
    if (!dataResult) {
        throw std::runtime_error("Failed to load model file: " + filepath + " Error: " + fastgltf::getErrorName(dataResult.error()).data());
    }
    fastgltf::GltfDataBuffer data = std::move(dataResult.get());
    fastgltf::Parser parser{};
    constexpr auto gltfOptions = fastgltf::Options::LoadExternalBuffers;
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
    std::unordered_map<size_t, int> nodeToJointIndex;
    if (!gltf.skins.empty()) {
        const auto& skin = gltf.skins[0];
        std::vector<glm::mat4> inverseBindMatrices;
        if (skin.inverseBindMatrices.has_value()) {
            const fastgltf::Accessor& ibmAccessor = gltf.accessors[skin.inverseBindMatrices.value()];
            inverseBindMatrices.reserve(ibmAccessor.count);
            fastgltf::iterateAccessor<glm::mat4>(gltf, ibmAccessor,
                [&](glm::mat4 mat) {
                    inverseBindMatrices.push_back(mat);
                });
        }
        for (size_t i = 0; i < skin.joints.size(); ++i) {
            nodeToJointIndex[skin.joints[i]] = static_cast<int>(i);
        }
        skeleton.resize(skin.joints.size());
        for (size_t i = 0; i < skin.joints.size(); ++i) {
            size_t nodeIndex = skin.joints[i];
            const fastgltf::Node& node = gltf.nodes[nodeIndex];
            Joint& joint = skeleton[i];
            joint.name = node.name;
            joint.inverseBindMatrix = (i < inverseBindMatrices.size()) ? inverseBindMatrices[i] : glm::mat4(1.0f);
            joint.localTransform = glm::mat4(1.0f);
            if (auto* trs = std::get_if<fastgltf::TRS>(&node.transform)) {
                glm::vec3 translation(trs->translation[0], trs->translation[1], trs->translation[2]);
                glm::quat rotation(trs->rotation[3], trs->rotation[0], trs->rotation[1], trs->rotation[2]);
                glm::vec3 scale(trs->scale[0], trs->scale[1], trs->scale[2]);
                glm::mat4 T = glm::translate(glm::mat4(1.0f), translation);
                glm::mat4 R = glm::mat4_cast(rotation);
                glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);
                joint.localTransform = T * R * S;
            } else if (auto* mat = std::get_if<fastgltf::math::fmat4x4>(&node.transform)) {
                std::memcpy(&joint.localTransform, mat, sizeof(glm::mat4));
            }
            joint.parentIndex = -1;
            for (size_t j = 0; j < gltf.nodes.size(); ++j) {
                const auto& potentialParent = gltf.nodes[j];
                for (const auto& childIdx : potentialParent.children) {
                    if (childIdx == nodeIndex) {
                        auto it = nodeToJointIndex.find(j);
                        if (it != nodeToJointIndex.end()) {
                            joint.parentIndex = it->second;
                        }
                        break;
                    }
                }
                if (joint.parentIndex != -1) {
                    break;
                }
            }
        }
    }
    const auto& animations = gltf.animations;
    for (const auto& anim : animations) {
        AnimationClip animationClip{};
        animationClip.name = anim.name;
        float animDuration = 0.0f;
        std::vector<AnimationChannel> channels;
        for (const auto& chan : anim.channels) {
            AnimationChannel channel{};
            channel.samplerIndex = chan.samplerIndex;
            size_t nodeIndex = chan.nodeIndex.has_value() ? chan.nodeIndex.value() : 0;
            auto it = nodeToJointIndex.find(nodeIndex);
            if (it == nodeToJointIndex.end()) {
                continue;
            }
            channel.targetNode = static_cast<size_t>(it->second);
            switch (chan.path) {
                case fastgltf::AnimationPath::Translation:
                    channel.path = AnimationChannel::Path::TRANSLATION;
                    break;
                case fastgltf::AnimationPath::Rotation:
                    channel.path = AnimationChannel::Path::ROTATION;
                    break;
                case fastgltf::AnimationPath::Scale:
                    channel.path = AnimationChannel::Path::SCALE;
                    break;
                default:
                    std::cerr << "Warning: Unsupported animation channel path in model " << filepath << "\n";
                    continue;
            };
            channels.push_back(channel);
        }
        animationClip.channels = std::move(channels);
        std::vector<AnimationSampler> samplers;
        for (const auto& sampler : anim.samplers) {
            AnimationSampler keyframes{};
            switch (sampler.interpolation) {
                case fastgltf::AnimationInterpolation::Linear:
                    keyframes.interpolation = AnimationSampler::Interpolation::LINEAR;
                    break;
                case fastgltf::AnimationInterpolation::Step:
                    keyframes.interpolation = AnimationSampler::Interpolation::STEP;
                    break;
                case fastgltf::AnimationInterpolation::CubicSpline:
                    keyframes.interpolation = AnimationSampler::Interpolation::CUBICSPLINE;
                    break;
                default:
                    std::cerr << "Warning: Unsupported animation sampler interpolation in model " << filepath << "\n";
                    continue;
            };
            const fastgltf::Accessor& inputAccessor = gltf.accessors[sampler.inputAccessor];
            float maxTime = 0.0f;
            fastgltf::iterateAccessor<float>(gltf, inputAccessor,
                [&](float time) {
                    keyframes.inputTimes.push_back(time);
                    if (time > maxTime) {
                        maxTime = time;
                    }
                });
            if (maxTime > animDuration) {
                animDuration = maxTime;
            }
            const fastgltf::Accessor& outputAccessor = gltf.accessors[sampler.outputAccessor];
            fastgltf::iterateAccessor<glm::vec4>(gltf, outputAccessor,
                [&](glm::vec4 value) {
                    keyframes.outputValues.push_back(value);
                });
            samplers.push_back(keyframes);
        }
        animationClip.samplers = std::move(samplers);
        animationClip.duration = animDuration;
        animationsMap[animationClip.name] = animationClip;
    }
    
    constexpr std::size_t floatsPerVertex = 12; // pos(3), normal(3), uv(2), tangent(4)
    std::vector<float> tempVertices;
    std::vector<uint32_t> tempIndices;
    std::vector<float> skinningData; // 4 joint indices + 4 weights per vertex
    bool hasSkinningData = false;
    for (const auto& primitive : mesh.primitives) {
        if (!primitive.indicesAccessor.has_value()) {
            std::cerr << "Warning: Primitive in model " << filepath << " has no indices. Skipping.\n";
            continue;
        }
        const auto possitionAttr = primitive.findAttribute("POSITION");
        if (possitionAttr == primitive.attributes.end()) {
            std::cerr << "Warning: Primitive in model " << filepath << " has no POSITION attribute. Skipping.\n";
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
            tempVertices[base + 11] = 1.0f; // tangent.w (handedness, default +1)
        }
        tempIndices.reserve(tempIndices.size() + indexAccessor.count);
        skinningData.resize(vertexCount * 8, 0.0f); // 4 joint indices + 4 weights per vertex
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
                    tempVertices[base + 11] = t.w; // handedness (+1 or -1)
                });
            hasTangents = true;
        }
        if (!hasTangents) {
            std::vector<glm::vec3> tangents(vertexCount, glm::vec3(0.0f));
            std::vector<glm::vec3> bitangents(vertexCount, glm::vec3(0.0f));
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
                glm::vec3 bitangent(0.0f, 1.0f, 0.0f);
                if (std::abs(det) > 1e-6f) {
                    const float invDet = 1.0f / det;
                    tangent = invDet * (edge1 * deltaUV2.y - edge2 * deltaUV1.y);
                    bitangent = invDet * (edge2 * deltaUV1.x - edge1 * deltaUV2.x);
                }
                tangents[i0] += tangent;
                tangents[i1] += tangent;
                tangents[i2] += tangent;
                bitangents[i0] += bitangent;
                bitangents[i1] += bitangent;
                bitangents[i2] += bitangent;
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
                glm::vec3 b = bitangents[i];
                float handedness = (glm::dot(glm::cross(n, t), b) < 0.0f) ? -1.0f : 1.0f;
                tempVertices[base + 8] = t.x;
                tempVertices[base + 9] = t.y;
                tempVertices[base + 10] = t.z;
                tempVertices[base + 11] = handedness;
            }
        }
        const auto jointsAttr = primitive.findAttribute("JOINTS_0");
        const auto weightsAttr = primitive.findAttribute("WEIGHTS_0");
        if (jointsAttr != primitive.attributes.end() && weightsAttr != primitive.attributes.end()) {
            hasSkinningData = true;
            const fastgltf::Accessor& jointsAccessor = gltf.accessors[jointsAttr->accessorIndex];
            const fastgltf::Accessor& weightsAccessor = gltf.accessors[weightsAttr->accessorIndex];
            fastgltf::iterateAccessorWithIndex<fastgltf::math::uvec4>(gltf, jointsAccessor,
                [&](fastgltf::math::uvec4 jointIndices, std::size_t index) {
                    const std::size_t base = index * 8;
                    skinningData[base + 0] = static_cast<float>(jointIndices[0]);
                    skinningData[base + 1] = static_cast<float>(jointIndices[1]);
                    skinningData[base + 2] = static_cast<float>(jointIndices[2]);
                    skinningData[base + 3] = static_cast<float>(jointIndices[3]);
                });
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(gltf, weightsAccessor,
                [&](fastgltf::math::fvec4 jointWeights, std::size_t index) {
                    const std::size_t base = index * 8;
                    skinningData[base + 4] = jointWeights[0];
                    skinningData[base + 5] = jointWeights[1];
                    skinningData[base + 6] = jointWeights[2];
                    skinningData[base + 7] = jointWeights[3];
                });
        }
    }
    if (tempVertices.empty() || tempIndices.empty()) {
        throw std::runtime_error("No valid geometry found in model: " + filepath);
    }
    if (hasSkinningData && !skinningData.empty()) {
        std::tie(skinningBuffer, skinningBufferMemory) = renderer->createBuffer(
            sizeof(float) * skinningData.size(),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );
        renderer->copyDataToBuffer(
            skinningData.data(),
            sizeof(float) * skinningData.size(),
            skinningBuffer,
            skinningBufferMemory
        );
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

std::pair<std::vector<glm::vec3>, std::vector<uint32_t>> engine::Model::loadVertsForModel() {
    const std::filesystem::path pathObj(filepath);
    auto dataResult = fastgltf::GltfDataBuffer::FromPath(pathObj);
    if (!dataResult) {
        throw std::runtime_error("Failed to load model file: " + filepath + " Error: " + fastgltf::getErrorName(dataResult.error()).data());
    }
    fastgltf::GltfDataBuffer data = std::move(dataResult.get());
    fastgltf::Parser parser{};
    constexpr auto gltfOptions = fastgltf::Options::LoadExternalBuffers;
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
    std::vector<glm::vec3> vertices;
    std::vector<uint32_t> indices;
    for (const auto& primitive : mesh.primitives) {
        const auto possitionAttr = primitive.findAttribute("POSITION");
        if (possitionAttr == primitive.attributes.end()) {
            continue;
        }
        const fastgltf::Accessor& positionAccessor = gltf.accessors[possitionAttr->accessorIndex];
        const std::size_t vertexOffset = vertices.size();
        fastgltf::iterateAccessor<glm::vec3>(gltf, positionAccessor,
            [&](glm::vec3 v) {
                vertices.push_back(v);
            });
        if (primitive.indicesAccessor.has_value()) {
            const fastgltf::Accessor& indexAccessor = gltf.accessors[primitive.indicesAccessor.value()];
            fastgltf::iterateAccessor<uint32_t>(gltf, indexAccessor,
                [&](uint32_t i) {
                    indices.push_back(static_cast<uint32_t>(vertexOffset + i));
                });
        }
    }
    return {vertices, indices};
}

engine::ModelManager::ModelManager(Renderer* renderer, std::string modelDirectory) : renderer(renderer), modelDirectory(modelDirectory) {
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
            std::string modelBaseName = std::filesystem::path(fileName).stem().string();
            std::string modelName = parentPath + modelBaseName;
            if (models.find(modelName) != models.end()) {
                std::cout << "Warning: Duplicate model name detected: " << modelName << ". Skipping " << filePath << "\n";
                continue;
            }
            Model* model = new Model(modelName, filePath, renderer);
            model->loadFromFile();
            models[modelName] = model;
        }
    };
    scanAndLoadModels(modelDirectory, "");
}
