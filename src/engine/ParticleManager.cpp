#include <engine/ParticleManager.h>
#include <glm/gtc/matrix_transform.hpp>
#include <engine/PushConstants.h>
#include <engine/Camera.h>
#include <engine/SpatialGrid.h>

engine::Particle::Particle(
    EntityManager* entityManager,
    const glm::vec3& position,
    const glm::vec3& color,
    const glm::vec3& velocity,
    float lifetime,
    float type,
    float size
) : entityManager(entityManager), position(position), color(color), velocity(velocity), lifetime(lifetime), type(type), size(size) {
        prevPosition = position;
        prevPrevPosition = prevPosition;
    }

void engine::Particle::update(float deltaTime) {
    age += deltaTime;
    if (age >= lifetime) {
        markForDeletion();
        return;
    }
    if (type == 1.0f) {
        return;
    }
    velocity.y -= gravity * deltaTime; // gravity
    glm::vec3 currentPos = position;
    prevPrevPosition = prevPosition;
    prevPosition = currentPos;
    glm::vec3 newPos = currentPos + velocity * deltaTime;
    float speedSq = glm::dot(velocity, velocity);
    if (speedSq < 0.01f) {
        markForDeletion();
        return;
    }
    if (age > 0.15f && speedSq > 1.0f) {
        Collider::Collision collision = checkCollision(newPos);
        if (collision.other) {
            glm::vec3 normal = glm::normalize(collision.mtv.normal);
            velocity = velocity - 2.0f * glm::dot(velocity, normal) * normal;
            velocity *= 0.5f;
            newPos = currentPos + velocity * deltaTime;
            if (checkCollision(newPos).other) {
                markForDeletion();
                return;
            }
        }
    }
    position = newPos;
}

engine::Collider::Collision engine::Particle::checkCollision(const glm::vec3& position) {
    const float particleRadius = 0.05f;
    engine::AABB particleAABB = {
        .min = position - glm::vec3(particleRadius),
        .max = position + glm::vec3(particleRadius)
    };
    static thread_local std::vector<engine::Collider*> candidates;
    entityManager->getSpatialGrid().query(particleAABB, candidates);
    for (const auto& collider : candidates) {
        engine::AABB otherAABB = collider->getWorldAABB();
        if (!engine::Collider::aabbIntersects(particleAABB, otherAABB, 0.0f)) {
            continue;
        }
        Collider::ColliderType type = collider->getColliderType();
        bool collides = false;
        glm::vec3 normal(0.0f);
        switch (type) {
            case Collider::ColliderType::AABB: {
                collides = true;
                glm::vec3 center = (otherAABB.min + otherAABB.max) * 0.5f;
                glm::vec3 toPoint = position - center;
                glm::vec3 halfSize = (otherAABB.max - otherAABB.min) * 0.5f;
                glm::vec3 normalized = toPoint / halfSize;
                float absX = std::abs(normalized.x);
                float absY = std::abs(normalized.y);
                float absZ = std::abs(normalized.z);
                if (absX >= absY && absX >= absZ) {
                    normal = glm::vec3(normalized.x > 0 ? 1.0f : -1.0f, 0.0f, 0.0f);
                } else if (absY >= absX && absY >= absZ) {
                    normal = glm::vec3(0.0f, normalized.y > 0 ? 1.0f : -1.0f, 0.0f);
                } else {
                    normal = glm::vec3(0.0f, 0.0f, normalized.z > 0 ? 1.0f : -1.0f);
                }
                break;
            }
            case Collider::ColliderType::OBB: {
                OBBCollider* obb = static_cast<OBBCollider*>(collider);
                glm::mat4 worldTransform = obb->getWorldTransform();
                glm::vec3 obbCenter = glm::vec3(worldTransform[3]);
                glm::vec3 halfSize = obb->getHalfSize();
                glm::vec3 axisX = glm::normalize(glm::vec3(worldTransform[0]));
                glm::vec3 axisY = glm::normalize(glm::vec3(worldTransform[1]));
                glm::vec3 axisZ = glm::normalize(glm::vec3(worldTransform[2]));
                glm::vec3 delta = position - obbCenter;
                float projX = glm::dot(delta, axisX);
                float projY = glm::dot(delta, axisY);
                float projZ = glm::dot(delta, axisZ);
                if (std::abs(projX) <= halfSize.x && std::abs(projY) <= halfSize.y && std::abs(projZ) <= halfSize.z) {
                    collides = true;
                    float distX = halfSize.x - std::abs(projX);
                    float distY = halfSize.y - std::abs(projY);
                    float distZ = halfSize.z - std::abs(projZ);
                    if (distX <= distY && distX <= distZ) {
                        normal = axisX * (projX > 0 ? 1.0f : -1.0f);
                    } else if (distY <= distX && distY <= distZ) {
                        normal = axisY * (projY > 0 ? 1.0f : -1.0f);
                    } else {
                        normal = axisZ * (projZ > 0 ? 1.0f : -1.0f);
                    }
                }
                break;
            }
            case Collider::ColliderType::ConvexHull: {
                ConvexHullCollider* hull = static_cast<ConvexHullCollider*>(collider);
                const std::vector<glm::vec3>& faceAxes = hull->getFaceAxesCached();
                const std::vector<glm::vec3>& worldVerts = hull->getWorldVerts();
                bool inside = true;
                float minDist = std::numeric_limits<float>::max();
                glm::vec3 closestNormal(0.0f);
                for (const glm::vec3& faceNormal : faceAxes) {
                    float hullMax = std::numeric_limits<float>::lowest();
                    for (const glm::vec3& v : worldVerts) {
                        hullMax = glm::max(hullMax, glm::dot(v, faceNormal));
                    }
                    float pointProj = glm::dot(position, faceNormal);
                    float dist = hullMax - pointProj;
                    if (dist < 0) {
                        inside = false;
                        break;
                    }
                    if (dist < minDist) {
                        minDist = dist;
                        closestNormal = faceNormal;
                    }
                }
                if (inside) {
                    collides = true;
                    normal = closestNormal;
                }
                break;
            }
        }
        if (collides) {
            Collider::Collision collision = {
                .other = collider,
                .mtv = {.normal = normal, .penetrationDepth = 0.0f},
                .worldHitPoint = position
            };
            return collision;
        }
    }
    return Collider::Collision{};
}

