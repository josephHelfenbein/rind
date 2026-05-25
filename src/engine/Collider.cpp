#include <engine/Collider.h>
#include <engine/SpatialGrid.h>
#include <engine/ThreadPool.h>
#include <engine/SIMD.h>
#include <algorithm>

engine::Collider::Collider(
    EntityManager* entityManager,
    const std::string& name,
    const glm::mat4& transform,
    const ColliderType& type
) : Entity(entityManager, name, "", transform, {}, false, EntityType::Collider), type(type) {
        entityManager->addCollider(this);
    }

engine::Collider::~Collider() {
    if (getEntityManager()) {
        getEntityManager()->removeCollider(this);
    }
}

engine::Collider::Collision engine::Collider::testRayCollision(Collider* collider, AABB rayAABB, const glm::vec3& rayOrigin, const glm::vec3& rayDir, float maxDistance, Collider* ignoreCollider) {
    if (collider == ignoreCollider || collider->getIsTrigger()) {
        return {};
    }
    AABB aabb = collider->getWorldAABB();
    if (!aabbIntersects(rayAABB, aabb)) {
        return {};
    }
    switch (collider->getColliderType()) {
        case ColliderType::AABB: {
            const glm::vec3 invDir = 1.0f / rayDir;
            const glm::vec3 t1 = (aabb.min - rayOrigin) * invDir;
            const glm::vec3 t2 = (aabb.max - rayOrigin) * invDir;
            const glm::vec3 tmin = glm::min(t1, t2);
            const glm::vec3 tmax = glm::max(t1, t2);
            const float tNear = glm::max(glm::max(tmin.x, tmin.y), tmin.z);
            const float tFar = glm::min(glm::min(tmax.x, tmax.y), tmax.z);
            if (tNear <= tFar && tFar >= 0.0f && tNear <= maxDistance) {
                const float tHit = tNear >= 0.0f ? tNear : tFar;
                glm::vec3 normal(0.0f);
                if (tNear >= 0.0f) {
                    if (tmin.x == tNear) normal = glm::vec3(rayDir.x > 0 ? -1.0f : 1.0f, 0.0f, 0.0f);
                    else if (tmin.y == tNear) normal = glm::vec3(0.0f, rayDir.y > 0 ? -1.0f : 1.0f, 0.0f);
                    else normal = glm::vec3(0.0f, 0.0f, rayDir.z > 0 ? -1.0f : 1.0f);
                }
                Collision collision = {
                    .other = collider,
                    .mtv = { .normal = normal },
                    .worldHitPoint = rayOrigin + rayDir * tHit
                };
                return collision;
            }
            break;
        }
        case ColliderType::OBB: {
            OBBCollider* obbCollider = static_cast<OBBCollider*>(collider);
            obbCollider->ensureCached();
            const auto& axes = obbCollider->getAxesCache();
            glm::vec3 obbCenter = obbCollider->getCenterCache();
            glm::vec3 halfSize = obbCollider->getHalfSize();
            glm::vec3 delta = obbCenter - rayOrigin;
            float halfSizes[3] = { halfSize.x, halfSize.y, halfSize.z };
            float tMin = 0.0f;
            float tMax = maxDistance;
            bool hit = true;
            int hitAxis = -1;
            float hitAxisDir = 0.0f;
            for (int i = 0; i < 3; ++i) {
                float e = glm::dot(axes[i], delta);
                float f = glm::dot(axes[i], rayDir);
                if (std::abs(f) > 1e-6f) {
                    float t1 = (e - halfSizes[i]) / f;
                    float t2 = (e + halfSizes[i]) / f;
                    float sign = 1.0f;
                    if (t1 > t2) { std::swap(t1, t2); sign = -1.0f; }
                    if (t1 > tMin) {
                        tMin = t1;
                        hitAxis = i;
                        hitAxisDir = -sign * (f > 0 ? 1.0f : -1.0f);
                    }
                    tMax = glm::min(tMax, t2);
                    if (tMin > tMax) {
                        hit = false;
                        break;
                    }
                } else if (-e - halfSizes[i] > 0.0f || -e + halfSizes[i] < 0.0f) {
                    hit = false;
                    break;
                }
            }
            if (hit && tMax >= 0.0f) {
                glm::vec3 normal = hitAxis >= 0 ? axes[hitAxis] * hitAxisDir : glm::vec3(0.0f);
                Collision collision = {
                    .other = collider,
                    .mtv = { .normal = normal },
                    .worldHitPoint = rayOrigin + rayDir * (tMin >= 0.0f ? tMin : tMax)
                };
                return collision;
            }
            break;
        }
        case ColliderType::ConvexHull: {
            ConvexHullCollider* hullCollider = static_cast<ConvexHullCollider*>(collider);
            const std::vector<glm::vec3>& faceAxes = hullCollider->getFaceAxesCached();
            const std::vector<glm::vec3>& worldVerts = hullCollider->getWorldVerts();
            glm::vec3 center = hullCollider->getWorldCenter();
            float tMin = 0.0f;
            float tMax = maxDistance;
            bool hit = true;
            glm::vec3 hitNormal(0.0f);
            for (const glm::vec3& normal : faceAxes) {
                float hullMin = std::numeric_limits<float>::max();
                float hullMax = std::numeric_limits<float>::lowest();
                for (const glm::vec3& v : worldVerts) {
                    float proj = glm::dot(v, normal);
                    hullMin = glm::min(hullMin, proj);
                    hullMax = glm::max(hullMax, proj);
                }
                float originProj = glm::dot(rayOrigin, normal);
                float dirProj = glm::dot(rayDir, normal);
                if (std::abs(dirProj) > 1e-6f) {
                    float t1 = (hullMin - originProj) / dirProj;
                    float t2 = (hullMax - originProj) / dirProj;
                    if (t1 > t2) std::swap(t1, t2);
                    if (t1 > tMin) {
                        tMin = t1;
                        hitNormal = dirProj > 0 ? -normal : normal;
                    }
                    tMax = glm::min(tMax, t2);
                    if (tMin > tMax) {
                        hit = false;
                        break;
                    }
                } else if (originProj < hullMin || originProj > hullMax) {
                    hit = false;
                    break;
                }
            }
            if (hit && tMax >= 0.0f) {
                Collision collision = {
                    .other = collider,
                    .mtv = { .normal = hitNormal },
                    .worldHitPoint = rayOrigin + rayDir * (tMin >= 0.0f ? tMin : tMax)
                };
                return collision;
            }
            break;
        }
        default:
            break;
    }
    return {};
}

