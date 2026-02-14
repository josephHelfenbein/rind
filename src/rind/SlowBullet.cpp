#include <rind/SlowBullet.h>
#include <rind/Player.h>
#include <rind/Enemy.h>
#include <engine/SpatialGrid.h>

#define PI 3.14159265358979323846f

rind::SlowBullet::SlowBullet(engine::EntityManager* entityManager, const std::string& name, glm::mat4 transform, const glm::vec3 velocity, const glm::vec4 color)
    : engine::Entity(entityManager, name, "gbuffer", transform, {"materials_slowbullet_albedo", "materials_slowbullet_metallic", "materials_slowbullet_roughness", "materials_slowbullet_normal"}, true), velocity(velocity), color(color) {
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
    std::vector<engine::Collider*> candidates;
    getEntityManager()->getSpatialGrid().query(bulletAABB, candidates);
    
    engine::Collider* hitCollider = nullptr;
    engine::Collider::CollisionMTV hitMtv;
    
    for (engine::Collider* other : candidates) {
        if (other == collider) continue;
        
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
        if (rind::Player* player = dynamic_cast<rind::Player*>(hitCollider->getParent())) {
            player->damage(10.0f);
            audioManager->playSound3D("laser_enemy_impact", hitPoint, 0.5f, true);
        } else if (rind::Enemy* enemy = dynamic_cast<rind::Enemy*>(hitCollider->getParent())) {
            enemy->damage(10.0f);
            audioManager->playSound3D("laser_enemy_impact", hitPoint, 0.5f, true);
        } else {
            audioManager->playSound3D("laser_ground_impact", hitPoint, 0.5f, true);
        }
        particleManager->burstParticles(
            glm::translate(glm::mat4(1.0f), hitPoint),
            color,
            reflectedDir * 40.0f,
            50,
            4.0f,
            0.5f
        );
        particleManager->burstParticles(
            glm::translate(glm::mat4(1.0f), hitPoint),
            color,
            reflectedDir * 25.0f,
            30,
            4.0f,
            0.4f
        );
        particleManager->burstParticles(
            glm::translate(glm::mat4(1.0f), hitPoint),
            color,
            reflectedDir * 10.0f,
            50,
            2.0f,
            0.8f
        );
        getEntityManager()->markForDeletion(this);
    } else {
        particleManager->burstParticles(
            getWorldTransform(),
            color,
            -velocity * 0.5f,
            2,
            1.0f,
            0.8f
        );
        float streakRoll = dist(rng) + 1.0f;
        if (streakRoll > 1.9f) {
            glm::vec3 rotAxis = glm::normalize(glm::vec3(dist(rng), dist(rng), dist(rng)));
            float rotAmount = dist(rng) * 2.0f * PI;
            glm::mat4 rot = glm::rotate(glm::mat4(1.0f), rotAmount, rotAxis);
            glm::vec3 offset = glm::vec3(rot * glm::vec4(0.25f, 0.0f, 0.0f, 1.0f));
            glm::vec3 startPos = getWorldPosition() + offset;
            glm::mat4 rot2 = glm::rotate(glm::mat4(1.0f), rotAmount + 0.25f, rotAxis);
            glm::vec3 endOffset = glm::vec3(rot2 * glm::vec4(0.25f, 0.0f, 0.0f, 1.0f));
            glm::vec3 endPos = getWorldPosition() + endOffset;
            particleManager->spawnTrail(startPos, glm::normalize(endPos - startPos), color, 0.3f);
        }
        float audioRoll = dist(rng) + 1.0f;
        if (streakRoll > 1.95f) {
            std::string choice = streakRoll >= 1.97 ? "1" : "2";
            audioManager->playSound3D("slowbullet_sound_" + choice, getWorldPosition(), 0.4f, true);
        }
    }
}