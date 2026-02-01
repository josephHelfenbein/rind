#include <engine/CharacterEntity.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>
#include <engine/io.h>

static inline glm::mat4 applyWorldTranslation(const glm::mat4& transform, const glm::vec3& offset) {
    glm::mat4 result = transform;
    result[3] += glm::vec4(offset, 0.0f);
    return result;
}

void engine::CharacterEntity::update(float deltaTime) {
    updateMovement(deltaTime);
    if (getWorldPosition().y < -30) {
        damage(health);
    }
    if (rotateVelocity != glm::vec3(0.0f)) rotateVelocity = glm::vec3(0.0f);
}

void engine::CharacterEntity::updateMovement(float deltaTime) {
    const float MAX_DELTA_TIME = 0.05f; // clamp deltaTime to avoid large jumps
    deltaTime = glm::min(deltaTime, MAX_DELTA_TIME);
    glm::vec3 desiredVel(0.0f);
    bool shouldDash = glm::length(dashing) > 1e-6f;
    if (glm::length(pressed) > 1e-6f || glm::length(dashing) > 1e-6f) {
        glm::mat4 t = getTransform();
        glm::vec3 forward = -glm::vec3(t[2]);
        forward.y = 0.0f;
        if (glm::length(forward) < 1e-6f) {
            forward = glm::vec3(0.0f, 0.0f, -1.0f);
        } else {
            forward = glm::normalize(forward);
        }
        glm::vec3 right = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), forward);
        if (glm::length(right) < 1e-6f) {
            right = glm::vec3(1.0f, 0.0f, 0.0f);
        } else {
            right = glm::normalize(right);
        }

        glm::vec3 worldDir = right * pressed.x + glm::vec3(0.0f, 1.0f, 0.0f) * pressed.y + forward * pressed.z;
        glm::vec3 dashDir = right * dashing.x + glm::vec3(0.0f, 1.0f, 0.0f) * dashing.y + forward * dashing.z;
        float m = glm::length(worldDir);
        if (m > 1e-6f) {
            worldDir /= m;
            desiredVel = worldDir * moveSpeed;
        }
        if (glm::length(dashDir) > 1e-6f) {
            dashVelocity = dashDir;
            dashing = glm::vec3(0.0f);
        }
    }
    velocity.x = desiredVel.x + dashVelocity.x;
    if (desiredVel.y > 1e-6f || dashVelocity.y > 1e-6f) {
        velocity.y = std::min(desiredVel.y + dashVelocity.y, velocity.y + desiredVel.y + dashVelocity.y);
    }
    velocity.z = desiredVel.z + dashVelocity.z;
    if (glm::length(dashVelocity) > 1e-6f) {
        float decayFactor = glm::exp(-dashDecayRate * deltaTime);
        dashVelocity *= decayFactor;
        if (glm::length(dashVelocity) < 0.1f) {
            dashVelocity = glm::vec3(0.0f);
        }
    }
    if (!grounded || velocity.y > 0.0f) {
        velocity.y -= gravity * deltaTime;
    }
    const float totalMoveLength = glm::length(velocity * deltaTime);
    uint32_t steps;
    if (totalMoveLength < 0.01f) {
        steps = 1; // small movement
    } else if (totalMoveLength < 0.05f) {
        steps = 2; // slow movement
    } else if (totalMoveLength < 0.15f) {
        steps = 4; // normal movement
    } else {
        steps = glm::min(static_cast<int>(glm::ceil(totalMoveLength / 0.05f)), 12); // fast movement
    }
    
    const float subDt = deltaTime / static_cast<float>(steps);
    glm::vec3 frameVelocity = velocity;
    bool touchedGround = false;
    glm::vec3 groundNormalAccum(0.0f);
    for (uint32_t i = 0u; i < steps; ++i) {
        glm::vec3 vStep(0.0f, frameVelocity.y * subDt, 0.0f);
        if (std::abs(vStep.y) >= 1e-6f) {
            Collider::Collision collision = willCollide(glm::translate(glm::mat4(1.0f), vStep));
            if (collision.other) {
                glm::vec3 mtv = collision.mtv.mtv;
                if (glm::dot(mtv, vStep) > 0.0f) {
                    mtv = -mtv; // oppose attempted vertical motion
                }
                glm::vec3 offset = vStep + mtv;
                setTransform(applyWorldTranslation(getTransform(), offset));
                float penetration = collision.mtv.penetrationDepth;
                if (penetration > 1e-6f) {
                    glm::vec3 n = glm::normalize(mtv);
                    float vn = glm::dot(frameVelocity, n);
                    if (vn < 0.0f) {
                        frameVelocity -= n * vn;
                        velocity -= n * glm::dot(velocity, n);
                    }
                    if (n.y > groundedNormalThreshold && frameVelocity.y <= 0.0f) {
                        touchedGround = true;
                        groundNormalAccum += n;
                        frameVelocity.y = 0.0f;
                        velocity.y = 0.0f;
                    }
                }
            } else {
                setTransform(applyWorldTranslation(getTransform(), vStep));
            }
        }
        glm::vec3 hStep(frameVelocity.x * subDt, 0.0f, frameVelocity.z * subDt);
        if (glm::length(hStep) > 1e-6f) {
            Collider::Collision collision = willCollide(glm::translate(glm::mat4(1.0f), hStep));
            if (collision.other) {
                glm::vec3 mtv = collision.mtv.mtv;
                if (glm::dot(mtv, hStep) > 0.0f) {
                    mtv = -mtv; // oppose attempted horizontal motion
                }
                float mtvLen = glm::length(mtv);
                if (mtvLen > 1e-6f) {
                    glm::vec3 mtvNorm = mtv / mtvLen;
                    if (mtvNorm.y > groundedNormalThreshold) {
                        touchedGround = true;
                        groundNormalAccum += mtvNorm;
                    }
                }
                glm::vec3 offset = hStep + mtv;
                setTransform(applyWorldTranslation(getTransform(), offset));
                float penetration = collision.mtv.penetrationDepth;
                if (penetration > 1e-6f) {
                    glm::vec3 n = glm::normalize(mtv);
                    float vn = glm::dot(frameVelocity, n);
                    if (vn < 0.0f) {
                        frameVelocity -= n * vn;
                        velocity -= n * glm::dot(velocity, n);
                    }
                }
            } else {
                setTransform(applyWorldTranslation(getTransform(), hStep));
            }
        }
    }
    Collider::Collision postCollision = willCollide(glm::mat4(1.0f));
    if (postCollision.other) {
        glm::vec3 mtv = postCollision.mtv.mtv;
        float penetration = postCollision.mtv.penetrationDepth;
        
        // check if MTV points up enough to be considered ground
        if (penetration > 1e-6f) {
            glm::vec3 n = glm::normalize(mtv);
            if (n.y > groundedNormalThreshold) {
                touchedGround = true;
                groundNormalAccum += n;
            }
            setTransform(applyWorldTranslation(getTransform(), mtv));
            float vn = glm::dot(frameVelocity, n);
            if (vn < 0.0f) {
                frameVelocity -= n * vn;
                velocity -= n * glm::dot(velocity, n);
            }
        }
    }
    if (touchedGround) {
        grounded = true;
        groundedTimer = 0.0f;
        if (velocity.y < 1e-6f) {
            velocity.y = 0.0f;
        }
    } else {
        grounded = groundedTimer <= coyoteTime;
        groundedTimer += deltaTime;
    }
    if (glm::length(velocity) < 1e-6f) {
        velocity = glm::vec3(0.0f);
    }
}