inline void filterCandidatesByRay(
    const engine::SpatialGrid::Candidates& candidates,
    const glm::vec3& rayOrigin, const glm::vec3& rayDir, float maxDistance,
    std::vector<uint8_t>& rayHit, std::vector<float>& rayTHit
) {
    const size_t n = candidates.size();
    rayHit.assign(n, 0);
    rayTHit.assign(n, 0.0f);
    if (n == 0) return;
    const float origin[3] = { rayOrigin.x, rayOrigin.y, rayOrigin.z };
    const float dir[3] = { rayDir.x, rayDir.y, rayDir.z };
    engine::simd::rayVsManyAABBs(
        origin, dir,
        candidates.minX.data(), candidates.minY.data(), candidates.minZ.data(),
        candidates.maxX.data(), candidates.maxY.data(), candidates.maxZ.data(),
        n, maxDistance,
        rayHit.data(), rayTHit.data()
    );
}

engine::Collider::Collision engine::Collider::raycastFirst(EntityManager* entityManager, const glm::vec3& rayOrigin, const glm::vec3& rayDir, float maxDistance, Collider* ignoreCollider, float margin) {
    glm::vec3 rayEnd = rayOrigin + rayDir * maxDistance;
    AABB rayAABB = {
        .min = glm::min(rayOrigin, rayEnd) - glm::vec3(margin),
        .max = glm::max(rayOrigin, rayEnd) + glm::vec3(margin)
    };
    static thread_local engine::SpatialGrid::Candidates candidates;
    static thread_local std::vector<uint8_t> rayHit;
    static thread_local std::vector<float> rayTHit;
    entityManager->getSpatialGrid().query(rayAABB, candidates, 0.0f);
    filterCandidatesByRay(candidates, rayOrigin, rayDir, maxDistance, rayHit, rayTHit);

    Collision closest{};
    float closestDistSq = std::numeric_limits<float>::max();
    const size_t n = candidates.size();
    for (size_t i = 0; i < n; ++i) {
        if (!rayHit[i]) continue;
        engine::Collider::Collision collision = testRayCollision(candidates.colliders[i], rayAABB, rayOrigin, rayDir, maxDistance, ignoreCollider);
        if (!collision.other) continue;
        const glm::vec3 d = collision.worldHitPoint - rayOrigin;
        const float distSq = glm::dot(d, d);
        if (distSq < closestDistSq) {
            closest = collision;
            closestDistSq = distSq;
        }
    }
    return closest;
}

bool engine::Collider::raycastAny(EntityManager* entityManager, const glm::vec3& rayOrigin, const glm::vec3& rayDir, float maxDistance, Collider* ignoreCollider, float margin) {
    glm::vec3 rayEnd = rayOrigin + rayDir * maxDistance;
    AABB rayAABB = {
        .min = glm::min(rayOrigin, rayEnd) - glm::vec3(margin),
        .max = glm::max(rayOrigin, rayEnd) + glm::vec3(margin)
    };
    static thread_local engine::SpatialGrid::Candidates candidates;
    static thread_local std::vector<uint8_t> rayHit;
    static thread_local std::vector<float> rayTHit;
    entityManager->getSpatialGrid().query(rayAABB, candidates, 0.0f);
    filterCandidatesByRay(candidates, rayOrigin, rayDir, maxDistance, rayHit, rayTHit);

    const size_t n = candidates.size();
    for (size_t i = 0; i < n; ++i) {
        if (!rayHit[i]) continue;
        engine::Collider::Collision collision = testRayCollision(candidates.colliders[i], rayAABB, rayOrigin, rayDir, maxDistance, ignoreCollider);
        if (collision.other) return true;
    }
    return false;
}

