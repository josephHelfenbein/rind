#include <engine/ParticleManager.h>
#include <engine/Renderer.h>
#include <engine/EntityManager.h>
#include <engine/ShaderManager.h>
#include <engine/PushConstants.h>
#include <engine/Camera.h>
#include <engine/SpatialGrid.h>
#include <engine/ThreadPool.h>
#include <engine/SIMD.h>
#include <glm/gtc/matrix_transform.hpp>

void engine::ParticleManager::ParticleSoA::clearAll() {
    posX.clear(); posY.clear(); posZ.clear();
    velX.clear(); velY.clear(); velZ.clear();
    prevPosX.clear(); prevPosY.clear(); prevPosZ.clear();
    prevPrevPosX.clear(); prevPrevPosY.clear(); prevPrevPosZ.clear();
    age.clear(); lifetime.clear(); type.clear(); dead.clear();
    size.clear(); colorR.clear(); colorG.clear(); colorB.clear();
}

size_t engine::ParticleManager::ParticleSoA::push(
    const glm::vec3& pos, const glm::vec3& col, const glm::vec3& vel,
    float life, float typ, float sz)
{
    const size_t i = posX.size();
    posX.push_back(pos.x); posY.push_back(pos.y); posZ.push_back(pos.z);
    velX.push_back(vel.x); velY.push_back(vel.y); velZ.push_back(vel.z);
    prevPosX.push_back(pos.x); prevPosY.push_back(pos.y); prevPosZ.push_back(pos.z);
    prevPrevPosX.push_back(pos.x); prevPrevPosY.push_back(pos.y); prevPrevPosZ.push_back(pos.z);
    age.push_back(0.0f);
    lifetime.push_back(life);
    type.push_back(typ);
    dead.push_back(0);
    size.push_back(sz);
    colorR.push_back(col.r); colorG.push_back(col.g); colorB.push_back(col.b);
    return i;
}

void engine::ParticleManager::ParticleSoA::truncateFront(size_t n) {
    if (n == 0) return;
    if (n >= count()) { clearAll(); return; }
    for (size_t i = 0; i < n; ++i) dead[i] = 1;
    compactDead();
}

void engine::ParticleManager::ParticleSoA::compactDead() {
    const size_t n = count();
    size_t w = 0;
    for (size_t r = 0; r < n; ++r) {
        if (dead[r]) continue;
        if (w != r) {
            posX[w] = posX[r]; posY[w] = posY[r]; posZ[w] = posZ[r];
            velX[w] = velX[r]; velY[w] = velY[r]; velZ[w] = velZ[r];
            prevPosX[w] = prevPosX[r]; prevPosY[w] = prevPosY[r]; prevPosZ[w] = prevPosZ[r];
            prevPrevPosX[w] = prevPrevPosX[r]; prevPrevPosY[w] = prevPrevPosY[r]; prevPrevPosZ[w] = prevPrevPosZ[r];
            age[w] = age[r]; lifetime[w] = lifetime[r]; type[w] = type[r];
            dead[w] = 0;
            size[w] = size[r];
            colorR[w] = colorR[r]; colorG[w] = colorG[r]; colorB[w] = colorB[r];
        }
        ++w;
    }
    posX.resize(w); posY.resize(w); posZ.resize(w);
    velX.resize(w); velY.resize(w); velZ.resize(w);
    prevPosX.resize(w); prevPosY.resize(w); prevPosZ.resize(w);
    prevPrevPosX.resize(w); prevPrevPosY.resize(w); prevPrevPosZ.resize(w);
    age.resize(w); lifetime.resize(w); type.resize(w); dead.resize(w);
    size.resize(w); colorR.resize(w); colorG.resize(w); colorB.resize(w);
}

