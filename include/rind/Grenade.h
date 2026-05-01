#pragma once

#include <engine/EntityManager.h>
#include <engine/Collider.h>
#include <engine/ParticleManager.h>
#include <engine/AudioManager.h>
#include <engine/VolumetricManager.h>
#include <rind/Player.h>
#include <rind/Enemy.h>
#include <numbers>

namespace rind {
    class Grenade : public engine::Entity {
    public:
        Grenade(
            engine::EntityManager* entityManager,
            Player* player,
            const glm::mat4& transform,
            const glm::vec3& velocity,
            const glm::vec3& color,
            float lifetime = 5.0f
        ) : engine::Entity(entityManager, "grenade" + hashName(transform), "gbuffer", transform, {"materials_slowbullet_albedo", "materials_slowbullet_metallic", "materials_slowbullet_roughness", "materials_slowbullet_normal"}, true, engine::Entity::EntityType::Generic), velocity(velocity), timeRemaining(lifetime), color(color), player(player) {
            setModel(entityManager->getRenderer()->getModelManager()->getModel("slowbullet"));
            collider = new engine::OBBCollider(
                entityManager,
                glm::mat4(1.0f),
                getName(),
                glm::vec3(0.25f)
            );
            collider->setIsDynamic(true);
            collider->setIsTrigger(true);
            addChild(collider);
            particleManager = entityManager->getRenderer()->getParticleManager();
            volumetricManager = entityManager->getRenderer()->getVolumetricManager();
            audioManager = entityManager->getRenderer()->getAudioManager();
        }

        void update(float deltaTime) override {
            timeRemaining -= deltaTime;
            if (timeRemaining <= 0.0f) {
                explode();
                return;
            }
            glm::vec3 movement = velocity * deltaTime;
            velocity.y -= gravity * deltaTime;
            setTransform(glm::translate(getTransform(), movement));
            static thread_local std::vector<engine::Collider*> candidates;
            getEntityManager()->getSpatialGrid().query(collider->getWorldAABB(), candidates);

            engine::Collider* hitCollider = nullptr;

            for (engine::Collider* other : candidates) {
                if (other == collider || other->getType() == engine::Entity::EntityType::Trigger) continue;

                engine::Collider::CollisionMTV mtv;
                if (collider->intersectsMTV(*other, mtv)) {
                    hitCollider = other;
                    break;
                }
            }
            if (hitCollider) {
                explode();
                return;
            }

            engine::Camera* camera = getEntityManager()->getCamera();
            if (!camera) {
                return;
            }
            float distanceToCamera = glm::length(getWorldPosition() - camera->getWorldPosition());
            if (distanceToCamera > 60.0f) {
                getEntityManager()->markForDeletion(this);
                return;
            }
            if (!camera->isSphereInFrustum(getWorldPosition(), 1.0f)) {
                particleManager->burstParticles(
                    getWorldPosition(),
                    color,
                    glm::vec3(0.0f, 1.0f, 0.0f) * 0.5f,
                    4,
                    1.0f,
                    2.0f,
                    0.5f
                );
                return;
            }
            float sizeFactor = dist(rng) * 0.2f + 0.4f; // 0.2 to 0.6
            float randomPhi = dist(rng) * 2.0f * std::numbers::pi_v<float>;
            float randomCostheta = dist(rng);
            float randomSintheta = sqrt(1.0f - randomCostheta * randomCostheta);
            glm::vec3 randomDir = glm::vec3(
                cos(randomPhi) * randomSintheta,
                sin(randomPhi) * randomSintheta,
                randomCostheta
            );
            if (distanceToCamera > 30.0f) {
                particleManager->burstParticles(
                    getWorldPosition(),
                    color,
                    randomDir * 2.0f,
                    6,
                    1.0f,
                    0.8f,
                    sizeFactor
                );
            } else {
                particleManager->burstParticles(
                    getWorldPosition(),
                    color,
                    randomDir * 2.0f,
                    10,
                    1.0f,
                    0.8f,
                    sizeFactor
                );
            }
        }

