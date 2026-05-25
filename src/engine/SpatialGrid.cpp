#include <engine/SpatialGrid.h>
#include <engine/Collider.h>
#include <engine/SIMD.h>
#include <algorithm>

engine::SpatialGrid::SpatialGrid(float cellSize)
    : cellSize(cellSize), invCellSize(1.0f / cellSize) {}

void engine::SpatialGrid::clear() {
    dynamicCells.clear();
    dynamicColliderCells.clear();
    staticCells.clear();
    staticColliderCells.clear();
}

engine::SpatialGrid::CellCoord engine::SpatialGrid::getCell(const glm::vec3& pos) const {
    return {
        static_cast<int>(std::floor(pos.x * invCellSize)),
        static_cast<int>(std::floor(pos.y * invCellSize)),
        static_cast<int>(std::floor(pos.z * invCellSize))
    };
}

void engine::SpatialGrid::getCellRange(const AABB& aabb, CellCoord& minCell, CellCoord& maxCell) const {
    minCell = getCell(aabb.min);
    maxCell = getCell(aabb.max);
}

void engine::SpatialGrid::insert(Collider* collider, const AABB& aabb) {
    CellCoord minCell, maxCell;
    getCellRange(aabb, minCell, maxCell);
    
    bool isDynamic = collider->getIsDynamic();
    auto& cells = isDynamic ? dynamicCells : staticCells;
    auto& colliderCells = isDynamic ? dynamicColliderCells : staticColliderCells;
    
    std::vector<CellCoord>& occupiedCells = colliderCells[collider];
    occupiedCells.clear();
    
    const int maxCellsTotal = 512;
    int rangeX = maxCell.x - minCell.x;
    int rangeY = maxCell.y - minCell.y;
    int rangeZ = maxCell.z - minCell.z;
    int totalCells = (rangeX + 1) * (rangeY + 1) * (rangeZ + 1);
    
    if (totalCells > maxCellsTotal) {
        occupiedCells.push_back(minCell);
        occupiedCells.push_back(maxCell);
        occupiedCells.push_back(CellCoord{(minCell.x + maxCell.x) / 2, (minCell.y + maxCell.y) / 2, (minCell.z + maxCell.z) / 2});
        for (const auto& coord : occupiedCells) {
            cells[coord].push_back(collider);
        }
        return;
    }
    
    for (int x = minCell.x; x <= maxCell.x; ++x) {
        for (int y = minCell.y; y <= maxCell.y; ++y) {
            for (int z = minCell.z; z <= maxCell.z; ++z) {
                CellCoord coord{x, y, z};
                cells[coord].push_back(collider);
                occupiedCells.push_back(coord);
            }
        }
    }
}

void engine::SpatialGrid::remove(Collider* collider) {
    auto it = dynamicColliderCells.find(collider);
    if (it != dynamicColliderCells.end()) {
        for (const CellCoord& coord : it->second) {
            auto cellIt = dynamicCells.find(coord);
            if (cellIt != dynamicCells.end()) {
                auto& vec = cellIt->second;
                std::erase(vec, collider);
                if (vec.empty()) {
                    dynamicCells.erase(cellIt);
                }
            }
        }
        dynamicColliderCells.erase(it);
        return;
    }
    
    it = staticColliderCells.find(collider);
    if (it != staticColliderCells.end()) {
        for (const CellCoord& coord : it->second) {
            auto cellIt = staticCells.find(coord);
            if (cellIt != staticCells.end()) {
                auto& vec = cellIt->second;
                std::erase(vec, collider);
                if (vec.empty()) {
                    staticCells.erase(cellIt);
                }
            }
        }
        staticColliderCells.erase(it);
    }
}

void engine::SpatialGrid::update(Collider* collider, const AABB& aabb) {
    remove(collider);
    insert(collider, aabb);
}

void engine::SpatialGrid::query(const AABB& aabb, Candidates& out, float margin) const {
    out.clear();
    CellCoord minCell, maxCell;
    getCellRange(aabb, minCell, maxCell);

    const int maxCellsPerAxis = 150;
    int rangeX = std::min(maxCell.x - minCell.x, maxCellsPerAxis);
    int rangeY = std::min(maxCell.y - minCell.y, maxCellsPerAxis);
    int rangeZ = std::min(maxCell.z - minCell.z, maxCellsPerAxis);

    const std::unordered_map<CellCoord, std::vector<Collider*>, CellCoordHash>* grids[2] = { &dynamicCells, &staticCells };

    // gather colliders from overlapping cells, deduplicated within each grid, with their world AABBs pushed into the SoA columns
    for (int x = minCell.x; x <= minCell.x + rangeX; ++x) {
        for (int y = minCell.y; y <= minCell.y + rangeY; ++y) {
            for (int z = minCell.z; z <= minCell.z + rangeZ; ++z) {
                CellCoord coord{x, y, z};
                for (const auto* cells : grids) {
                    auto it = cells->find(coord);
                    if (it != cells->end()) {
                        const auto& vec = it->second;
                        out.colliders.insert(out.colliders.end(), vec.begin(), vec.end());
                    }
                }
            }
        }
    }

    const size_t totalCells = static_cast<size_t>(rangeX + 1) * static_cast<size_t>(rangeY + 1) * static_cast<size_t>(rangeZ + 1);
    if (totalCells > 1 && out.colliders.size() > 1) {
        std::sort(out.colliders.begin(), out.colliders.end());
        out.colliders.erase(std::unique(out.colliders.begin(), out.colliders.end()), out.colliders.end());
    }

    const size_t n = out.colliders.size();
    out.minX.resize(n); out.minY.resize(n); out.minZ.resize(n);
    out.maxX.resize(n); out.maxY.resize(n); out.maxZ.resize(n);
    out.intersects.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const AABB world = out.colliders[i]->getWorldAABB();
        out.minX[i] = world.min.x; out.minY[i] = world.min.y; out.minZ[i] = world.min.z;
        out.maxX[i] = world.max.x; out.maxY[i] = world.max.y; out.maxZ[i] = world.max.z;
    }

    // SIMD AABB-vs-AABB filter against the query AABB
    if (n > 0) {
        const float qMin[3] = {aabb.min.x, aabb.min.y, aabb.min.z};
        const float qMax[3] = {aabb.max.x, aabb.max.y, aabb.max.z};
        engine::simd::aabbVsManyAABBs(
            qMin, qMax,
            out.minX.data(), out.minY.data(), out.minZ.data(),
            out.maxX.data(), out.maxY.data(), out.maxZ.data(),
            n, margin, out.intersects.data());
    }
}

void engine::SpatialGrid::rebuild(const std::vector<Collider*>& colliders) {
    dynamicCells.clear();
    dynamicColliderCells.clear();
    
    for (Collider* collider : colliders) {
        if (collider->getIsDynamic()) {
            AABB aabb = collider->getWorldAABB();
            insert(collider, aabb);
        } else if (staticColliderCells.find(collider) == staticColliderCells.end()) {
            AABB aabb = collider->getWorldAABB();
            insert(collider, aabb);
        }
    }
}