engine::ParticleManager::ParticleManager(Renderer* renderer)
    : renderer(renderer) {
        renderer->registerParticleManager(this);
    }

void engine::ParticleManager::init() {
    VkDeviceSize bufferSize = maxParticles * sizeof(ParticleGPU);
    size_t frames = static_cast<size_t>(renderer->getMaxFramesInFlight());
    particleBuffers.resize(frames);
    particleBufferMemory.resize(frames);
    particleBuffersMapped.resize(frames);
    for (size_t i = 0; i < frames; ++i) {
        std::tie(particleBuffers[i], particleBufferMemory[i]) = renderer->createBuffer(
            bufferSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        vkMapMemory(renderer->getDevice(), particleBufferMemory[i], 0, bufferSize, 0, &particleBuffersMapped[i]);
    }
    createParticleDescriptorSets();
}

engine::ParticleManager::~ParticleManager() {
    for (size_t i = 0; i < particleBuffersMapped.size(); ++i) {
        if (particleBuffersMapped[i] != nullptr && i < particleBufferMemory.size() && particleBufferMemory[i] != VK_NULL_HANDLE) {
            vkUnmapMemory(renderer->getDevice(), particleBufferMemory[i]);
            particleBuffersMapped[i] = nullptr;
        }
    }
    for (size_t i = 0; i < particleBuffers.size(); ++i) {
        vkDestroyBuffer(renderer->getDevice(), particleBuffers[i], nullptr);
        vkFreeMemory(renderer->getDevice(), particleBufferMemory[i], nullptr);
    }
    particleBuffers.clear();
    particleBufferMemory.clear();
    particleBuffersMapped.clear();
    particles.clear();
}

void engine::ParticleManager::clear() {
    for (auto& particle : particles) {
        particle.markForDeletion();
    }
}

void engine::ParticleManager::createParticleDescriptorSets() {
    GraphicsShader* shader = renderer->getShaderManager()->getGraphicsShader("particle");
    VkDevice device = renderer->getDevice();
    size_t frames = static_cast<size_t>(renderer->getMaxFramesInFlight());
    
    VkImageView depthImageView = renderer->getPassImageView("gbuffer", "Depth");
    if (depthImageView == VK_NULL_HANDLE) {
        throw std::runtime_error("Failed to get gbuffer depth image view for particles!");
    }
    
    std::vector<VkDescriptorSetLayout> layouts(frames, shader->descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = shader->descriptorPool,
        .descriptorSetCount = static_cast<uint32_t>(frames),
        .pSetLayouts = layouts.data()
    };
    descriptorSets.resize(frames);
    if (vkAllocateDescriptorSets(device, &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate particle descriptor sets!");
    }
    
    for (size_t i = 0; i < frames; ++i) {
        VkDescriptorBufferInfo bufferInfo = {
            .buffer = particleBuffers[i],
            .offset = 0,
            .range = VK_WHOLE_SIZE
        };
        VkDescriptorImageInfo depthImageInfo = {
            .sampler = VK_NULL_HANDLE,
            .imageView = depthImageView,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        };
        VkDescriptorImageInfo samplerInfo = {
            .sampler = renderer->getMainTextureSampler(),
            .imageView = VK_NULL_HANDLE,
            .imageLayout = VK_IMAGE_LAYOUT_UNDEFINED
        };
        std::array<VkWriteDescriptorSet, 3> descriptorWrites = {{
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = descriptorSets[i],
                .dstBinding = 0,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pBufferInfo = &bufferInfo
            },
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = descriptorSets[i],
                .dstBinding = 1,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                .pImageInfo = &depthImageInfo
            },
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = descriptorSets[i],
                .dstBinding = 2,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                .pImageInfo = &samplerInfo
            }
        }};
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
}