void engine::Collider::raycast(EntityManager* entityManager, std::vector<engine::Collider::Collision>& outColliders, const glm::vec3& rayOrigin, const glm::vec3& rayDir, float maxDistance, Collider* ignoreCollider, float margin) {
    glm::vec3 rayEnd = rayOrigin + rayDir * maxDistance;
    AABB rayAABB = {
        .min = glm::min(rayOrigin, rayEnd) - glm::vec3(margin),
        .max = glm::max(rayOrigin, rayEnd) + glm::vec3(margin)
    };
    static thread_local engine::SpatialGrid::Candidates candidates;
    static thread_local std::vector<uint8_t> rayHit;
    static thread_local std::vector<float> rayTHit;
    entityManager->getSpatialGrid().query(rayAABB, candidates, 0.0f);
    filterCandidatesByRay(candidates, rayOrigin, rayDir, maxDistance, rayHit, rayTHit);

    const size_t n = candidates.size();
    for (size_t i = 0; i < n; ++i) {
        if (!rayHit[i]) continue;
        engine::Collider::Collision collision = testRayCollision(candidates.colliders[i], rayAABB, rayOrigin, rayDir, maxDistance, ignoreCollider);
        if (collision.other) {
            outColliders.push_back(collision);
        }
    }
}

size_t engine::Collider::raycast(EntityManager* entityManager, const glm::vec3& rayOrigin, const glm::vec3& rayDir, float maxDistance, Collider* ignoreCollider, float margin) {
    size_t hitCount = 0;
    glm::vec3 rayEnd = rayOrigin + rayDir * maxDistance;
    AABB rayAABB = {
        .min = glm::min(rayOrigin, rayEnd) - glm::vec3(margin),
        .max = glm::max(rayOrigin, rayEnd) + glm::vec3(margin)
    };
    static thread_local engine::SpatialGrid::Candidates candidates;
    static thread_local std::vector<uint8_t> rayHit;
    static thread_local std::vector<float> rayTHit;
    entityManager->getSpatialGrid().query(rayAABB, candidates, 0.0f);
    filterCandidatesByRay(candidates, rayOrigin, rayDir, maxDistance, rayHit, rayTHit);

    const size_t n = candidates.size();
    for (size_t i = 0; i < n; ++i) {
        if (!rayHit[i]) continue;
        engine::Collider::Collision collision = testRayCollision(candidates.colliders[i], rayAABB, rayOrigin, rayDir, maxDistance, ignoreCollider);
        if (collision.other) {
            hitCount++;
        }
    }
    return hitCount;
}

std::array<glm::vec3, 8> engine::Collider::buildOBBCorners(const glm::mat4& transform, const glm::vec3& half) {
    std::array<glm::vec3, 8> corners = {
        glm::vec3(transform * glm::vec4(-half.x, -half.y, -half.z, 1.0f)),
        glm::vec3(transform * glm::vec4( half.x, -half.y, -half.z, 1.0f)),
        glm::vec3(transform * glm::vec4( half.x,  half.y, -half.z, 1.0f)),
        glm::vec3(transform * glm::vec4(-half.x,  half.y, -half.z, 1.0f)),
        glm::vec3(transform * glm::vec4(-half.x, -half.y,  half.z, 1.0f)),
        glm::vec3(transform * glm::vec4( half.x, -half.y,  half.z, 1.0f)),
        glm::vec3(transform * glm::vec4( half.x,  half.y,  half.z, 1.0f)),
        glm::vec3(transform * glm::vec4(-half.x,  half.y,  half.z, 1.0f))
    };
    return corners;
}

engine::AABB engine::Collider::aabbFromCorners(const std::array<glm::vec3, 8>& corners) {
    glm::vec3 min = corners[0];
    glm::vec3 max = corners[0];
    for (const auto& corner : corners) {
        min = glm::min(min, corner);
        max = glm::max(max, corner);
    }
    return AABB{min, max};
}

std::pair<float, float> engine::Collider::projectOntoAxis(const std::array<glm::vec3, 8>& corners, const glm::vec3& axis) {
    float min = glm::dot(corners[0], axis);
    float max = min;
    for (size_t i = 1; i < corners.size(); ++i) {
        float projection = glm::dot(corners[i], axis);
        min = glm::min(min, projection);
        max = glm::max(max, projection);
    }
    return { min, max };
}

std::array<glm::vec3, 8> engine::Collider::getCornersFromAABB(const AABB& aabb) {
    std::array<glm::vec3, 8> corners = {
        glm::vec3(aabb.min.x, aabb.min.y, aabb.min.z),
        glm::vec3(aabb.max.x, aabb.min.y, aabb.min.z),
        glm::vec3(aabb.max.x, aabb.max.y, aabb.min.z),
        glm::vec3(aabb.min.x, aabb.max.y, aabb.min.z),
        glm::vec3(aabb.min.x, aabb.min.y, aabb.max.z),
        glm::vec3(aabb.max.x, aabb.min.y, aabb.max.z),
        glm::vec3(aabb.max.x, aabb.max.y, aabb.max.z),
        glm::vec3(aabb.min.x, aabb.max.y, aabb.max.z)
    };
    return corners;
}

