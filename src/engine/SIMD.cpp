#include <engine/SIMD.h>
#include <kernels_ispc.h>

namespace engine::simd {

    ProjectionRange projectVertsSoA(
        const float* vx, const float* vy, const float* vz,
        size_t n,
        float ax, float ay, float az,
        float off
    ) {
        if (n == 0) return {0.0f, 0.0f};
        ispc::ProjectionRange r;
        ispc::projectVertsSoA(
            vx, vy, vz, static_cast<int32_t>(n),
            ax, ay, az, off, &r
        );
        return {r.min, r.max};
    }

    void cullAABBsAgainstFrustum(
        const float* minX, const float* minY, const float* minZ,
        const float* maxX, const float* maxY, const float* maxZ,
        size_t count,
        const Plane planes[6],
        uint8_t* outVisible
    ) {
        if (count == 0) return;
        static_assert(sizeof(Plane) == sizeof(ispc::Plane),
                      "engine::simd::Plane and ispc::Plane must have identical layout");
        ispc::cullAABBsAgainstFrustum(
            minX, minY, minZ, maxX, maxY, maxZ,
            static_cast<int32_t>(count),
            reinterpret_cast<const ispc::Plane*>(planes),
            outVisible
        );
    }

    void aabbVsManyAABBs(
        const float aMin[3], const float aMax[3],
        const float* bMinX, const float* bMinY, const float* bMinZ,
        const float* bMaxX, const float* bMaxY, const float* bMaxZ,
        size_t count,
        float margin,
        uint8_t* outIntersect
    ) {
        if (count == 0) return;
        ispc::aabbVsManyAABBs(
            aMin[0], aMin[1], aMin[2], aMax[0], aMax[1], aMax[2],
            bMinX, bMinY, bMinZ, bMaxX, bMaxY, bMaxZ,
            static_cast<int32_t>(count),
            margin,
            outIntersect
        );
    }

    void rayVsManyAABBs(
        const float rayOrigin[3], const float rayDir[3],
        const float* bMinX, const float* bMinY, const float* bMinZ,
        const float* bMaxX, const float* bMaxY, const float* bMaxZ,
        size_t count,
        float maxDistance,
        uint8_t* outHit,
        float* outTHit
    ) {
        if (count == 0) return;
        ispc::rayVsManyAABBs(
            rayOrigin[0], rayOrigin[1], rayOrigin[2],
            rayDir[0], rayDir[1], rayDir[2],
            bMinX, bMinY, bMinZ, bMaxX, bMaxY, bMaxZ,
            static_cast<int32_t>(count),
            maxDistance,
            outHit, outTHit
        );
    }

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
    ) {
        if (count == 0) return;
        ispc::integrateParticleKinematics(
            posX, posY, posZ,
            velX, velY, velZ,
            prevPosX, prevPosY, prevPosZ,
            prevPrevPosX, prevPrevPosY, prevPrevPosZ,
            age, lifetime, type, dead,
            static_cast<int32_t>(count),
            dt, gravity
        );
    }
}