void engine::ParticleManager::burstParticles(const glm::vec3& position, const glm::vec3& color, const glm::vec3& velocity, int count, float lifetime, float spread, float size) {
    if (particles.size() >= hardCap) return;
    float velLength = glm::length(velocity) + dist(rng) * 0.1f * glm::length(velocity);
    size_t remaining = hardCap - particles.size();
    size_t spawnCount = std::min(static_cast<size_t>(count), remaining);
    for (size_t i = 0; i < spawnCount; ++i) {
        float offsetX = dist(rng) * spread * velLength;
        float offsetY = dist(rng) * spread * velLength;
        float offsetZ = dist(rng) * spread * velLength;
        glm::vec3 velocityOffset = glm::vec3(offsetX, offsetY, offsetZ);
        float particleLifetime = lifetime + dist(rng) * 0.2f * lifetime;
        glm::vec3 colorOffset = glm::vec3(dist(rng), dist(rng), dist(rng)) * 0.1f;
        glm::vec3 particleColor = color + colorOffset;
        particleColor = glm::clamp(particleColor, glm::vec3(0.0f), glm::vec3(1.0f));
        particles.emplace_back(renderer->getEntityManager(), position, particleColor, velocity + velocityOffset, particleLifetime, 0.0f, size);
    }
}

void engine::ParticleManager::spawnTrail(const glm::vec3& start, const glm::vec3& dir, const glm::vec3& color, float lifetime, float fakeAge) {
    if (particles.size() >= hardCap) return;
    particles.emplace_back(renderer->getEntityManager(), start, color, glm::vec3(0.0f), lifetime, 1.0f);
    Particle& p = particles.back();
    p.setPrevPosition(dir);
    p.setPrevPrevPosition(start);
    p.setAge(fakeAge);
}