void engine::CharacterEntity::move(const glm::vec3& delta, bool remap) {
    glm::vec3 remappedDelta = delta;
    if (remap) {
        remapCoord(remappedDelta);
    }
    pressed += remappedDelta;
}

void engine::CharacterEntity::stopMove(const glm::vec3& delta, bool remap) {
    glm::vec3 remappedDelta = delta;
    if (remap) {
        remapCoord(remappedDelta);
    }
    pressed -= remappedDelta;
    if (glm::length(pressed) < 1e-6f) {
        pressed = glm::vec3(0.0f);
    }
}

void engine::CharacterEntity::dash(const glm::vec3& direction, float strength) {
    dashing = glm::normalize(direction) * strength;
}

void engine::CharacterEntity::jump(float strength) {
    if (grounded || groundedTimer <= coyoteTime) {
        velocity.y = strength * jumpSpeed;
        grounded = false;
    }
}

void engine::CharacterEntity::rotate(const glm::vec3& delta) {
    if (delta.y != 0.0f) {
        glm::mat4 currentTransform = getTransform();
        glm::quat currentRotation = glm::quat_cast(currentTransform);
        glm::quat yawDelta = glm::angleAxis(delta.y, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::quat newRotation = glm::normalize(yawDelta * currentRotation);

        glm::mat4 newTransform = glm::mat4_cast(newRotation);
        newTransform[3] = currentTransform[3];

        glm::mat4 deltaTransform = glm::inverse(currentTransform) * newTransform;
        engine::Collider::Collision collision = willCollide(deltaTransform);

        bool allowRotation = false;
        if (!collision.other) {
            allowRotation = true;
        } else if (collision.mtv.penetrationDepth < 1e-6f) {
            allowRotation = true;
        } else {
            glm::vec3 n = collision.mtv.mtv / collision.mtv.penetrationDepth;
            if (std::abs(n.y) > 0.6f) {
                allowRotation = true;
            }
        }

        if (allowRotation) {
            setTransform(newTransform);
            rotateVelocity.y = delta.y / getEntityManager()->getRenderer()->getDeltaTime();
        }
    }
    if (delta.z != 0.0f) {
        Entity* head = getHead();
        if (head) {
            glm::mat4 currentTransform = head->getTransform();
            glm::quat currentRotation = glm::quat_cast(currentTransform);
            glm::vec3 eulerAngles = glm::eulerAngles(currentRotation);
            eulerAngles.x = glm::clamp(eulerAngles.x + delta.z, -glm::half_pi<float>() + 0.01f, glm::half_pi<float>() - 0.01f);
            glm::mat4 newTransform = glm::yawPitchRoll(eulerAngles.y, eulerAngles.x, eulerAngles.z);
            newTransform[3] = currentTransform[3];
            head->setTransform(newTransform);
            rotateVelocity.x = delta.z / getEntityManager()->getRenderer()->getDeltaTime();
        }
    }
}

engine::Collider::Collision engine::CharacterEntity::willCollide(const glm::mat4& deltaTransform) {
    if (!collider) {
        return Collider::Collision();
    }
    AABB myAABB = collider->getWorldAABB();
    glm::vec3 delta = glm::vec3(deltaTransform[3]);
    if (glm::length(delta) > 1e-6f) {
        myAABB.min += delta;
        myAABB.max += delta;
    }
    const float margin = 0.1f;
    AABB queryAABB = {
        myAABB.min - glm::vec3(margin),
        myAABB.max + glm::vec3(margin)
    };
    std::vector<Collider*> candidates;
    getEntityManager()->getSpatialGrid().query(queryAABB, candidates);
    
    for (Collider* otherCollider : candidates) {
        if (otherCollider == collider) {
            continue;
        }
        AABB otherAABB = otherCollider->getWorldAABB();
        if (!Collider::aabbIntersects(myAABB, otherAABB, 0.002f)) {
            continue;
        }
        Collider::Collision collision;
        if (collider->intersectsMTV(*otherCollider, collision.mtv, deltaTransform)) {
            collision.other = otherCollider;
            return collision;
        }
    }
    return Collider::Collision();
}