        void explode() {
            if (exploded) return;
            exploded = true;
            getEntityManager()->markForDeletion(this);
            static thread_local std::vector<engine::Collider*> candidates;
            engine::AABB bigAABB = collider->getWorldAABB();
            bigAABB.min -= glm::vec3(10.0f);
            bigAABB.max += glm::vec3(10.0f);
            getEntityManager()->getSpatialGrid().query(bigAABB, candidates);
            
            engine::Collider* hitCollider = nullptr;
            
            bool showHitmarker = false;
            glm::vec3 hitmarkerColor{0.0f};

            for (engine::Collider* candidate : candidates) {
                if (candidate == collider || candidate->getType() == engine::Entity::EntityType::Trigger) continue;
                
                engine::Entity* other = candidate->getParent();
                float distance = glm::length(glm::vec3(getWorldTransform()[3]) - other->getWorldPosition());
                float damage = glm::clamp(150.0f * (1.0f - distance / 10.0f), 0.0f, 150.0f);

                if (other->getType() == engine::Entity::EntityType::Enemy) {
                    rind::Enemy* enemy = static_cast<rind::Enemy*>(other);
                    enemy->damage(damage);
                    if (player) {
                        showHitmarker = true;
                        if (enemy->getHealth() - damage <= 0.0f) {
                            hitmarkerColor = glm::vec3(1.0f, 0.2f, 0.2f);
                        } else if (hitmarkerColor != glm::vec3(1.0f, 1.0f, 1.0f)) {
                            hitmarkerColor = glm::vec3(1.0f, 1.0f, 1.0f);
                        }
                    }
                } else if (other->getType() == engine::Entity::EntityType::Player) {
                    static_cast<rind::Player*>(other)->damage(damage);
                }
            }
            if (player && showHitmarker) {
                player->showHitmarker(hitmarkerColor);
            }
            audioManager->playSound3D("grenade_explode", getWorldPosition(), 1.2f, 0.15f);
            volumetricManager->createVolumetric(
                glm::scale(
                    getWorldTransform(),
                    glm::vec3(5.0f, 5.0f, 5.0f)
                ),
                glm::scale(
                    getWorldTransform(),
                    glm::vec3(20.0f, 20.0f, 20.0f)
                ),
                glm::vec4(glm::min(color + glm::vec3(0.2f), glm::vec3(1.0f)), 1.0f),
                2.0f,
                6.0f
            );
            volumetricManager->createVolumetric(
                glm::scale(
                    getWorldTransform(),
                    glm::vec3(6.0f, 6.0f, 6.0f)
                ),
                glm::scale(
                    getWorldTransform(),
                    glm::vec3(25.0f, 25.0f, 25.0f)
                ),
                glm::vec4(0.1f, 0.1f, 0.1f, 0.4f),
                4.5f
            );
            particleManager->burstParticles(
                getWorldPosition() + glm::vec3(0.0f, 0.5f, 0.0f),
                color,
                glm::vec3(0.0f, 1.0f, 0.0f) * 5.0f,
                250,
                5.0f,
                0.5f,
                0.5f
            );
            particleManager->burstParticles(
                getWorldPosition() + glm::vec3(0.0f, 0.5f, 0.0f),
                color,
                glm::vec3(0.0f, 1.0f, 0.0f) * 10.0f,
                160,
                7.0f,
                1.0f,
                1.0f
            );
            particleManager->burstParticles(
                getWorldPosition() + glm::vec3(0.0f, 0.5f, 0.0f),
                color,
                glm::vec3(0.0f, 1.0f, 0.0f) * 11.0f,
                50,
                5.0f,
                1.0f,
                1.5f
            );
            particleManager->burstParticles(
                getWorldPosition() + glm::vec3(0.0f, 0.5f, 0.0f),
                color,
                glm::vec3(0.0f, 1.0f, 0.0f) * 12.0f,
                300,
                6.0f,
                2.0f,
                0.4f
            );
        }
    private:
        bool exploded = false;
        glm::vec3 velocity;
        float timeRemaining;
        glm::vec3 color;

        engine::AudioManager* audioManager;
        engine::ParticleManager* particleManager;
        engine::VolumetricManager* volumetricManager;
        Player* player;

        std::mt19937 rng{std::random_device{}()};
        std::uniform_real_distribution<float> dist{-1.0f, 1.0f};

        float gravity = 20.0f;

        engine::OBBCollider* collider;

        std::string hashName(const glm::mat4& transform) {
            glm::vec3 position = glm::vec3(transform[3]);
            uint32_t hash = static_cast<uint32_t>(position.x * 73856093) ^ static_cast<uint32_t>(position.y * 19349663) ^ static_cast<uint32_t>(position.z * 83492791);
            hash ^= static_cast<uint32_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
            return std::to_string(hash);
        }
    };
};