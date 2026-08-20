#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>
#include <optional>
#include <unordered_map>
#include <engine/ModelManager.h>

namespace engine {
    class Collider;

    class SpatialGrid {
    public:
        SpatialGrid(float cellSize = 2.0f, glm::vec3 mapSize = glm::vec3(100.0f)) {
                invCellSize = 1.0f / cellSize;
                maxCells = glm::uvec3(glm::ceil(mapSize * invCellSize));
                mapCenter = glm::vec3(maxCells) * 0.5f;
                dynamicCells.resize(maxCells.x * maxCells.y * maxCells.z);
                staticCells.resize(maxCells.x * maxCells.y * maxCells.z);
                std::for_each(dynamicCells.begin(), dynamicCells.end(), [](auto& v) { v.reserve(4); });
            }

        struct Candidates {
            std::vector<Collider*> colliders;
            std::vector<float> minX, minY, minZ;
            std::vector<float> maxX, maxY, maxZ;
            std::vector<uint8_t> intersects;

            size_t size() const { return colliders.size(); }
            bool empty() const { return colliders.empty(); }
            void clear() {
                colliders.clear();
                minX.clear(); minY.clear(); minZ.clear();
                maxX.clear(); maxY.clear(); maxZ.clear();
                intersects.clear();
            }
        };

        void clear();
        void insert(Collider* collider);
        void remove(Collider* collider);

        void update(Collider* collider);

        void query(const AABB& aabb, Candidates& out, float margin = 0.0f) const;

        void rebuild(const std::vector<Collider*>& colliders);
        
    private:
        glm::uvec3 getCellPos(glm::vec3 coord) const {
            glm::vec3 local = coord * invCellSize + mapCenter;
            glm::uvec3 cell(glm::floor(local));
            cell = glm::clamp(cell, glm::uvec3(0), maxCells - glm::uvec3(1));
            return cell;
        }

        size_t getCellIndex(glm::vec3 pos) const {
            const auto c = getCellPos(pos);
            return c.x + c.y * maxCells.x + c.z * maxCells.x * maxCells.y;
        }

        size_t getCellIndex(glm::uvec3 validCell) const { // assume bounds validation
            return validCell.x + validCell.y * maxCells.x + validCell.z * maxCells.x * maxCells.y;
        }

        std::pair<glm::uvec3, glm::uvec3> getCellRange(const AABB& aabb) const;

        float invCellSize;
        glm::uvec3 maxCells;
        glm::vec3 mapCenter;
        std::unordered_map<Collider*, std::vector<size_t>> dynamicColliderCells;
        std::unordered_map<Collider*, std::vector<size_t>> staticColliderCells;
        // 55*25*55 size / 2.0 cell size = 28*13*28 cells
        // 2 * (28*13*28 * (24B per vector + 8B ptr * (4 reserved / 2 (only dynamic))) + 24B per vector) = ~815KB
        std::vector<std::vector<Collider*>> dynamicCells;
        std::vector<std::vector<Collider*>> staticCells;
    };
}