void engine::ParticleManager::updateParticleBuffer(uint32_t currentFrame) {
    VkDevice device = renderer->getDevice();
    if (particles.size() > maxParticles) {
        vkDeviceWaitIdle(device);
        if (particles.size() > hardCap) {
            size_t toRemove = particles.size() - hardCap;
            particles.erase(particles.begin(), particles.begin() + toRemove);
        }
        maxParticles = std::min(std::max(maxParticles * 2, static_cast<uint32_t>(particles.size())), hardCap);
        for (size_t i = 0; i < particleBuffersMapped.size(); ++i) {
            if (particleBuffersMapped[i] != nullptr && i < particleBufferMemory.size() && particleBufferMemory[i] != VK_NULL_HANDLE) {
                vkUnmapMemory(device, particleBufferMemory[i]);
                particleBuffersMapped[i] = nullptr;
            }
        }
        for (size_t i = 0; i < particleBuffers.size(); ++i) {
            vkDestroyBuffer(device, particleBuffers[i], nullptr);
            vkFreeMemory(device, particleBufferMemory[i], nullptr);
        }
        particleBuffers.clear();
        particleBufferMemory.clear();
        particleBuffersMapped.clear();
        particleBuffers.resize(renderer->getMaxFramesInFlight());
        particleBufferMemory.resize(renderer->getMaxFramesInFlight());
        particleBuffersMapped.resize(renderer->getMaxFramesInFlight(), nullptr);
        for (size_t i = 0; i < particleBuffers.size(); ++i) {
            VkDeviceSize bufferSize = maxParticles * sizeof(ParticleGPU);
            std::tie(particleBuffers[i], particleBufferMemory[i]) = renderer->createBuffer(
                bufferSize,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );
            vkMapMemory(device, particleBufferMemory[i], 0, bufferSize, 0, &particleBuffersMapped[i]);
        }
        GraphicsShader* shader = renderer->getShaderManager()->getGraphicsShader("particle");
        vkResetDescriptorPool(renderer->getDevice(), shader->descriptorPool, 0);
        createParticleDescriptorSets();
    }
    ParticleGPU* gpuData = static_cast<ParticleGPU*>(particleBuffersMapped[currentFrame]);
    visibleCount = 0;
    uint32_t backIdx = static_cast<uint32_t>(particles.size());
    Camera* camera = renderer->getEntityManager()->getCamera();
    if (!camera) return;
    for (size_t i = 0; i < particles.size(); ++i) {
        if (camera->isSphereInFrustum(particles[i].getPosition(), 0.1f)) {
            gpuData[visibleCount++] = particles[i].getGPUData();
        } else {
            gpuData[--backIdx] = particles[i].getGPUData();
        }
    }
    for (size_t i = visibleCount; i < particles.size(); ++i) {
        gpuData[i] = gpuData[backIdx++];
    }
}

void engine::ParticleManager::renderParticles(VkCommandBuffer commandBuffer, uint32_t currentFrame) {
    if (particles.empty()) return;
    GraphicsShader* shader = renderer->getShaderManager()->getGraphicsShader("particle");
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shader->pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shader->pipelineLayout, 0, 1, &descriptorSets[currentFrame], 0, nullptr);
    Camera* camera = renderer->getEntityManager()->getCamera();
    if (!camera) return;
    VkExtent2D extent = renderer->getSwapChainExtent();
    ParticlePC pushConstants = {
        .viewProj = camera->getViewProjectionMatrix(),
        .screenSize = glm::vec2(static_cast<float>(extent.width), static_cast<float>(extent.height)),
        .particleSize = 0.022f,
        .trailWidth = 0.03f,
        .streakScale = 0.0005f
    };
    vkCmdPushConstants(commandBuffer, shader->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ParticlePC), &pushConstants);
    vkCmdDraw(commandBuffer, 4, static_cast<uint32_t>(visibleCount), 0, 0);
}

void engine::ParticleManager::updateAll(float deltaTime) {
#if defined(USE_OPENMP)
    const int count = static_cast<int>(particles.size());
    if (count > 64) {
        #pragma omp parallel for
        for (int i = 0; i < count; ++i) {
            particles[i].update(deltaTime);
        }
    } else
#endif
    for (auto& particle : particles) {
        particle.update(deltaTime);
    }
    std::erase_if(particles, [](const Particle& p) {
        return p.isMarkedForDeletion();
    });
}
