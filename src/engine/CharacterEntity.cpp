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
}

void engine::CharacterEntity::updateMovement(float deltaTime) {
    const float MAX_DELTA_TIME = 0.1f; // clamp deltaTime to avoid large jumps, 100 ms
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

        glm::vec3 worldDir = right * pressed.x + forward * pressed.z;
        glm::vec3 dashDir = right * dashing.x + forward * dashing.z;
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
    // subset integration: horizontal and vertical
    const float MAX_STEP = 0.02f; // 20 ms
    const float totalMoveLength = glm::length(velocity * deltaTime);
    int steps = totalMoveLength > 0.0f ? static_cast<int>(glm::ceil(totalMoveLength / (MAX_STEP * moveSpeed))) : 1;
    steps = glm::clamp(steps, 1, 8);
    const float subDt = deltaTime / static_cast<float>(steps);
    glm::vec3 frameVelocity = velocity;
    bool touchedGround = false;
    glm::vec3 groundNormalAccum(0.0f);
    for (int i = 0; i < steps; ++i) {
        glm::vec3 vStep(0.0f, frameVelocity.y * subDt, 0.0f);
        if (std::abs(vStep.y) < 1e-6f && !touchedGround) {
            Collider::Collision collision = willCollide(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.2f, 0.0f)));
            if (collision.other) {
                float penetration = collision.mtv.penetrationDepth;
                if (penetration > 1e-6f) {
                    glm::vec3 gn = collision.mtv.mtv / penetration;
                    if (gn.y > groundedNormalThreshold) {
                        touchedGround = true;
                        groundNormalAccum += gn;
                        if (velocity.y < 0.0f) {
                            velocity.y = 0.0f;
                        }
                    }
                }
            }
        }
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
                    glm::vec3 n = mtv / penetration;
                    float vn = glm::dot(velocity, n);
                    if (vn < 0.0f) {
                        velocity -= n * vn;
                    }
                    setTransform(applyWorldTranslation(getTransform(), n * 0.001f)); // nudge out slightly
                    if (n.y > groundedNormalThreshold && velocity.y <= 0.0f) {
                        touchedGround = true;
                        groundNormalAccum += n;
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
                    glm::vec3 n = mtv / penetration;
                    float vn = glm::dot(velocity, n);
                    if (vn < 0.0f) {
                        velocity -= n * vn;
                    }
                    setTransform(applyWorldTranslation(getTransform(), n * 0.001f)); // nudge out slightly
                }
            } else {
                setTransform(applyWorldTranslation(getTransform(), hStep));
            }
        }
        Collider::Collision postCollision = willCollide(glm::mat4(1.0f));
        if (postCollision.other) {
            glm::vec3 mtv = postCollision.mtv.mtv;
            float penetration = postCollision.mtv.penetrationDepth;
            
            // check if MTV points up enough to be considered ground
            if (penetration > 1e-6f) {
                glm::vec3 n = mtv / penetration;
                if (n.y > groundedNormalThreshold) {
                    touchedGround = true;
                    groundNormalAccum += n;
                }
                setTransform(applyWorldTranslation(getTransform(), mtv));
                float vn = glm::dot(velocity, n);
                if (vn < 0.0f) {
                    velocity -= n * vn;
                }
                setTransform(applyWorldTranslation(getTransform(), n * 0.001f)); // nudge out slightly
            }
        }
    }
    if (touchedGround) {
        grounded = true;
        groundedTimer = 0.0f;
        if (velocity.y < 0.0f) {
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

void engine::CharacterEntity::move(const glm::vec3& delta) {
    glm::vec3 remappedDelta = delta;
    remapCoord(remappedDelta);
    pressed += remappedDelta;
}

void engine::CharacterEntity::stopMove(const glm::vec3& delta) {
    glm::vec3 remappedDelta = delta;
    remapCoord(remappedDelta);
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
        }
    }
    if (delta.z != 0.0f) {
        Entity* head = this->getChildByName("camera");
        if (!head) {
            head = this->getChildByName("head");
        }
        if (head) {
            glm::mat4 currentTransform = head->getTransform();
            glm::quat currentRotation = glm::quat_cast(currentTransform);
            glm::vec3 eulerAngles = glm::eulerAngles(currentRotation);
            eulerAngles.x = glm::clamp(eulerAngles.x + delta.z, -glm::half_pi<float>() + 0.01f, glm::half_pi<float>() - 0.01f);
            glm::mat4 newTransform = glm::yawPitchRoll(eulerAngles.y, eulerAngles.x, eulerAngles.z);
            newTransform[3] = currentTransform[3];
            head->setTransform(newTransform);
        }
    }
}

engine::Collider::Collision engine::CharacterEntity::willCollide(const glm::mat4& deltaTransform) {
    if (!collider) {
        return Collider::Collision();
    }
    AABB myAABB = collider->getWorldAABB();
    if (glm::length(glm::vec3(deltaTransform[3])) > 1e-6f) {
        myAABB.min += glm::vec3(deltaTransform[3]);
        myAABB.max += glm::vec3(deltaTransform[3]);
    }
    for (auto& [entityName, entity] : getEntityManager()->getEntities()) {
        if (entity == this) {
            continue;
        }
        Collider* otherCollider = nullptr;
        for (auto& child : entity->getChildren()) {
            otherCollider = dynamic_cast<Collider*>(child);
            if (otherCollider) {
                break;
            }
        }
        if (!otherCollider) {
            continue;
        }
        AABB otherAABB = otherCollider->getWorldAABB();
        bool aabbIntersect = Collider::aabbIntersects(myAABB, otherAABB, 0.002);
        if (aabbIntersect) {
            Collider::Collision collision;
            if (collider->intersectsMTV(*otherCollider, collision.mtv, deltaTransform)) {
                collision.other = otherCollider;
                return collision;
            }
        }
    }
    return Collider::Collision();
}