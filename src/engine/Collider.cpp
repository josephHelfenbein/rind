#include <engine/Collider.h>

std::array<glm::vec3, 8> engine::Collider::buildOBBCorners(const glm::mat4& transform, const glm::vec3& half) {
    std::array<glm::vec3, 8> corners;
    corners[0] = glm::vec3(transform * glm::vec4(-half.x, -half.y, -half.z, 1.0f));
    corners[1] = glm::vec3(transform * glm::vec4( half.x, -half.y, -half.z, 1.0f));
    corners[2] = glm::vec3(transform * glm::vec4( half.x,  half.y, -half.z, 1.0f));
    corners[3] = glm::vec3(transform * glm::vec4(-half.x,  half.y, -half.z, 1.0f));
    corners[4] = glm::vec3(transform * glm::vec4(-half.x, -half.y,  half.z, 1.0f));
    corners[5] = glm::vec3(transform * glm::vec4( half.x, -half.y,  half.z, 1.0f));
    corners[6] = glm::vec3(transform * glm::vec4( half.x,  half.y,  half.z, 1.0f));
    corners[7] = glm::vec3(transform * glm::vec4(-half.x,  half.y,  half.z, 1.0f));
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
    std::array<glm::vec3, 8> corners;
    corners[0] = glm::vec3(aabb.min.x, aabb.min.y, aabb.min.z);
    corners[1] = glm::vec3(aabb.max.x, aabb.min.y, aabb.min.z);
    corners[2] = glm::vec3(aabb.max.x, aabb.max.y, aabb.min.z);
    corners[3] = glm::vec3(aabb.min.x, aabb.max.y, aabb.min.z);
    corners[4] = glm::vec3(aabb.min.x, aabb.min.y, aabb.max.z);
    corners[5] = glm::vec3(aabb.max.x, aabb.min.y, aabb.max.z);
    corners[6] = glm::vec3(aabb.max.x, aabb.max.y, aabb.max.z);
    corners[7] = glm::vec3(aabb.min.x, aabb.max.y, aabb.max.z);
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
    glm::mat4 transform = getWorldTransform();
    auto corners = Collider::buildOBBCorners(transform, halfSize);
    return Collider::aabbFromCorners(corners);
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
    glm::mat4 transform = getWorldTransform() * deltaTransform;
    auto cornersA = Collider::buildOBBCorners(transform, halfSize);
    AABB movedAABB = Collider::aabbFromCorners(cornersA);
    AABB otherAABB = other.getWorldAABB();
    if (!Collider::aabbIntersects(movedAABB, otherAABB, 0.001f)) {
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
    glm::mat4 worldTransform = getWorldTransform();
    bool same = isCached;
    if (same) {
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                if (std::abs(worldTransform[c][r] - lastTransform[c][r]) > 1e-6f) {
                    same = false;
                    break;
                }
            }
            if (!same) break;
        }
    }
    if (same) {
        return;
    }
    buildConvexData(localVerts, localTris, worldTransform, worldVerts, edgeAxesCached, faceAxesCached, worldCenter);
    lastTransform = worldTransform;
    isCached = true;
}