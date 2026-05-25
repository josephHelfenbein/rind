#pragma once

#include <cstddef>
#include <cstdint>

namespace engine::simd {

    // convex-hull SAT projection

    struct ProjectionRange {
        float min;
        float max;
    };

    ProjectionRange projectVertsSoA(
        const float* vx, const float* vy, const float* vz,
        size_t paddedCount,
        float axisX, float axisY, float axisZ,
        float offsetProj);

    static constexpr size_t kPad = 8;


    // frustum culling

    struct Plane {
        float nx, ny, nz, d;
    };

    void cullAABBsAgainstFrustum(
        const float* minX, const float* minY, const float* minZ,
        const float* maxX, const float* maxY, const float* maxZ,
        size_t count,
        const Plane planes[6],
        uint8_t* outVisible);

    // AABB-vs-AABB intersection (batched)

    void aabbVsManyAABBs(
        const float aMin[3], const float aMax[3],
        const float* bMinX, const float* bMinY, const float* bMinZ,
        const float* bMaxX, const float* bMaxY, const float* bMaxZ,
        size_t count,
        float margin,
        uint8_t* outIntersect
    );

    // ray vs AABB (batched slab test)

    void rayVsManyAABBs(
        const float rayOrigin[3], const float rayDir[3],
        const float* bMinX, const float* bMinY, const float* bMinZ,
        const float* bMaxX, const float* bMaxY, const float* bMaxZ,
        size_t count,
        float maxDistance,
        uint8_t* outHit,
        float* outTHit
    );


    // particle kinematics step

    void integrateParticleKinematics(
        float* posX, float* posY, float* posZ,
        float* velX, float* velY, float* velZ,
        float* prevPosX, float* prevPosY, float* prevPosZ,
        float* prevPrevPosX, float* prevPrevPosY, float* prevPrevPosZ,
        float* age,
        const float* lifetime,
        const float* type,
        uint8_t* dead,
        size_t count,
        float dt,
        float gravity
    );
}