bool engine::Collider::aabbOverlapMTV(const AABB& a, const AABB& b, CollisionMTV& out) {
    glm::vec3 aCenter = 0.5f * (a.min + a.max);
    glm::vec3 bCenter = 0.5f * (b.min + b.max);
    glm::vec3 aHalf = 0.5f * (a.max - a.min);
    glm::vec3 bHalf = 0.5f * (b.max - b.min);
    glm::vec3 delta = bCenter - aCenter;
    glm::vec3 overlap = aHalf + bHalf - glm::abs(delta);
    if (overlap.x > 0 && overlap.y > 0 && overlap.z > 0) {
        if (overlap.x < overlap.y && overlap.x < overlap.z) {
            out.penetrationDepth = overlap.x;
            out.normal = glm::vec3((delta.x < 0) ? -1.0f : 1.0f, 0.0f, 0.0f);
        } else if (overlap.y < overlap.x && overlap.y < overlap.z) {
            out.penetrationDepth = overlap.y;
            out.normal = glm::vec3(0.0f, (delta.y < 0) ? -1.0f : 1.0f, 0.0f);
        } else {
            out.penetrationDepth = overlap.z;
            out.normal = glm::vec3(0.0f, 0.0f, (delta.z < 0) ? -1.0f : 1.0f);
        }
        out.mtv = out.normal * out.penetrationDepth;
        return true;
    }
    return false;
}

bool engine::Collider::aabbIntersects(const AABB& a, const AABB& b, float margin) {
    return (a.min.x - margin <= b.max.x && a.max.x + margin >= b.min.x) &&
           (a.min.y - margin <= b.max.y && a.max.y + margin >= b.min.y) &&
           (a.min.z - margin <= b.max.z && a.max.z + margin >= b.min.z);
}

glm::vec3 engine::Collider::normalizeOrZero(const glm::vec3& v) {
    float length = glm::length(v);
    if (length > 1e-6f) {
        return v / length;
    }
    return glm::vec3(0.0f);
}

void engine::Collider::addAxisUnique(std::vector<glm::vec3>& axes, const glm::vec3& axis) {
    glm::vec3 normAxis = normalizeOrZero(axis);
    if (glm::length(normAxis) < 1e-6f) return;
    for (const auto& a : axes) {
        if (std::abs(glm::dot(a, normAxis)) > 0.999f) {
            return; // axis already exists
        }
    }
    axes.push_back(normAxis);
}

std::pair<float, float> engine::Collider::projectVertsOntoAxis(std::span<const glm::vec3> verts, const glm::vec3& axis, const glm::vec3& offset) {
    if (verts.empty()) {
        return { 0.0f, 0.0f };
    }
    float min = glm::dot(verts[0] + offset, axis);
    float max = min;
    float offsetProjection = glm::dot(offset, axis);
    for (const auto& vert : verts) {
        float projection = glm::dot(vert, axis) + offsetProjection;
        min = glm::min(min, projection);
        max = glm::max(max, projection);
    }
    return { min, max };
}

bool engine::Collider::satMTV(std::span<const glm::vec3> vertsA, std::span<const glm::vec3> vertsB, std::span<const glm::vec3> edgesA, std::span<const glm::vec3> edgesB, std::span<const glm::vec3> axesA, std::span<const glm::vec3> axesB, CollisionMTV& out, const glm::vec3 centerDelta, const glm::vec3& offsetA, const glm::vec3& offsetB, std::span<const glm::vec2> aSelfProjOnAxesA, std::span<const glm::vec2> bSelfProjOnAxesB, ColliderVertSoA aSoa, ColliderVertSoA bSoa) {
    static thread_local std::vector<glm::vec3> axes;
    axes.clear();
    axes.reserve(axesA.size() + axesB.size() + 36);
    const size_t naxA = axesA.size();
    for (const auto& axis : axesA) {
        axes.push_back(axis);
    }
    const size_t naxB = axesB.size();
    for (const auto& axis : axesB) {
        axes.push_back(axis);
    }
    const size_t maxEdgesPerShape = 6;
    size_t edgeCountA = std::min(edgesA.size(), maxEdgesPerShape);
    size_t edgeCountB = std::min(edgesB.size(), maxEdgesPerShape);
    for (size_t i = 0; i < edgeCountA; ++i) {
        for (size_t j = 0; j < edgeCountB; ++j) {
            addAxisUnique(axes, glm::cross(edgesA[i], edgesB[j]));
        }
    }
    float minPenetration = std::numeric_limits<float>::max();
    glm::vec3 bestAxis(0.0f);

    const bool haveACache = aSelfProjOnAxesA.size() == naxA;
    const bool haveBCache = bSelfProjOnAxesB.size() == naxB;

    auto projectA = [&](size_t i, const glm::vec3& axis, float& aMin, float& aMax) {
        if (haveACache && i < naxA) {
            float shift = glm::dot(offsetA, axis);
            aMin = aSelfProjOnAxesA[i].x + shift;
            aMax = aSelfProjOnAxesA[i].y + shift;
        } else if (aSoa.x && aSoa.paddedCount >= engine::simd::kPad) {
            const float offProj = glm::dot(offsetA, axis);
            auto r = engine::simd::projectVertsSoA(
                aSoa.x, aSoa.y, aSoa.z, aSoa.paddedCount,
                axis.x, axis.y, axis.z, offProj
            );
            aMin = r.min;
            aMax = r.max;
        } else {
            std::tie(aMin, aMax) = projectVertsOntoAxis(vertsA, axis, offsetA);
        }
    };
    auto projectB = [&](size_t i, const glm::vec3& axis, float& bMin, float& bMax) {
        if (haveBCache && i >= naxA && i < naxA + naxB) {
            float shift = glm::dot(offsetB, axis);
            bMin = bSelfProjOnAxesB[i - naxA].x + shift;
            bMax = bSelfProjOnAxesB[i - naxA].y + shift;
        } else if (bSoa.x && bSoa.paddedCount >= engine::simd::kPad) {
            const float offProj = glm::dot(offsetB, axis);
            auto r = engine::simd::projectVertsSoA(
                bSoa.x, bSoa.y, bSoa.z, bSoa.paddedCount,
                axis.x, axis.y, axis.z, offProj
            );
            bMin = r.min;
            bMax = r.max;
        } else {
            std::tie(bMin, bMax) = projectVertsOntoAxis(vertsB, axis, offsetB);
        }
    };

    for (size_t i = 0; i < axes.size(); ++i) {
        const glm::vec3& axis = axes[i];
        float aMin, aMax, bMin, bMax;
        projectA(i, axis, aMin, aMax);
        projectB(i, axis, bMin, bMax);
        float overlap = glm::min(aMax, bMax) - glm::max(aMin, bMin);
        if (overlap <= 1e-6f) {
            return false;
        }
        if (overlap < minPenetration) {
            minPenetration = overlap;
            bestAxis = axis;
        }
    }
    if (minPenetration <= 1e-6f) {
        return false;
    }
    if (glm::dot(bestAxis, centerDelta) < 0.0f) {
        bestAxis = -bestAxis;
    }
    out.normal = bestAxis;
    out.penetrationDepth = minPenetration;
    out.mtv = out.normal * out.penetrationDepth;
    return true;
}

