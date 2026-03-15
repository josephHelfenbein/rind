#pragma once

#include <engine/EntityManager.h>
#include <engine/ModelManager.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <span>

namespace engine {
    class Collider : public Entity {
    public:
        enum class ColliderType {
            AABB,
            OBB,
            ConvexHull
        };
        struct CollisionMTV {
            glm::vec3 mtv{0.0f};
            glm::vec3 normal{0.0f};
            float penetrationDepth{0.0f};
        };
        struct Collision {
            Collider* other = nullptr;
            CollisionMTV mtv;
            glm::vec3 worldHitPoint{0.0f};
        };
        Collider(
            EntityManager* entityManager,
            const std::string& name,
            const glm::mat4& transform,
            const ColliderType& type
        );
        virtual ~Collider();
        const ColliderType& getColliderType() const { return type; }
        virtual AABB getWorldAABB() = 0;
        virtual bool intersectsMTV(Collider& other, CollisionMTV& out, const glm::mat4& deltaTransform = glm::mat4(1.0f)) = 0;
        glm::vec3 intersects(Collider& other, const glm::mat4 deltaTransform = glm::mat4(1.0f)) {
            CollisionMTV mtv;
            if (intersectsMTV(other, mtv, deltaTransform)) {
                return mtv.mtv;
            }
            return glm::vec3(0.0f);
        }
        inline static Collision testRayCollision(Collider* collider, AABB rayAABB, const glm::vec3& rayOrigin, const glm::vec3& rayDir, float maxDistance, Collider* ignoreCollider);
        static bool aabbIntersects(const AABB& a, const AABB& b, float margin = 0.0f);
        static Collision raycastFirst(EntityManager* entityManager, const glm::vec3& rayOrigin, const glm::vec3& rayDir, float maxDistance, Collider* ignoreCollider, float margin = 0.1f);
        static void raycast(EntityManager* entityManager, std::vector<Collision>& outColliders, const glm::vec3& rayOrigin, const glm::vec3& rayDir, float maxDistance, Collider* ignoreCollider = nullptr, float margin = 0.1f);
        static size_t raycast(EntityManager* entityManager, const glm::vec3& rayOrigin, const glm::vec3& rayDir, float maxDistance, Collider* ignoreCollider = nullptr, float margin = 0.1f);
        static bool raycastAny(EntityManager* entityManager, const glm::vec3& rayOrigin, const glm::vec3& rayDir, float maxDistance, Collider* ignoreCollider = nullptr, float margin = 0.1f);
        static AABB aabbFromCorners(const std::array<glm::vec3, 8>& corners);
        static std::array<glm::vec3, 8> getCornersFromAABB(const AABB& aabb);
        bool getIsTrigger() const { return isTrigger; }
        void setIsTrigger(bool trigger) {
            isTrigger = trigger;
            setEntityType(trigger ? engine::Entity::EntityType::Trigger : engine::Entity::EntityType::Collider);
        }
        bool getIsDynamic() const { return isDynamic; }
        void setIsDynamic(bool dynamic) {
            if (dynamic && !isDynamic) {
                getEntityManager()->addDynamicCollider(this);
            } else if (!dynamic && isDynamic) {
                getEntityManager()->removeDynamicCollider(this);
            }
            isDynamic = dynamic;
        }
    protected:
        static std::array<glm::vec3, 8> buildOBBCorners(const glm::mat4& transform, const glm::vec3& half);
        static std::pair<float, float> projectOntoAxis(const std::array<glm::vec3, 8>& corners, const glm::vec3& axis); // min, max
        static bool aabbOverlapMTV(const AABB& a, const AABB& b, CollisionMTV& out);
        static glm::vec3 normalizeOrZero(const glm::vec3& v);
        static void addAxisUnique(std::vector<glm::vec3>& axes, const glm::vec3& axis);
        static std::pair<float, float> projectVertsOntoAxis(std::span<const glm::vec3> verts, const glm::vec3& axis, const glm::vec3& offset = glm::vec3(0.0f)); // min, max
        static bool satMTV(std::span<const glm::vec3> vertsA, std::span<const glm::vec3> vertsB, std::span<const glm::vec3> edgesA, std::span<const glm::vec3> edgesB, std::span<const glm::vec3> axesA, std::span<const glm::vec3> axesB, CollisionMTV& out, const glm::vec3 centerDelta, const glm::vec3& offsetA = glm::vec3(0.0f), const glm::vec3& offsetB = glm::vec3(0.0f));
    private:
        bool isTrigger = false;
        bool isDynamic = false;
        ColliderType type;
    };
    class AABBCollider;
    class OBBCollider;
    class ConvexHullCollider;

