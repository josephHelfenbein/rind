#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>

namespace engine {
    class Collider;
    struct AABB;

    class SpatialGrid {
    public:
        SpatialGrid(float cellSize = 10.0f);

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
        void insert(Collider* collider, const AABB& aabb);
        void remove(Collider* collider);

        void update(Collider* collider, const AABB& aabb);

        void query(const AABB& aabb, Candidates& out, float margin = 0.0f) const;

        void rebuild(const std::vector<Collider*>& colliders);

        void setCellSize(float size) { cellSize = size; invCellSize = 1.0f / size; }
        float getCellSize() const { return cellSize; }
        
    private:
        struct CellCoord {
            int x, y, z;
            bool operator==(const CellCoord& other) const {
                return x == other.x && y == other.y && z == other.z;
            }
        };
        
        struct CellCoordHash {
            size_t operator()(const CellCoord& coord) const {
                return static_cast<size_t>(coord.x) * 73856093UL ^
                       static_cast<size_t>(coord.y) * 19349663UL ^
                       static_cast<size_t>(coord.z) * 83492791UL;
            }
        };
        
        CellCoord getCell(const glm::vec3& pos) const;
        void getCellRange(const AABB& aabb, CellCoord& minCell, CellCoord& maxCell) const;
        
        float cellSize;
        float invCellSize;
        std::unordered_map<CellCoord, std::vector<Collider*>, CellCoordHash> dynamicCells;
        std::unordered_map<Collider*, std::vector<CellCoord>> dynamicColliderCells;
        std::unordered_map<CellCoord, std::vector<Collider*>, CellCoordHash> staticCells;
        std::unordered_map<Collider*, std::vector<CellCoord>> staticColliderCells;
    };
}
