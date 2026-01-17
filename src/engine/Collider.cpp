#include <engine/Collider.h>
#include <engine/SpatialGrid.h>

engine::Collider::Collider(EntityManager* entityManager, const std::string& name, glm::mat4 transform)
    : Entity(entityManager, name, "", transform, {}, false) {
        entityManager->addCollider(this);
    }

engine::Collider::~Collider() {
    if (getEntityManager()) {
        getEntityManager()->removeCollider(this);
    }
}

std::vector<engine::Collider::Collision> engine::Collider::raycast(EntityManager* entityManager, const glm::vec3& rayOrigin, const glm::vec3& rayDir, float maxDistance, Collider* ignoreCollider, bool returnFirstHit) {
    std::vector<Collision> results;
    glm::vec3 rayEnd = rayOrigin + rayDir * maxDistance;
    AABB rayAABB = {
        glm::min(rayOrigin, rayEnd),
        glm::max(rayOrigin, rayEnd)
    };
    
    const std::vector<Collider*>& candidates = entityManager->getColliders();
    
    for (Collider* collider : candidates) {
        if (collider == ignoreCollider) {
            continue;
        }
        AABB aabb = collider->getWorldAABB();
        if (!aabbIntersects(rayAABB, aabb)) {
            continue;
        }
        ColliderType type = collider->getType();
        switch (type) {
            case ColliderType::AABB: {
                AABBCollider* aabbCollider = static_cast<AABBCollider*>(collider);
                glm::vec3 invDir = 1.0f / rayDir;
                glm::vec3 t1 = (aabb.min - rayOrigin) * invDir;
                glm::vec3 t2 = (aabb.max - rayOrigin) * invDir;
                glm::vec3 tmin = glm::min(t1, t2);
                glm::vec3 tmax = glm::max(t1, t2);
                float tNear = glm::max(glm::max(tmin.x, tmin.y), tmin.z);
                float tFar = glm::min(glm::min(tmax.x, tmax.y), tmax.z);
                if (tNear <= tFar && tFar >= 0.0f && tNear <= maxDistance) {
                    float tHit = tNear >= 0.0f ? tNear : tFar;
                    glm::vec3 hitPoint = rayOrigin + rayDir * tHit;
                    glm::vec3 normal(0.0f);
                    if (tNear >= 0.0f) {
                        if (tmin.x == tNear) normal = glm::vec3(rayDir.x > 0 ? -1.0f : 1.0f, 0.0f, 0.0f);
                        else if (tmin.y == tNear) normal = glm::vec3(0.0f, rayDir.y > 0 ? -1.0f : 1.0f, 0.0f);
                        else normal = glm::vec3(0.0f, 0.0f, rayDir.z > 0 ? -1.0f : 1.0f);
                    }
                    Collision collision = {
                        .other = collider,
                        .mtv = { .normal = normal },
                        .worldHitPoint = hitPoint
                    };
                    results.push_back(collision);
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
                    results.push_back(collision);
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
                    results.push_back(collision);
                }
                break;
            }
        }
    }
    if (returnFirstHit && !results.empty()) {
        Collision closest = results[0];
        float closestDist = glm::length(closest.worldHitPoint - rayOrigin);
        for (const Collision& collision : results) {
            float dist = glm::length(collision.worldHitPoint - rayOrigin);
            if (dist < closestDist) {
                closest = collision;
                closestDist = dist;
            }
        }
        results.clear();
        results.push_back(closest);
    }
    return results;
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

std::pair<float, float> engine::Collider::projectVertsOntoAxis(const std::vector<glm::vec3>& verts, const glm::vec3& axis, const glm::vec3& offset) {
    if (verts.empty()) {
        return { 0.0f, 0.0f };
    }
#if defined(USE_OPENMP)
    float mnLocal = std::numeric_limits<float>::max();
    float mxLocal = std::numeric_limits<float>::lowest();
    #pragma omp parallel for reduction(min:mnLocal) reduction(max:mxLocal)
    for (int i = 0; i < static_cast<int>(verts.size()); ++i) {
        float projection = glm::dot(verts[i] + offset, axis);
        if (projection < mnLocal) mnLocal = projection;
        if (projection > mxLocal) mxLocal = projection;
    }
    return { mnLocal, mxLocal };
#else
    float min = glm::dot(verts[0] + offset, axis);
    float max = min;
    for (size_t i = 1; i < verts.size(); ++i) {
        float projection = glm::dot(verts[i] + offset, axis);
        min = glm::min(min, projection);
        max = glm::max(max, projection);
    }
    return { min, max };
#endif
}

bool engine::Collider::satMTV(const std::vector<glm::vec3>& vertsA, const std::vector<glm::vec3>& vertsB, const std::vector<glm::vec3>& edgesA, const std::vector<glm::vec3>& edgesB, const std::vector<glm::vec3>& axesA, const std::vector<glm::vec3>& axesB, CollisionMTV& out, const glm::vec3 centerDelta, const glm::vec3& offsetA, const glm::vec3& offsetB) {
    std::vector<glm::vec3> axes;
    axes.reserve(axesA.size() + axesB.size() + edgesA.size() * edgesB.size());
    for (const auto& axis : axesA) {
        addAxisUnique(axes, axis);
    }
    for (const auto& axis : axesB) {
        addAxisUnique(axes, axis);
    }
    for (const auto& edgeA : edgesA) {
        for (const auto& edgeB : edgesB) {
            addAxisUnique(axes, glm::cross(edgeA, edgeB));
        }
    }
    float minPenetration = std::numeric_limits<float>::max();
    glm::vec3 bestAxis(0.0f);
#if defined(USE_OPENMP)
    const int m = static_cast<int>(axes.size());
    if (m == 0) return false;
    std::vector<float> overlaps(static_cast<size_t>(m), 0.0f);
    #pragma omp parallel for
    for (int i = 0; i < m; ++i) {
        float aMin, aMax, bMin, bMax;
        const glm::vec3& axis = axes[static_cast<size_t>(i)];
        std::tie(aMin, aMax) = projectVertsOntoAxis(vertsA, axis, offsetA);
        std::tie(bMin, bMax) = projectVertsOntoAxis(vertsB, axis, offsetB);
        overlaps[static_cast<size_t>(i)] = glm::min(aMax, bMax) - glm::max(aMin, bMin);
    }
    for (size_t i = 0; i < static_cast<size_t>(m); ++i) {
        if (overlaps[i] <= 1e-6f) {
            return false;
        }
    }
    int bestIdx = -1;
    #pragma omp parallel
    {
        float localMin = std::numeric_limits<float>::max();
        int localIdx = -1;
        #pragma omp for nowait
        for (int i = 0; i < m; ++i) {
            float overlap = overlaps[static_cast<size_t>(i)];
            if (overlap < localMin) {
                localMin = overlap;
                localIdx = i;
            }
        }
        #pragma omp critical
        {
            if (localMin < minPenetration) {
                minPenetration = localMin;
                bestIdx = localIdx;
            }
        }
    }
    if (minPenetration <= 1e-6f || bestIdx == -1) {
        return false;
    }
    bestAxis = axes[static_cast<size_t>(bestIdx)];
#else
    int axisIdx = 0;
    for (const auto& axis : axes) {
        float aMin, aMax, bMin, bMax;
        std::tie(aMin, aMax) = projectVertsOntoAxis(vertsA, axis, offsetA);
        std::tie(bMin, bMax) = projectVertsOntoAxis(vertsB, axis, offsetB);
        float overlap = glm::min(aMax, bMax) - glm::max(aMin, bMin);
        if (overlap <= 0.0f) {
            return false;
        }
        if (overlap < minPenetration) {
            minPenetration = overlap;
            bestAxis = axis;
        }
        ++axisIdx;
    }
    if (minPenetration <= 1e-6f) {
        return false;
    }
#endif
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
#if defined(USE_OPENMP)
    #pragma omp parallel for
#endif
    for (int i = 0; i < static_cast<int>(verts.size()); ++i) {
        outVerts[i] = glm::vec3(transform * glm::vec4(verts[static_cast<size_t>(i)], 1.0f));
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
        addAxisUnique(outEdgeAxes, normalizeOrZero(b - a));
        addAxisUnique(outEdgeAxes, normalizeOrZero(c - b));
        addAxisUnique(outEdgeAxes, normalizeOrZero(a - c));
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
#if defined(USE_OPENMP)
    #pragma omp parallel for reduction(+:centerX, centerY, centerZ)
#endif
    for (int i = 0; i < static_cast<int>(outVerts.size()); ++i) {
        const auto& v = outVerts[static_cast<size_t>(i)];
        centerX += v.x; centerY += v.y; centerZ += v.z;
    }
    outCenter = glm::vec3(centerX, centerY, centerZ) / static_cast<float>(outVerts.size());
}

engine::AABB engine::AABBCollider::getWorldAABB() {
    glm::mat4 transform = getWorldTransform();
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
    glm::mat4 currentTransform = getWorldTransform();
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
    if (worldVerts.empty()) {
        glm::vec3 p = glm::vec3(const_cast<ConvexHullCollider*>(this)->getWorldTransform()[3]);
        return AABB{p - glm::vec3(0.001f), p + glm::vec3(0.001f)};
    }
    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float minZ = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float maxY = std::numeric_limits<float>::lowest();
    float maxZ = std::numeric_limits<float>::lowest();
#if defined(USE_OPENMP)
    #pragma omp parallel for reduction(min:minX, minY, minZ) reduction(max:maxX, maxY, maxZ)
    for (int i = 0; i < static_cast<int>(worldVerts.size()); ++i) {
        const glm::vec3& w = worldVerts[static_cast<size_t>(i)];
        if (w.x < minX) minX = w.x;
        if (w.y < minY) minY = w.y;
        if (w.z < minZ) minZ = w.z;
        if (w.x > maxX) maxX = w.x;
        if (w.y > maxY) maxY = w.y;
        if (w.z > maxZ) maxZ = w.z;
    }
#else
    for (const auto& w : worldVerts) {
        if (w.x < minX) minX = w.x;
        if (w.y < minY) minY = w.y;
        if (w.z < minZ) minZ = w.z;
        if (w.x > maxX) maxX = w.x;
        if (w.y > maxY) maxY = w.y;
        if (w.z > maxZ) maxZ = w.z;
    }
#endif
    return AABB{ glm::vec3(minX, minY, minZ), glm::vec3(maxX, maxY, maxZ) };
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
    glm::mat4 transform = getWorldTransform() * deltaTransform;
    auto cornersA = Collider::buildOBBCorners(transform, halfSize);
    AABB thisAABB = Collider::aabbFromCorners(cornersA);
    AABB otherAABB = other.getWorldAABB();
    if (other.getType() == ColliderType::AABB) {
        return Collider::aabbOverlapMTV(thisAABB, otherAABB, out);
    }
    if (!Collider::aabbIntersects(thisAABB, otherAABB, 0.001f)) {
        return false;
    }
    std::vector<glm::vec3> vertsA(cornersA.begin(), cornersA.end());
    std::vector<glm::vec3> faceAxesA = {
        Collider::normalizeOrZero(glm::vec3(transform[0])),
        Collider::normalizeOrZero(glm::vec3(transform[1])),
        Collider::normalizeOrZero(glm::vec3(transform[2]))
    };
    std::vector<glm::vec3> edgeAxesA = faceAxesA;
    glm::vec3 centerA = glm::vec3(transform[3]);
    glm::mat4 otherTransform = other.getWorldTransform();
    std::vector<glm::vec3> vertsB; std::vector<glm::vec3> faceAxesB; std::vector<glm::vec3> edgeAxesB; glm::vec3 centerB(0.0f);
    switch (other.getType()) {
        case ColliderType::OBB: {
            auto cornersB = Collider::buildOBBCorners(otherTransform, static_cast<const engine::OBBCollider&>(other).getHalfSize());
            vertsB.assign(cornersB.begin(), cornersB.end());
            faceAxesB = {
                Collider::normalizeOrZero(glm::vec3(otherTransform[0])),
                Collider::normalizeOrZero(glm::vec3(otherTransform[1])),
                Collider::normalizeOrZero(glm::vec3(otherTransform[2]))
            };
            edgeAxesB = faceAxesB;
            centerB = glm::vec3(otherTransform[3]);
            break;
        }
        case ColliderType::ConvexHull: {
            const auto& cvx = static_cast<const ConvexHullCollider&>(other);
            const auto& oVerts = const_cast<std::vector<glm::vec3>&>(cvx.getWorldVerts());
            if (!oVerts.empty()) {
                vertsB = oVerts;
                faceAxesB = cvx.getFaceAxesCached();
                edgeAxesB = cvx.getEdgeAxesCached();
                centerB = cvx.getWorldCenter();
            } else {
                auto corners = getCornersFromAABB(otherAABB);
                vertsB.assign(corners.begin(), corners.end());
                faceAxesB = {
                    glm::vec3(1.0f, 0.0f, 0.0f),
                    glm::vec3(0.0f, 1.0f, 0.0f),
                    glm::vec3(0.0f, 0.0f, 1.0f)
                };
                edgeAxesB = faceAxesB;
                centerB = 0.5f * (otherAABB.min + otherAABB.max);
            }
            break;
        }
        default:
            return false;
    }
    return Collider::satMTV(vertsA, vertsB, edgeAxesA, edgeAxesB, faceAxesA, faceAxesB, out, centerA - centerB);
}

bool engine::OBBCollider::intersectsMTV(Collider& other, CollisionMTV& out, const glm::mat4& deltaTransform) {
    bool hasDelta = glm::length(glm::vec3(deltaTransform[3])) > 1e-6f;
    std::array<glm::vec3, 8> cornersA;
    std::array<glm::vec3, 3> faceAxesALocal;
    glm::vec3 centerA;
    
    if (hasDelta) {
        glm::mat4 transform = getWorldTransform() * deltaTransform;
        cornersA = Collider::buildOBBCorners(transform, halfSize);
        faceAxesALocal = {
            Collider::normalizeOrZero(glm::vec3(transform[0])),
            Collider::normalizeOrZero(glm::vec3(transform[1])),
            Collider::normalizeOrZero(glm::vec3(transform[2]))
        };
        centerA = glm::vec3(transform[3]);
    } else {
        ensureCached();
        cornersA = cornersCache;
        faceAxesALocal = axesCache;
        centerA = centerCache;
    }
    
    AABB movedAABB = Collider::aabbFromCorners(cornersA);
    AABB otherAABB = other.getWorldAABB();
    if (!Collider::aabbIntersects(movedAABB, otherAABB, 0.001f)) {
        return false;
    }
    
    std::vector<glm::vec3> vertsA(cornersA.begin(), cornersA.end());
    std::vector<glm::vec3> faceAxesA(faceAxesALocal.begin(), faceAxesALocal.end());
    std::vector<glm::vec3> edgeAxesA = faceAxesA;
    
    std::vector<glm::vec3> vertsB; std::vector<glm::vec3> faceAxesB; std::vector<glm::vec3> edgeAxesB; glm::vec3 centerB(0.0f);
    
    switch (other.getType()) {
        case ColliderType::AABB: {
            auto corners = getCornersFromAABB(otherAABB);
            vertsB.assign(corners.begin(), corners.end());
            faceAxesB = {
                glm::vec3(1.0f, 0.0f, 0.0f),
                glm::vec3(0.0f, 1.0f, 0.0f),
                glm::vec3(0.0f, 0.0f, 1.0f)
            };
            edgeAxesB = faceAxesB;
            centerB = 0.5f * (otherAABB.min + otherAABB.max);
            break;
        }
        case ColliderType::OBB: {
            OBBCollider& otherOBB = static_cast<OBBCollider&>(other);
            otherOBB.ensureCached();
            const auto& cornersB = otherOBB.getCornersCache();
            const auto& otherAxes = otherOBB.getAxesCache();
            vertsB.assign(cornersB.begin(), cornersB.end());
            faceAxesB.assign(otherAxes.begin(), otherAxes.end());
            edgeAxesB = faceAxesB;
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
                centerB = cvx.getWorldCenter();
            } else {
                auto corners = getCornersFromAABB(otherAABB);
                vertsB.assign(corners.begin(), corners.end());
                faceAxesB = {
                    glm::vec3(1.0f, 0.0f, 0.0f),
                    glm::vec3(0.0f, 1.0f, 0.0f),
                    glm::vec3(0.0f, 0.0f, 1.0f)
                };
                edgeAxesB = faceAxesB;
                centerB = 0.5f * (otherAABB.min + otherAABB.max);
            }
            break;
        }
    }
    return Collider::satMTV(vertsA, vertsB, edgeAxesA, edgeAxesB, faceAxesA, faceAxesB, out, centerA - centerB);
}

bool engine::ConvexHullCollider::intersectsMTV(Collider& other, CollisionMTV& out, const glm::mat4& deltaTransform) {
    AABB otherAABB = other.getWorldAABB();
    AABB thisAABB = getWorldAABB();
    if (!Collider::aabbIntersects(thisAABB, otherAABB, 0.001f)) {
        return false;
    }
    ensureCached();
    const std::vector<glm::vec3>& vertsA = worldVerts;
    const std::vector<glm::vec3>& faceAxesA = faceAxesCached;
    const std::vector<glm::vec3>& edgeAxesA = edgeAxesCached;
    std::vector<glm::vec3> vertsB; std::vector<glm::vec3> faceAxesB; std::vector<glm::vec3> edgeAxesB; glm::vec3 centerB(0.0f);
    glm::vec3 centerA = worldCenter + glm::vec3(deltaTransform[3]);
    glm::mat4 otherTransform = other.getWorldTransform();
    switch (other.getType()) {
        case ColliderType::AABB: {
            auto corners = getCornersFromAABB(otherAABB);
            vertsB.assign(corners.begin(), corners.end());
            faceAxesB = {
                glm::vec3(1.0f, 0.0f, 0.0f),
                glm::vec3(0.0f, 1.0f, 0.0f),
                glm::vec3(0.0f, 0.0f, 1.0f)
            };
            edgeAxesB = faceAxesB;
            break;
        }
        case ColliderType::OBB: {
            auto cornersB = Collider::buildOBBCorners(otherTransform, static_cast<const engine::OBBCollider&>(other).getHalfSize());
            vertsB.assign(cornersB.begin(), cornersB.end());
            faceAxesB = {
                Collider::normalizeOrZero(glm::vec3(otherTransform[0])),
                Collider::normalizeOrZero(glm::vec3(otherTransform[1])),
                Collider::normalizeOrZero(glm::vec3(otherTransform[2]))
            };
            edgeAxesB = faceAxesB;
            break;
        }
        case ColliderType::ConvexHull: {
            const auto& cvx = static_cast<const ConvexHullCollider&>(other);
            const auto& oVerts = cvx.worldVerts;
            if (!oVerts.empty()) {
                vertsB = oVerts;
                faceAxesB = cvx.faceAxesCached;
                edgeAxesB = cvx.edgeAxesCached;
                centerB = cvx.worldCenter;
            } else {
                auto corners = getCornersFromAABB(otherAABB);
                vertsB.assign(corners.begin(), corners.end());
                faceAxesB = {
                    glm::vec3(1.0f, 0.0f, 0.0f),
                    glm::vec3(0.0f, 1.0f, 0.0f),
                    glm::vec3(0.0f, 0.0f, 1.0f)
                };
                edgeAxesB = faceAxesB;
            }
            break;
        }
    }
    return Collider::satMTV(vertsA, vertsB, edgeAxesA, edgeAxesB, faceAxesA, faceAxesB, out, centerA - centerB, deltaTransform[3], glm::vec3(otherTransform[3]));
}

void engine::ConvexHullCollider::ensureCached() {
    uint32_t currentGen = getTransformGeneration();
    if (isCached && currentGen == lastTransformGeneration) {
        return;
    }
    glm::mat4 worldTransform = getWorldTransform();
    buildConvexData(localVerts, localTris, worldTransform, worldVerts, edgeAxesCached, faceAxesCached, worldCenter);
    lastTransformGeneration = currentGen;
    isCached = true;
}