engine::ParticleGPU engine::ParticleManager::makeGPU(size_t i) const {
    return {
        .position = glm::vec4(particles.posX[i], particles.posY[i], particles.posZ[i], particles.age[i]),
        .prevPosition = glm::vec4(particles.prevPosX[i], particles.prevPosY[i], particles.prevPosZ[i], particles.lifetime[i]),
        .prevPrevPosition = glm::vec4(particles.prevPrevPosX[i], particles.prevPrevPosY[i], particles.prevPrevPosZ[i], particles.type[i]),
        .color = glm::vec4(particles.colorR[i], particles.colorG[i], particles.colorB[i], particles.size[i])
    };
}

void engine::ParticleManager::collideOne(size_t i, float deltaTime) {
    if (particles.dead[i]) return;
    if (particles.type[i] == 1.0f) return;
    if (particles.age[i] <= 0.15f) return;

    const float vx = particles.velX[i];
    const float vy = particles.velY[i];
    const float vz = particles.velZ[i];
    const float speedSq = vx * vx + vy * vy + vz * vz;
    if (speedSq <= 1.0f) return;

    const glm::vec3 currentPos(particles.prevPosX[i], particles.prevPosY[i], particles.prevPosZ[i]);
    glm::vec3 newPos(particles.posX[i], particles.posY[i], particles.posZ[i]);

    const float particleRadius = 0.05f;
    const float stepLen = std::sqrt(speedSq) * deltaTime;
    const float expand = particleRadius + stepLen;
    engine::AABB sweepAABB = {
        .min = glm::min(currentPos, newPos) - glm::vec3(expand),
        .max = glm::max(currentPos, newPos) + glm::vec3(expand)
    };
    static thread_local engine::SpatialGrid::Candidates candidates;
    renderer->getEntityManager()->getSpatialGrid().query(sweepAABB, candidates, 0.0f);

    Collider::Collision collision = narrowPhaseCollision(newPos, candidates);
    if (!collision.other) return;

    glm::vec3 velocity(vx, vy, vz);
    glm::vec3 normal = glm::normalize(collision.mtv.normal);
    velocity = velocity - 2.0f * glm::dot(velocity, normal) * normal;
    velocity *= 0.5f;
    particles.velX[i] = velocity.x;
    particles.velY[i] = velocity.y;
    particles.velZ[i] = velocity.z;

    newPos = currentPos + velocity * deltaTime;
    if (narrowPhaseCollision(newPos, candidates).other) {
        particles.dead[i] = 1;
        return;
    }

    particles.posX[i] = newPos.x;
    particles.posY[i] = newPos.y;
    particles.posZ[i] = newPos.z;
}

engine::Collider::Collision engine::ParticleManager::checkCollision(const glm::vec3& position) {
    const float particleRadius = 0.05f;
    engine::AABB particleAABB = {
        .min = position - glm::vec3(particleRadius),
        .max = position + glm::vec3(particleRadius)
    };
    static thread_local engine::SpatialGrid::Candidates candidates;
    renderer->getEntityManager()->getSpatialGrid().query(particleAABB, candidates, 0.0f);
    return narrowPhaseCollision(position, candidates);
}

engine::Collider::Collision engine::ParticleManager::narrowPhaseCollision(const glm::vec3& position, const engine::SpatialGrid::Candidates& candidates) {
    const float particleRadius = 0.05f;
    engine::AABB particleAABB = {
        .min = position - glm::vec3(particleRadius),
        .max = position + glm::vec3(particleRadius)
    };
    const size_t n = candidates.size();
    for (size_t idx = 0; idx < n; ++idx) {
        if (!candidates.intersects[idx]) continue;
        if (particleAABB.min.x > candidates.maxX[idx] || particleAABB.max.x < candidates.minX[idx] ||
            particleAABB.min.y > candidates.maxY[idx] || particleAABB.max.y < candidates.minY[idx] ||
            particleAABB.min.z > candidates.maxZ[idx] || particleAABB.max.z < candidates.minZ[idx]) {
            continue;
        }
        Collider* collider = candidates.colliders[idx];
        const engine::AABB otherAABB = {
            .min = glm::vec3(candidates.minX[idx], candidates.minY[idx], candidates.minZ[idx]),
            .max = glm::vec3(candidates.maxX[idx], candidates.maxY[idx], candidates.maxZ[idx])
        };
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
    particles.clearAll();
}

void engine::ParticleManager::clear() {
    std::fill(particles.dead.begin(), particles.dead.end(), uint8_t{1});
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
    if (particles.count() >= hardCap) return;
    float velLength = glm::length(velocity) + dist(rng) * 0.1f * glm::length(velocity);
    size_t remaining = hardCap - particles.count();
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
        particles.push(position, particleColor, velocity + velocityOffset, particleLifetime, 0.0f, size);
    }
}