void engine::ConvexHullCollider::buildConvexData(const std::vector<glm::vec3>& verts, const std::vector<glm::ivec3>& tris, const glm::mat4& transform, std::vector<glm::vec3>& outVerts, std::vector<glm::vec3>& outEdgeAxes, std::vector<glm::vec3>& outFaceAxes, glm::vec3& outCenter) {
    outVerts.clear();
    outEdgeAxes.clear();
    outFaceAxes.clear();
    outCenter = glm::vec3(0.0f);
    outVerts.resize(verts.size());
    const size_t vcount = verts.size();
    if (vcount > 128) {
        ThreadPool::global().parallel_for_chunks(0, vcount, 64, [&](size_t b, size_t e, size_t) {
            for (size_t i = b; i < e; ++i) {
                outVerts[i] = glm::vec3(transform * glm::vec4(verts[i], 1.0f));
            }
        });
    } else {
        for (size_t i = 0; i < vcount; ++i) {
            outVerts[i] = glm::vec3(transform * glm::vec4(verts[i], 1.0f));
        }
    }
    if (outVerts.empty()) {
        return;
    }
    for (const auto& tri : tris) {
        if (static_cast<size_t>(tri.x) >= outVerts.size() ||
            static_cast<size_t>(tri.y) >= outVerts.size() ||
            static_cast<size_t>(tri.z) >= outVerts.size()) {
            continue;
        }
        glm::vec3 a = outVerts[static_cast<size_t>(tri.x)];
        glm::vec3 b = outVerts[static_cast<size_t>(tri.y)];
        glm::vec3 c = outVerts[static_cast<size_t>(tri.z)];
        glm::vec3 normal = glm::cross(b - a, c - a);
        float len = glm::length(normal);
        if (len > 1e-6f) {
            addAxisUnique(outFaceAxes, normal / len);
        }
        if (outEdgeAxes.size() < 12) {
            addAxisUnique(outEdgeAxes, normalizeOrZero(b - a));
            addAxisUnique(outEdgeAxes, normalizeOrZero(c - b));
            addAxisUnique(outEdgeAxes, normalizeOrZero(a - c));
        }
    }
    if (outFaceAxes.empty()) {
        outFaceAxes = {
            glm::vec3(1.0f, 0.0f, 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f),
            glm::vec3(0.0f, 0.0f, 1.0f)
        };
    }
    if (outEdgeAxes.empty()) {
        outEdgeAxes = outFaceAxes;
    }
    float centerX = 0.0f, centerY = 0.0f, centerZ = 0.0f;
    for (const auto& vert : outVerts) {
        centerX += vert.x; centerY += vert.y; centerZ += vert.z;
    }
    outCenter = glm::vec3(centerX, centerY, centerZ) / static_cast<float>(outVerts.size());
}

engine::AABB engine::AABBCollider::getWorldAABB() {
    const glm::mat4& transform = getWorldTransform();
    auto corners = Collider::buildOBBCorners(transform, halfSize);
    return Collider::aabbFromCorners(corners);
}

engine::AABB engine::OBBCollider::getWorldAABB() {
    ensureCached();
    return Collider::aabbFromCorners(cornersCache);
}

void engine::OBBCollider::ensureCached() {
    uint32_t currentGen = getTransformGeneration();
    if (isCached && currentGen == lastTransformGeneration) {
        return;
    }
    const glm::mat4& currentTransform = getWorldTransform();
    cornersCache = Collider::buildOBBCorners(currentTransform, halfSize);
    axesCache = {
        Collider::normalizeOrZero(glm::vec3(currentTransform[0])),
        Collider::normalizeOrZero(glm::vec3(currentTransform[1])),
        Collider::normalizeOrZero(glm::vec3(currentTransform[2]))
    };
    centerCache = glm::vec3(currentTransform[3]);
    lastTransformGeneration = currentGen;
    isCached = true;
}

