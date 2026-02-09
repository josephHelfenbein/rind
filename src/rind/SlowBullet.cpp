#include <rind/SlowBullet.h>
#include <rind/Player.h>
#include <rind/Enemy.h>

rind::SlowBullet::SlowBullet(engine::EntityManager* entityManager, const std::string& name, glm::mat4 transform, const glm::vec3 velocity)
    : engine::Entity(entityManager, name, "gbuffer", transform, {"materials_slowbullet_albedo", "materials_slowbullet_metallic", "materials_slowbullet_roughness", "materials_slowbullet_normal"}, true), velocity(velocity) {
        setModel(entityManager->getRenderer()->getModelManager()->getModel("slowbullet"));
        setCastShadow(false);
        collider = new engine::OBBCollider(
            entityManager,
            glm::mat4(1.0f),
            name
        );
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
    std::vector<engine::Collider::Collision> hits = engine::Collider::raycast(
        getEntityManager(),
        getWorldPosition(),
        velocity,
        glm::length(velocity) * deltaTime,
        collider,
        true
    );
    if (!hits.empty()) {
        engine::Collider::Collision collision = hits[0];
        glm::vec3 normal = glm::normalize(collision.mtv.normal);
        glm::vec3 reflectedDir = glm::reflect(velocity, normal);
        if (rind::Player* player = dynamic_cast<rind::Player*>(collision.other->getParent())) {
            player->damage(10.0f);
            audioManager->playSound3D("laser_enemy_impact", collision.worldHitPoint, 0.5f, true);
        } else if (rind::Enemy* enemy = dynamic_cast<rind::Enemy*>(collision.other->getParent())) {
            enemy->damage(10.0f);
            audioManager->playSound3D("laser_enemy_impact", collision.worldHitPoint, 0.5f, true);
        } else {
            audioManager->playSound3D("laser_ground_impact", collision.worldHitPoint, 0.5f, true);
        }
        particleManager->burstParticles(
            glm::translate(glm::mat4(1.0f), collision.worldHitPoint),
            glm::vec4(1.0f, 1.0f, 0.5f, 1.0f),
            reflectedDir * 40.0f,
            50,
            4.0f,
            0.5f
        );
        particleManager->burstParticles(
            glm::translate(glm::mat4(1.0f), collision.worldHitPoint),
            glm::vec4(1.0f, 1.0f, 0.5f, 1.0f),
            reflectedDir * 25.0f,
            30,
            4.0f,
            0.4f
        );
        particleManager->burstParticles(
            glm::translate(glm::mat4(1.0f), collision.worldHitPoint),
            glm::vec4(1.0f, 1.0f, 0.5f, 1.0f),
            reflectedDir * 10.0f,
            50,
            2.0f,
            0.3f
        );
        getEntityManager()->markForDeletion(this);
    } else {
        particleManager->burstParticles(
            getWorldTransform(),
            glm::vec4(1.0f, 1.0f, 0.5f, 1.0f),
            -velocity * 0.5f,
            2,
            1.0f,
            0.8f
        );
    }
}