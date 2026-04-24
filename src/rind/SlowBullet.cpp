#include <rind/SlowBullet.h>
#include <rind/Player.h>
#include <rind/Enemy.h>
#include <engine/Camera.h>
#include <engine/SpatialGrid.h>
#include <numbers>

rind::SlowBullet::SlowBullet(
    engine::EntityManager* entityManager,
    const std::string& name,
    const glm::mat4& transform,
    const glm::vec3& velocity,
    const glm::vec3& color
) : engine::Entity(entityManager, name, "gbuffer", transform, {"materials_slowbullet_albedo", "materials_slowbullet_metallic", "materials_slowbullet_roughness", "materials_slowbullet_normal"}, true), velocity(velocity), color(color) {
        setModel(entityManager->getRenderer()->getModelManager()->getModel("slowbullet"));
        setCastShadow(false);
        collider = new engine::OBBCollider(
            entityManager,
            glm::mat4(1.0f),
            name,
            glm::vec3(0.25f)
        );
        collider->setIsTrigger(true);
        collider->setIsDynamic(true);
        addChild(collider);
        particleManager = entityManager->getRenderer()->getParticleManager();
        audioManager = entityManager->getRenderer()->getAudioManager();
    }

void rind::SlowBullet::update(float deltaTime) {
    timeAlive += deltaTime;
    if (timeAlive >= lifetime) {
        getEntityManager()->markForDeletion(this);
    } else {
        glm::vec3 movement = velocity * deltaTime;
        setTransform(glm::translate(getTransform(), movement));
    }
    
    engine::AABB bulletAABB = collider->getWorldAABB();
    static thread_local std::vector<engine::Collider*> candidates;
    getEntityManager()->getSpatialGrid().query(bulletAABB, candidates);
    
    engine::Collider* hitCollider = nullptr;
    engine::Collider::CollisionMTV hitMtv;
    
    for (engine::Collider* other : candidates) {
        if (other == collider || other->getType() == engine::Entity::EntityType::Trigger) continue;
        
        engine::Collider::CollisionMTV mtv;
        if (collider->intersectsMTV(*other, mtv)) {
            hitCollider = other;
            hitMtv = mtv;
            break;
        }
    }
    
    if (hitCollider) {
        glm::vec3 hitPoint = getWorldPosition();
        glm::vec3 normal = glm::length(hitMtv.normal) > 1e-6f ? glm::normalize(hitMtv.normal) : glm::normalize(-velocity);
        glm::vec3 reflectedDir = glm::reflect(velocity, normal);
        engine::Entity* other = hitCollider->getParent();
        if (other->getType() == engine::Entity::EntityType::Player) {
            Player* player = static_cast<Player*>(other);
            player->damage(10.0f);
            audioManager->playSound3D("laser_enemy_impact", hitPoint, 0.5f, 0.2F);
        } else if (other->getType() == engine::Entity::EntityType::Enemy) {
            rind::Enemy* enemy = static_cast<rind::Enemy*>(other);
            enemy->damage(10.0f);
            audioManager->playSound3D("laser_enemy_impact", hitPoint, 0.5f, 0.2F);
        } else {
            audioManager->playSound3D("laser_ground_impact", hitPoint, 0.5f, 0.2F);
        }
        particleManager->burstParticles(
            hitPoint,
            color,
            reflectedDir * 40.0f,
            50,
            4.0f,
            0.5f,
            0.9f
        );
        particleManager->burstParticles(
            hitPoint,
            color,
            reflectedDir * 25.0f,
            100,
            4.0f,
            0.4f,
            0.4f
        );
        particleManager->burstParticles(
            hitPoint,
            color,
            reflectedDir * 10.0f,
            50,
            2.0f,
            0.3f,
            0.7f
        );
        particleManager->burstParticles(
            hitPoint,
            color,
            reflectedDir * 30.0f,
            40,
            3.0f,
            0.35f,
            1.1f
        );
        getEntityManager()->markForDeletion(this);
    } else {
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
                2,
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
                2,
                1.0f,
                0.8f,
                sizeFactor
            );
        } else {
            particleManager->burstParticles(
                getWorldPosition(),
                color,
                randomDir * 2.0f,
                4,
                1.0f,
                0.8f,
                sizeFactor
            );
        }
        float streakRoll = dist(rng) + 1.0f;
        if (streakRoll > 1.0f) { // 50% chance
            glm::vec3 startPos = getWorldPosition() + randomDir * 0.2f;
            float randomPhi2 = dist(rng) * 2.0f * std::numbers::pi_v<float>;
            float randomCostheta2 = dist(rng);
            float randomSintheta2 = sqrt(1.0f - randomCostheta2 * randomCostheta2);
            glm::vec3 randomDir2 = glm::vec3(
                cos(randomPhi2) * randomSintheta2,
                sin(randomPhi2) * randomSintheta2,
                randomCostheta2
            );
            particleManager->spawnTrail(startPos, randomDir2 * 0.75f, color, 0.2f);
        }
        if (streakRoll > 1.95f) { // 2.5% chance
            std::string choice = streakRoll >= 1.97 ? "1" : "2";
            audioManager->playSound3D("slowbullet_sound_" + choice, getWorldPosition(), 0.4f, 0.5F);
        }
    }
}