engine::AABB engine::ConvexHullCollider::getWorldAABB() {
    ensureCached();
    return cachedAABB;
}

void engine::ConvexHullCollider::setVertsFromModel(std::vector<glm::vec3>&& vertices, std::vector<uint32_t>&& indices, const glm::mat4& transform) {
    localVerts.clear();
    localTris.clear();
    localVerts.reserve(vertices.size());
    for (const auto& v : vertices) {
        localVerts.emplace_back(glm::vec3(transform * glm::vec4(v, 1.0f)));
    }
    const size_t expectedTriCount = indices.size() / 3;
    localTris.reserve(expectedTriCount);
    for (size_t i = 0; i < expectedTriCount; ++i) {
        glm::ivec3 tri(
            static_cast<int>(indices[i * 3 + 0]),
            static_cast<int>(indices[i * 3 + 1]),
            static_cast<int>(indices[i * 3 + 2])
        );
        if (static_cast<size_t>(tri.x) >= localVerts.size() ||
            static_cast<size_t>(tri.y) >= localVerts.size() ||
            static_cast<size_t>(tri.z) >= localVerts.size()) {
            continue;
        }
        localTris.emplace_back(tri);
    }
    isCached = false;
}

void engine::ConvexHullCollider::setVertsFromModel(const std::vector<glm::vec3>& vertices, const std::vector<uint32_t>& indices, const glm::mat4& transform) {
    localVerts.clear();
    localTris.clear();
    localVerts.reserve(vertices.size());
    for (const auto& v : vertices) {
        localVerts.emplace_back(glm::vec3(transform * glm::vec4(v, 1.0f)));
    }
    const size_t expectedTriCount = indices.size() / 3;
    localTris.reserve(expectedTriCount);
    for (size_t i = 0; i < expectedTriCount; ++i) {
        glm::ivec3 tri(
            static_cast<int>(indices[i * 3 + 0]),
            static_cast<int>(indices[i * 3 + 1]),
            static_cast<int>(indices[i * 3 + 2])
        );
        if (static_cast<size_t>(tri.x) >= localVerts.size() ||
            static_cast<size_t>(tri.y) >= localVerts.size() ||
            static_cast<size_t>(tri.z) >= localVerts.size()) {
            continue;
        }
        localTris.emplace_back(tri);
    }
    isCached = false;
}

bool engine::AABBCollider::intersectsMTV(Collider& other, CollisionMTV& out, const glm::mat4& deltaTransform) {
    glm::mat4 transform = getWorldTransform();
    transform[3] += glm::vec4(glm::vec3(deltaTransform[3]), 0.0f); // world-space translation
    auto cornersA = Collider::buildOBBCorners(transform, halfSize);
    AABB thisAABB = Collider::aabbFromCorners(cornersA);
    AABB otherAABB = other.getWorldAABB();
    if (other.getColliderType() == ColliderType::AABB) {
        return Collider::aabbOverlapMTV(thisAABB, otherAABB, out);
    }
    if (!Collider::aabbIntersects(thisAABB, otherAABB, 0.001f)) {
        return false;
    }
    std::array<glm::vec3, 3> faceAxesA = {
        Collider::normalizeOrZero(glm::vec3(transform[0])),
        Collider::normalizeOrZero(glm::vec3(transform[1])),
        Collider::normalizeOrZero(glm::vec3(transform[2]))
    };
    glm::vec3 centerA = glm::vec3(transform[3]);
    std::array<glm::vec3, 8> cornersB;
    std::array<glm::vec3, 3> axesBArr;
    static const std::array<glm::vec3, 3> cardinalAxes = {
        glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)
    };
    std::span<const glm::vec3> vertsB;
    std::span<const glm::vec3> faceAxesB;
    std::span<const glm::vec3> edgeAxesB;
    std::span<const glm::vec2> bSelfProj;
    ColliderVertSoA bSoa{};
    glm::vec3 centerB(0.0f);
    switch (other.getColliderType()) {
        case ColliderType::OBB: {
            const glm::mat4& otherTransform = other.getWorldTransform();
            cornersB = Collider::buildOBBCorners(otherTransform, static_cast<const engine::OBBCollider&>(other).getHalfSize());
            axesBArr = {
                Collider::normalizeOrZero(glm::vec3(otherTransform[0])),
                Collider::normalizeOrZero(glm::vec3(otherTransform[1])),
                Collider::normalizeOrZero(glm::vec3(otherTransform[2]))
            };
            vertsB = cornersB;
            faceAxesB = axesBArr;
            edgeAxesB = axesBArr;
            centerB = glm::vec3(otherTransform[3]);
            break;
        }
        case ColliderType::ConvexHull: {
            const auto& cvx = static_cast<const ConvexHullCollider&>(other);
            const auto& oVerts = cvx.getWorldVerts();
            if (!oVerts.empty()) {
                vertsB = oVerts;
                faceAxesB = cvx.getFaceAxesCached();
                edgeAxesB = cvx.getEdgeAxesCached();
                bSelfProj = cvx.getFaceAxisSelfProjCached();
                bSoa = { cvx.getWorldVertsX().data(), cvx.getWorldVertsY().data(),
                         cvx.getWorldVertsZ().data(), cvx.getWorldVertsX().size() };
                centerB = cvx.getWorldCenter();
            } else {
                cornersB = getCornersFromAABB(otherAABB);
                vertsB = cornersB;
                faceAxesB = cardinalAxes;
                edgeAxesB = cardinalAxes;
                centerB = 0.5f * (otherAABB.min + otherAABB.max);
            }
            break;
        }
        default:
            return false;
    }
    return Collider::satMTV(cornersA, vertsB, faceAxesA, edgeAxesB, faceAxesA, faceAxesB, out, centerA - centerB, glm::vec3(0.0f), glm::vec3(0.0f), {}, bSelfProj, {}, bSoa);
}