void engine::ParticleManager::spawnTrail(const glm::vec3& start, const glm::vec3& dir, const glm::vec3& color, float lifetime, float fakeAge) {
    if (particles.count() >= hardCap) return;
    const size_t i = particles.push(start, color, glm::vec3(0.0f), lifetime, 1.0f, 1.0f);
    // Trail particles encode (start, dir) into the prevPrev / prev slots for streak rendering
    particles.prevPosX[i] = dir.x;
    particles.prevPosY[i] = dir.y;
    particles.prevPosZ[i] = dir.z;
    particles.prevPrevPosX[i] = start.x;
    particles.prevPrevPosY[i] = start.y;
    particles.prevPrevPosZ[i] = start.z;
    particles.age[i] = fakeAge;
}

void engine::ParticleManager::updateParticleBuffer(uint32_t currentFrame) {
    VkDevice device = renderer->getDevice();
    if (particles.count() > hardCap) {
        size_t toRemove = particles.count() - hardCap;
        particles.truncateFront(toRemove);
    }
    if (particles.count() > maxParticles) {
        vkDeviceWaitIdle(device);
        maxParticles = std::min(std::max(maxParticles * 2, static_cast<uint32_t>(particles.count())), hardCap);
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
        renderer->createComputeDescriptorSets();
    }
    ParticleGPU* gpuData = static_cast<ParticleGPU*>(particleBuffersMapped[currentFrame]);
    visibleCount = 0;
    uint32_t backIdx = static_cast<uint32_t>(particles.count());
    Camera* camera = renderer->getEntityManager()->getCamera();
    if (!camera) return;
    const size_t n = particles.count();
    for (size_t i = 0; i < n; ++i) {
        if (camera->isSphereInFrustum(positionAt(i), 0.1f)) {
            gpuData[visibleCount++] = makeGPU(i);
        } else {
            gpuData[--backIdx] = makeGPU(i);
        }
    }
    for (size_t i = visibleCount; i < n; ++i) {
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
        .particleSize = 0.017f,
        .trailWidth = 0.03f,
        .streakScale = 0.00045f
    };
    vkCmdPushConstants(commandBuffer, shader->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ParticlePC), &pushConstants);
    vkCmdDraw(commandBuffer, 4, static_cast<uint32_t>(visibleCount), 0, 0);
}

void engine::ParticleManager::updateAll(float deltaTime) {
    const size_t count = particles.count();

    // SIMD kinematics for every particle
    engine::simd::integrateParticleKinematics(
        particles.posX.data(), particles.posY.data(), particles.posZ.data(),
        particles.velX.data(), particles.velY.data(), particles.velZ.data(),
        particles.prevPosX.data(), particles.prevPosY.data(), particles.prevPosZ.data(),
        particles.prevPrevPosX.data(), particles.prevPrevPosY.data(), particles.prevPrevPosZ.data(),
        particles.age.data(),
        particles.lifetime.data(),
        particles.type.data(),
        particles.dead.data(),
        count,
        deltaTime,
        kGravity
    );

    // scalar collision for the subset of particles that need it
    if (count > 64) {
        ThreadPool::global().parallel_for_chunks(0, count, 32, [&](size_t b, size_t e, size_t) {
            for (size_t i = b; i < e; ++i) {
                collideOne(i, deltaTime);
            }
        });
    } else {
        for (size_t i = 0; i < count; ++i) {
            collideOne(i, deltaTime);
        }
    }

    particles.compactDead();
}
