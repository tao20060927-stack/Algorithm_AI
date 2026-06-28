#include "ShortestPathStrategy.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <queue>
#include <set>
#include <stdexcept>

namespace ai_player {
std::vector<Position> shortestPath(const MazeData &data, Position start, Position target, const std::string &algorithm)
{
    const int rows = static_cast<int>(data.grid.size());
    const int cols = static_cast<int>(data.grid[0].size());
    std::vector dist(rows, std::vector<int>(cols, INT_MAX));
    std::vector parent(rows, std::vector<Position>(cols, kInvalid));
    auto heuristic = [&](Position pos) {
        if (algorithm == "astar" || algorithm == "smart") {
            return std::abs(pos.first - target.first) + std::abs(pos.second - target.second);
        }
        return 0;
    };

    using Node = std::pair<int, Position>;
    std::priority_queue<Node, std::vector<Node>, std::greater<>> queue;
    dist[start.first][start.second] = 0;
    queue.push({heuristic(start), start});

    while (!queue.empty()) {
        const auto [priority, pos] = queue.top();
        queue.pop();
        (void)priority;
        if (pos == target) break;
        for (const auto [dr, dc] : kDirs) {
            const int nr = pos.first + dr;
            const int nc = pos.second + dc;
            if (!passable(data, nr, nc)) continue;
            const int nextDist = dist[pos.first][pos.second] + 1;
            if (nextDist < dist[nr][nc]) {
                dist[nr][nc] = nextDist;
                parent[nr][nc] = pos;
                queue.push({nextDist + heuristic({nr, nc}), {nr, nc}});
            }
        }
    }

    if (dist[target.first][target.second] == INT_MAX) {
        throw std::runtime_error("target is unreachable");
    }

    std::vector<Position> path;
    for (Position pos = target; pos != kInvalid; pos = parent[pos.first][pos.second]) {
        path.push_back(pos);
        if (pos == start) break;
    }
    std::reverse(path.begin(), path.end());
    return path;
}

std::vector<Position> planAdventurePath(const MazeData &data, const std::string &algorithm)
{
    MazeData planningData = data;
    std::set<Position> remaining;
    for (const auto &pos : planningData.golds) remaining.insert(pos);
    for (const auto &pos : planningData.locks) remaining.insert(pos);
    std::vector<bool> bossTriggered(planningData.bosses.size(), false);

    Position current = planningData.start;
    std::vector<Position> fullPath{current};
    while (!remaining.empty() || std::any_of(bossTriggered.begin(), bossTriggered.end(), [](bool done) { return !done; })) {
        Position best = kInvalid;
        std::vector<Position> bestPath;
        int bestBossIndex = -1;
        for (const auto &target : remaining) {
            try {
                auto candidate = shortestPath(planningData, current, target, algorithm);
                if (best == kInvalid || candidate.size() < bestPath.size()) {
                    best = target;
                    bestPath = std::move(candidate);
                    bestBossIndex = -1;
                }
            } catch (const std::exception &) {
            }
        }
        for (int i = 0; i < static_cast<int>(planningData.bosses.size()); ++i) {
            if (bossTriggered[i]) continue;
            for (const auto [dr, dc] : kDirs) {
                Position trigger{planningData.bosses[i].first + dr, planningData.bosses[i].second + dc};
                if (!passable(planningData, trigger.first, trigger.second)) continue;
                try {
                    auto candidate = shortestPath(planningData, current, trigger, algorithm);
                    if (best == kInvalid || candidate.size() < bestPath.size()) {
                        best = trigger;
                        bestPath = std::move(candidate);
                        bestBossIndex = i;
                    }
                } catch (const std::exception &) {
                }
            }
        }
        if (best == kInvalid) break;
        fullPath.insert(fullPath.end(), bestPath.begin() + 1, bestPath.end());
        current = best;
        if (bestBossIndex >= 0) {
            bossTriggered[bestBossIndex] = true;
            const auto [row, col] = planningData.bosses[bestBossIndex];
            planningData.grid[row][col] = " ";
        }
        else remaining.erase(best);
    }

    auto exitPath = shortestPath(planningData, current, planningData.exit, algorithm);
    fullPath.insert(fullPath.end(), exitPath.begin() + 1, exitPath.end());
    return fullPath;
}
} // namespace ai_player
