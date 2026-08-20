#include <engine/SpatialGrid.h>
#include <engine/Collider.h>
#include <engine/SIMD.h>
#include <algorithm>
#include <utility>

void engine::SpatialGrid::clear() {
    for (auto& cell : dynamicCells) { cell.clear(); }
    for (auto& cell : staticCells) { cell.clear(); }
    dynamicColliderCells.clear();
    staticColliderCells.clear();
}

std::pair<glm::uvec3, glm::uvec3> engine::SpatialGrid::getCellRange(const AABB& aabb) const {
    auto minCell = getCellPos(aabb.min);
    auto maxCell = getCellPos(aabb.max);
    return std::make_pair(minCell, maxCell);
}

void engine::SpatialGrid::insert(Collider* collider) {
    const AABB& aabb = collider->getWorldAABB();
    const auto& [minCell, maxCell] = getCellRange(aabb);
    
    bool isDynamic = collider->getIsDynamic();
    auto& cells = isDynamic ? dynamicCells : staticCells;
    auto& colliderCells = isDynamic ? dynamicColliderCells : staticColliderCells;
    
    std::vector<size_t>& occupiedCells = colliderCells[collider];
    occupiedCells.clear();

    for (size_t x = minCell.x; x <= maxCell.x; ++x) {
        for (size_t y = minCell.y; y <= maxCell.y; ++y) {
            for (size_t z = minCell.z; z <= maxCell.z; ++z) {
                glm::uvec3 cellCoord{x, y, z};
                size_t index = getCellIndex(cellCoord);
                cells[index].push_back(collider);
                occupiedCells.push_back(index);
            }
        }
    }
}

void engine::SpatialGrid::remove(Collider* collider) {
    if (!collider) return;
    auto deleteIt = [](std::vector<std::vector<Collider*>>& cells, std::unordered_map<Collider*, std::vector<size_t>>& colliderCells, Collider* collider) {
        auto it = colliderCells.find(collider);
        if (it != colliderCells.end()) {
            for (size_t i = 0; i < it->second.size(); ++i) {
                size_t cellIndex = it->second[i];
                auto& vec = cells[cellIndex];
                std::erase_if(vec, [collider](Collider* c) { return c == collider; });
            }
            colliderCells.erase(it);
        }
    };
    deleteIt(dynamicCells, dynamicColliderCells, collider);
    deleteIt(staticCells, staticColliderCells, collider);
}

void engine::SpatialGrid::update(Collider* collider) {
    remove(collider);
    insert(collider);
}

void engine::SpatialGrid::query(const AABB& aabb, Candidates& out, float margin) const {
    out.clear();
    const auto& [minCell, maxCell] = getCellRange(aabb);

    size_t rangeX = static_cast<size_t>(maxCell.x - minCell.x);
    size_t rangeY = static_cast<size_t>(maxCell.y - minCell.y);
    size_t rangeZ = static_cast<size_t>(maxCell.z - minCell.z);
    const size_t totalCells = (rangeX + 1) * (rangeY + 1) * (rangeZ + 1);

    // 2 grids
    const std::vector<std::vector<Collider*>>* grids[2] = { &dynamicCells, &staticCells };

    if (totalCells == 1) {
        // single-cell fast path
        for (const auto* cells : grids) {
            const auto& vec = (*cells)[getCellIndex(minCell)];
            for (const auto& c : vec) {
                out.colliders.push_back(c);
                out.minX.push_back(c->getWorldAABB().min.x); out.minY.push_back(c->getWorldAABB().min.y); out.minZ.push_back(c->getWorldAABB().min.z);
                out.maxX.push_back(c->getWorldAABB().max.x); out.maxY.push_back(c->getWorldAABB().max.y); out.maxZ.push_back(c->getWorldAABB().max.z);
            }
        }
        out.intersects.resize(out.colliders.size());
    } else {
        // multi-cell path
        for (size_t x = minCell.x; x <= maxCell.x; ++x) {
            for (size_t y = minCell.y; y <= maxCell.y; ++y) {
                for (size_t z = minCell.z; z <= maxCell.z; ++z) {
                    for (const auto* cells : grids) {
                        const auto& vec = (*cells)[getCellIndex(glm::uvec3{x, y, z})];
                        for (const auto& c : vec) {
                            out.colliders.push_back(c);
                        }
                    }
                }
            }
        }

        if (out.colliders.size() > 1) {
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
    }

    const size_t n = out.colliders.size();

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
    for (auto& cell : dynamicCells) { cell.clear(); }
    dynamicColliderCells.clear();
    
    for (Collider* collider : colliders) {
        if (collider->getIsDynamic() || staticColliderCells.find(collider) == staticColliderCells.end()) {
            insert(collider);
        }
    }
}