bool engine::OBBCollider::intersectsMTV(Collider& other, CollisionMTV& out, const glm::mat4& deltaTransform) {
    bool hasDelta = glm::length(glm::vec3(deltaTransform[3])) > 1e-6f;
    std::array<glm::vec3, 8> cornersAStorage;
    std::array<glm::vec3, 3> axesAStorage;
    const std::array<glm::vec3, 8>* cornersAPtr;
    const std::array<glm::vec3, 3>* axesAPtr;
    glm::vec3 centerA;

    if (hasDelta) {
        glm::mat4 transform = getWorldTransform();
        transform[3] += glm::vec4(glm::vec3(deltaTransform[3]), 0.0f); // world-space translation
        cornersAStorage = Collider::buildOBBCorners(transform, halfSize);
        axesAStorage = {
            Collider::normalizeOrZero(glm::vec3(transform[0])),
            Collider::normalizeOrZero(glm::vec3(transform[1])),
            Collider::normalizeOrZero(glm::vec3(transform[2]))
        };
        cornersAPtr = &cornersAStorage;
        axesAPtr = &axesAStorage;
        centerA = glm::vec3(transform[3]);
    } else {
        ensureCached();
        cornersAPtr = &cornersCache;
        axesAPtr = &axesCache;
        centerA = centerCache;
    }

    AABB movedAABB = Collider::aabbFromCorners(*cornersAPtr);
    AABB otherAABB = other.getWorldAABB();
    if (!Collider::aabbIntersects(movedAABB, otherAABB, 0.001f)) {
        return false;
    }

    std::array<glm::vec3, 8> cornersB;
    std::array<glm::vec3, 3> axesBArr;
    static const std::array<glm::vec3, 3> cardinalAxes = {
        glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)
    };
    std::span<const glm::vec3> vertsB;
    std::span<const glm::vec3> faceAxesB;
    std::span<const glm::vec3> edgeAxesB;
    std::span<const glm::vec2> bSelfProj;
    ColliderVertSoA bSoa{};
    glm::vec3 centerB(0.0f);

    switch (other.getColliderType()) {
        case ColliderType::AABB: {
            cornersB = getCornersFromAABB(otherAABB);
            vertsB = cornersB;
            faceAxesB = cardinalAxes;
            edgeAxesB = cardinalAxes;
            centerB = 0.5f * (otherAABB.min + otherAABB.max);
            break;
        }
        case ColliderType::OBB: {
            OBBCollider& otherOBB = static_cast<OBBCollider&>(other);
            otherOBB.ensureCached();
            vertsB = otherOBB.getCornersCache();
            faceAxesB = otherOBB.getAxesCache();
            edgeAxesB = otherOBB.getAxesCache();
            centerB = otherOBB.getCenterCache();
            break;
        }
        case ColliderType::ConvexHull: {
            const auto& cvx = static_cast<const ConvexHullCollider&>(other);
            const auto& oVerts = cvx.getWorldVerts();
            if (!oVerts.empty()) {
                vertsB = oVerts;
                faceAxesB = cvx.getFaceAxesCached();
                edgeAxesB = cvx.getEdgeAxesCached();
                bSelfProj = cvx.getFaceAxisSelfProjCached();
                bSoa = { cvx.getWorldVertsX().data(), cvx.getWorldVertsY().data(),
                         cvx.getWorldVertsZ().data(), cvx.getWorldVertsX().size() };
                centerB = cvx.getWorldCenter();
            } else {
                cornersB = getCornersFromAABB(otherAABB);
                vertsB = cornersB;
                faceAxesB = cardinalAxes;
                edgeAxesB = cardinalAxes;
                centerB = 0.5f * (otherAABB.min + otherAABB.max);
            }
            break;
        }
    }
    return Collider::satMTV(*cornersAPtr, vertsB, *axesAPtr, edgeAxesB, *axesAPtr, faceAxesB, out, centerA - centerB, glm::vec3(0.0f), glm::vec3(0.0f), {}, bSelfProj, {}, bSoa);
}

