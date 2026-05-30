struct ProjectionRange {
    float min;
    float max;
};

struct Plane {
    float nx, ny, nz, d;
};

export void projectVertsSoA(
    uniform const float vx[], uniform const float vy[], uniform const float vz[],
    uniform int n,
    uniform float ax, uniform float ay, uniform float az,
    uniform float off,
    uniform ProjectionRange* uniform result
) {
    if (n == 0) {
        result->min = 0.0f;
        result->max = 0.0f;
        return;
    }

    uniform float p0 = vx[0] * ax + vy[0] * ay + vz[0] * az + off;
    float pmin = p0;
    float pmax = p0;

    foreach (i = 0 ... n) {
        float p = vx[i] * ax + vy[i] * ay + vz[i] * az + off;
        pmin = min(pmin, p);
        pmax = max(pmax, p);
    }

    result->min = reduce_min(pmin);
    result->max = reduce_max(pmax);
}

export void cullAABBsAgainstFrustum(
    uniform const float minX[], uniform const float minY[], uniform const float minZ[],
    uniform const float maxX[], uniform const float maxY[], uniform const float maxZ[],
    uniform int count,
    uniform const Plane planes[],
    uniform unsigned int8 outVisible[]
) {
    foreach (i = 0 ... count) {
        bool inside = true;
        for (uniform int p = 0; p < 6; ++p) {
            uniform Plane pl = planes[p];
            float px = (pl.nx >= 0.0f) ? maxX[i] : minX[i];
            float py = (pl.ny >= 0.0f) ? maxY[i] : minY[i];
            float pz = (pl.nz >= 0.0f) ? maxZ[i] : minZ[i];
            float dist = pl.nx * px + pl.ny * py + pl.nz * pz + pl.d;
            if (dist < 0.0f) {
                inside = false;
            }
        }
        outVisible[i] = inside ? (unsigned int8) 1 : (unsigned int8) 0;
    }
}

export void aabbVsManyAABBs(
    uniform float aMinX, uniform float aMinY, uniform float aMinZ,
    uniform float aMaxX, uniform float aMaxY, uniform float aMaxZ,
    uniform const float bMinX[], uniform const float bMinY[], uniform const float bMinZ[],
    uniform const float bMaxX[], uniform const float bMaxY[], uniform const float bMaxZ[],
    uniform int count,
    uniform float margin,
    uniform unsigned int8 outIntersect[]
) {
    foreach (i = 0 ... count) {
        bool hit =
            (aMinX - margin <= bMaxX[i]) && (aMaxX + margin >= bMinX[i]) &&
            (aMinY - margin <= bMaxY[i]) && (aMaxY + margin >= bMinY[i]) &&
            (aMinZ - margin <= bMaxZ[i]) && (aMaxZ + margin >= bMinZ[i]);
        outIntersect[i] = hit ? (unsigned int8) 1 : (unsigned int8) 0;
    }
}

export void rayVsManyAABBs(
    uniform float oX, uniform float oY, uniform float oZ,
    uniform float dX, uniform float dY, uniform float dZ,
    uniform const float bMinX[], uniform const float bMinY[], uniform const float bMinZ[],
    uniform const float bMaxX[], uniform const float bMaxY[], uniform const float bMaxZ[],
    uniform int count,
    uniform float maxDistance,
    uniform unsigned int8 outHit[],
    uniform float outTHit[]
) {
    uniform float invX = 1.0f / dX;
    uniform float invY = 1.0f / dY;
    uniform float invZ = 1.0f / dZ;

    foreach (i = 0 ... count) {
        float t1x = (bMinX[i] - oX) * invX;
        float t2x = (bMaxX[i] - oX) * invX;
        float t1y = (bMinY[i] - oY) * invY;
        float t2y = (bMaxY[i] - oY) * invY;
        float t1z = (bMinZ[i] - oZ) * invZ;
        float t2z = (bMaxZ[i] - oZ) * invZ;
        float tNear = max(max(min(t1x, t2x), min(t1y, t2y)), min(t1z, t2z));
        float tFar  = min(min(max(t1x, t2x), max(t1y, t2y)), max(t1z, t2z));
        bool hit = (tNear <= tFar) && (tFar >= 0.0f) && (tNear <= maxDistance);
        outHit[i] = hit ? (unsigned int8) 1 : (unsigned int8) 0;
        outTHit[i] = hit ? (tNear >= 0.0f ? tNear : tFar) : 0.0f;
    }
}

export void integrateParticleKinematics(
    uniform float posX[],   uniform float posY[],   uniform float posZ[],
    uniform float velX[],   uniform float velY[],   uniform float velZ[],
    uniform float prevPosX[], uniform float prevPosY[], uniform float prevPosZ[],
    uniform float prevPrevPosX[], uniform float prevPrevPosY[], uniform float prevPrevPosZ[],
    uniform float age[],
    uniform const float lifetime[],
    uniform const float type[],
    uniform unsigned int8 dead[],
    uniform int count,
    uniform float dt,
    uniform float gravity
) {
    foreach (i = 0 ... count) {
        if (dead[i] != (unsigned int8) 0) continue;

        age[i] += dt;
        if (age[i] >= lifetime[i]) {
            dead[i] = (unsigned int8) 1;
            continue;
        }
        if (type[i] == 1.0f) continue;

        velY[i] -= gravity * dt;

        float cx = posX[i];
        float cy = posY[i];
        float cz = posZ[i];

        prevPrevPosX[i] = prevPosX[i];
        prevPrevPosY[i] = prevPosY[i];
        prevPrevPosZ[i] = prevPosZ[i];
        prevPosX[i] = cx;
        prevPosY[i] = cy;
        prevPosZ[i] = cz;

        float vx = velX[i];
        float vy = velY[i];
        float vz = velZ[i];
        float ssq = vx * vx + vy * vy + vz * vz;
        if (ssq < 0.01f) {
            dead[i] = (unsigned int8) 1;
            continue;
        }

        posX[i] = cx + vx * dt;
        posY[i] = cy + vy * dt;
        posZ[i] = cz + vz * dt;
    }
}