    class AABBCollider : public Collider {
    public:
        AABBCollider(EntityManager* entityManager, const glm::mat4& transform, const std::string& parentName, const glm::vec3 halfSize = glm::vec3(0.5f))
            : Collider(entityManager, "collision_" + parentName, transform, ColliderType::AABB), halfSize(halfSize) {}
        AABB getWorldAABB() override;
        bool intersectsMTV(Collider& other, CollisionMTV& out, const glm::mat4& deltaTransform = glm::mat4(1.0f)) override;
    private:
        glm::vec3 halfSize;
    };
    class OBBCollider : public Collider {
    public:
        OBBCollider(EntityManager* entityManager, const glm::mat4& transform, const std::string& parentName, const glm::vec3 halfSize = glm::vec3(0.5f))
            : Collider(entityManager, "collision_" + parentName, transform, ColliderType::OBB), halfSize(halfSize) {}
        AABB getWorldAABB() override;
        bool intersectsMTV(Collider& other, CollisionMTV& out, const glm::mat4& deltaTransform = glm::mat4(1.0f)) override;
        glm::vec3 getHalfSize() const { return halfSize; }
        
        void ensureCached();
        const std::array<glm::vec3, 8>& getCornersCache() const { return cornersCache; }
        const std::array<glm::vec3, 3>& getAxesCache() const { return axesCache; }
        glm::vec3 getCenterCache() const { return centerCache; }
        
    private:
        glm::vec3 halfSize;
        
        mutable std::array<glm::vec3, 8> cornersCache;
        mutable std::array<glm::vec3, 3> axesCache;
        mutable glm::vec3 centerCache{0.0f};
        mutable uint32_t lastTransformGeneration = 0;
        mutable bool isCached = false;
    };
    class ConvexHullCollider : public Collider {
    public:
        ConvexHullCollider(EntityManager* entityManager, const glm::mat4& transform, const std::string& parentName)
            : Collider(entityManager, "collision_" + parentName, transform, ColliderType::ConvexHull) {}
        
        AABB getWorldAABB() override;
        bool intersectsMTV(Collider& other, CollisionMTV& out, const glm::mat4& deltaTransform = glm::mat4(1.0f)) override;

        const std::vector<glm::vec3>& getWorldVerts() const { return worldVerts; }
        const std::vector<glm::vec3>& getEdgeAxesCached() const { return edgeAxesCached; }
        const std::vector<glm::vec3>& getFaceAxesCached() const { return faceAxesCached; }
        glm::vec3 getWorldCenter() const { return worldCenter; }
        void setVertsFromModel(std::vector<glm::vec3>&& vertices, std::vector<uint32_t>&& indices, const glm::mat4& transform = glm::mat4(1.0f));
        void setVertsFromModel(const std::vector<glm::vec3>& vertices, const std::vector<uint32_t>& indices, const glm::mat4& transform);

    private:
        std::vector<glm::vec3> localVerts;
        std::vector<glm::ivec3> localTris;
        std::vector<glm::vec3> worldVerts;
        std::vector<glm::vec3> edgeAxesCached;
        std::vector<glm::vec3> faceAxesCached;
        glm::vec3 worldCenter{0.0f};
        uint32_t lastTransformGeneration = 0;
        bool isCached = false;
        void ensureCached();
        void buildConvexData(const std::vector<glm::vec3>& verts, const std::vector<glm::ivec3>& tris, const glm::mat4& transform, std::vector<glm::vec3>& outVerts, std::vector<glm::vec3>& outEdgeAxes, std::vector<glm::vec3>& outFaceAxes, glm::vec3& outCenter);
    };
};