bool engine::ConvexHullCollider::intersectsMTV(Collider& other, CollisionMTV& out, const glm::mat4& deltaTransform) {
    AABB otherAABB = other.getWorldAABB();
    AABB thisAABB = getWorldAABB();
    if (!Collider::aabbIntersects(thisAABB, otherAABB, 0.001f)) {
        return false;
    }
    ensureCached();
    glm::vec3 centerA = worldCenter + glm::vec3(deltaTransform[3]);
    std::array<glm::vec3, 8> cornersB;
    std::array<glm::vec3, 3> axesBArr;
    static const std::array<glm::vec3, 3> cardinalAxes = {
        glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)
    };
    std::span<const glm::vec3> vertsB;
    std::span<const glm::vec3> faceAxesB;
    std::span<const glm::vec3> edgeAxesB;
    std::span<const glm::vec2> bSelfProj;
    ColliderVertSoA bSoa{};
    glm::vec3 centerB(0.0f);
    const glm::mat4& otherTransform = other.getWorldTransform();
    switch (other.getColliderType()) {
        case ColliderType::AABB: {
            cornersB = getCornersFromAABB(otherAABB);
            vertsB = cornersB;
            faceAxesB = cardinalAxes;
            edgeAxesB = cardinalAxes;
            break;
        }
        case ColliderType::OBB: {
            cornersB = Collider::buildOBBCorners(otherTransform, static_cast<const engine::OBBCollider&>(other).getHalfSize());
            axesBArr = {
                Collider::normalizeOrZero(glm::vec3(otherTransform[0])),
                Collider::normalizeOrZero(glm::vec3(otherTransform[1])),
                Collider::normalizeOrZero(glm::vec3(otherTransform[2]))
            };
            vertsB = cornersB;
            faceAxesB = axesBArr;
            edgeAxesB = axesBArr;
            break;
        }
        case ColliderType::ConvexHull: {
            const auto& cvx = static_cast<const ConvexHullCollider&>(other);
            const auto& oVerts = cvx.worldVerts;
            if (!oVerts.empty()) {
                vertsB = oVerts;
                faceAxesB = cvx.faceAxesCached;
                edgeAxesB = cvx.edgeAxesCached;
                bSelfProj = cvx.faceAxisSelfProjCached;
                bSoa = { cvx.worldVertsX.data(), cvx.worldVertsY.data(),
                         cvx.worldVertsZ.data(), cvx.worldVertsX.size() };
                centerB = cvx.worldCenter;
            } else {
                cornersB = getCornersFromAABB(otherAABB);
                vertsB = cornersB;
                faceAxesB = cardinalAxes;
                edgeAxesB = cardinalAxes;
            }
            break;
        }
    }
    ColliderVertSoA aSoa{ worldVertsX.data(), worldVertsY.data(),
                          worldVertsZ.data(), worldVertsX.size() };
    return Collider::satMTV(worldVerts, vertsB, edgeAxesCached, edgeAxesB, faceAxesCached, faceAxesB, out, centerA - centerB, glm::vec3(deltaTransform[3]), glm::vec3(otherTransform[3]), faceAxisSelfProjCached, bSelfProj, aSoa, bSoa);
}

void engine::ConvexHullCollider::ensureCached() {
    uint32_t currentGen = getTransformGeneration();
    if (isCached && currentGen == lastTransformGeneration) {
        return;
    }
    const glm::mat4& worldTransform = getWorldTransform();
    buildConvexData(localVerts, localTris, worldTransform, worldVerts, edgeAxesCached, faceAxesCached, worldCenter);

    if (worldVerts.empty()) {
        glm::vec3 p = glm::vec3(worldTransform[3]);
        cachedAABB = AABB{p - glm::vec3(0.001f), p + glm::vec3(0.001f)};
        faceAxisSelfProjCached.clear();
        worldVertsX.clear();
        worldVertsY.clear();
        worldVertsZ.clear();
    } else {
        glm::vec3 minW(std::numeric_limits<float>::max());
        glm::vec3 maxW(std::numeric_limits<float>::lowest());
        for (const auto& w : worldVerts) {
            minW = glm::min(minW, w);
            maxW = glm::max(maxW, w);
        }
        cachedAABB = AABB{minW, maxW};

        faceAxisSelfProjCached.resize(faceAxesCached.size());
        for (size_t a = 0; a < faceAxesCached.size(); ++a) {
            const glm::vec3& axis = faceAxesCached[a];
            float pMin = glm::dot(worldVerts[0], axis);
            float pMax = pMin;
            for (size_t v = 1; v < worldVerts.size(); ++v) {
                float p = glm::dot(worldVerts[v], axis);
                if (p < pMin) pMin = p;
                if (p > pMax) pMax = p;
            }
            faceAxisSelfProjCached[a] = glm::vec2(pMin, pMax);
        }

        const size_t nReal = worldVerts.size();
        const size_t nPad = ((nReal + kSoAPad - 1) / kSoAPad) * kSoAPad;
        worldVertsX.resize(nPad);
        worldVertsY.resize(nPad);
        worldVertsZ.resize(nPad);
        for (size_t i = 0; i < nReal; ++i) {
            worldVertsX[i] = worldVerts[i].x;
            worldVertsY[i] = worldVerts[i].y;
            worldVertsZ[i] = worldVerts[i].z;
        }
        const float padX = worldVerts[0].x;
        const float padY = worldVerts[0].y;
        const float padZ = worldVerts[0].z;
        for (size_t i = nReal; i < nPad; ++i) {
            worldVertsX[i] = padX;
            worldVertsY[i] = padY;
            worldVertsZ[i] = padZ;
        }
    }

    lastTransformGeneration = currentGen;
    isCached = true;
}