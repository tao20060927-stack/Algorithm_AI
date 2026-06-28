#include "RealtimeGreedyStrategy.h"

#include <algorithm>
#include <cmath>
#include <climits>
#include <queue>
#include <set>

namespace ai_player {
namespace {
constexpr double kAlpha = 2.0;
constexpr double kBeta = 0.5;
constexpr double kEpsilon = 1e-6;

class MemoryGreedyAgent {
public:
    explicit MemoryGreedyAgent(const MazeData &maze) : maze_(maze)
    {
        const int rows = static_cast<int>(maze_.grid.size());
        const int cols = static_cast<int>(maze_.grid[0].size());
        knownMap_.assign(rows, std::vector<std::string>(cols, "U"));
        observed_.assign(rows, std::vector<bool>(cols, false));
        visited_.assign(rows, std::vector<bool>(cols, false));
        visitCount_.assign(rows, std::vector<int>(cols, 0));
        collectedCoins_.assign(rows, std::vector<bool>(cols, false));
        triggeredTraps_.assign(rows, std::vector<bool>(cols, false));
        bossTriggerCells_.assign(rows, std::vector<bool>(cols, false));
    }

    std::vector<Position> run()
    {
        Position current = maze_.start;
        std::vector<Position> path{current};
        const int maxSteps = static_cast<int>(maze_.grid.size() * maze_.grid[0].size() * 4);

        for (int step = 0; step < maxSteps && current != maze_.exit; ++step) {
            updateKnownMap(current);
            applyCurrentCell(current);

            const auto nextPath = selectBestPath(current);
            if (nextPath.size() <= 1) break;

            previousPosition_ = current;
            hasPreviousPosition_ = true;
            current = nextPath[1];
            path.push_back(current);
        }
        return path;
    }

private:
    const MazeData &maze_;
    std::vector<std::vector<std::string>> knownMap_;
    std::vector<std::vector<bool>> observed_;
    std::vector<std::vector<bool>> visited_;
    std::vector<std::vector<int>> visitCount_;
    std::vector<std::vector<bool>> collectedCoins_;
    std::vector<std::vector<bool>> triggeredTraps_;
    std::vector<Position> knownBosses_;
    std::vector<bool> defeatedBosses_;
    std::vector<std::vector<bool>> bossTriggerCells_;
    int currentResource_ = 0;
    int steps_ = 0;
    Position currentTarget_ = kInvalid;
    double currentTargetScore_ = -1e18;
    Position previousPosition_ = kInvalid;
    bool hasPreviousPosition_ = false;

    // 功能：判断坐标是否在迷宫内；输入：行列坐标；输出：是否有效。
    bool inBounds(int row, int col) const
    {
        return row >= 0 && col >= 0 && row < static_cast<int>(maze_.grid.size()) &&
               col < static_cast<int>(maze_.grid[0].size());
    }

    // 功能：根据当前位置 3x3 视野更新 known_map；输入：当前位置；输出：无，更新观察状态和已知 Boss 触发区。
    void updateKnownMap(Position current)
    {
        for (int row = current.first - 1; row <= current.first + 1; ++row) {
            for (int col = current.second - 1; col <= current.second + 1; ++col) {
                if (!inBounds(row, col)) continue;
                observed_[row][col] = true;
                knownMap_[row][col] = maze_.grid[row][col];
                if (knownMap_[row][col] == "B" && !containsBoss({row, col})) {
                    knownBosses_.push_back({row, col});
                    defeatedBosses_.push_back(false);
                    updateBossTriggerCells({row, col});
                }
            }
        }
    }

    // 功能：结算 AI 实际站上当前格后的状态；输入：当前位置；输出：无，更新资源、访问和一次性资源触发状态。
    void applyCurrentCell(Position current)
    {
        const auto [row, col] = current;
        visited_[row][col] = true;
        ++visitCount_[row][col];
        if (steps_ > 0 && knownMap_[row][col] == "G" && !collectedCoins_[row][col]) {
            currentResource_ += kGoldValue;
            collectedCoins_[row][col] = true;
        } else if (steps_ > 0 && knownMap_[row][col] == "T" && !triggeredTraps_[row][col]) {
            currentResource_ += kTrapValue;
            triggeredTraps_[row][col] = true;
        }
        markTriggeredBossDefeated(current);
        ++steps_;
    }

    // 功能：判断某个 Boss 是否已经被 3x3 视野观察并记录；输入：Boss 坐标；输出：是否已知。
    bool containsBoss(Position pos) const
    {
        return std::find(knownBosses_.begin(), knownBosses_.end(), pos) != knownBosses_.end();
    }

    // 功能：查找某个 Boss 在已知 Boss 列表中的下标；输入：Boss 坐标；输出：下标，找不到则为 -1。
    int bossIndexAt(Position pos) const
    {
        for (int i = 0; i < static_cast<int>(knownBosses_.size()); ++i) {
            if (knownBosses_[i] == pos) return i;
        }
        return -1;
    }

    // 功能：进入 Boss 正邻接触发区后标记 Boss 已击败；输入：当前位置；输出：无。
    void markTriggeredBossDefeated(Position current)
    {
        for (int i = 0; i < static_cast<int>(knownBosses_.size()); ++i) {
            const int distance = std::abs(current.first - knownBosses_[i].first) +
                                 std::abs(current.second - knownBosses_[i].second);
            if (distance == 1) {
                defeatedBosses_[i] = true;
            }
        }
    }

    // 功能：根据已观察到的 Boss 坐标标记上下左右触发区；输入：Boss 坐标；输出：无。
    void updateBossTriggerCells(Position boss)
    {
        for (const auto [dr, dc] : kDirs) {
            const int row = boss.first + dr;
            const int col = boss.second + dc;
            if (inBounds(row, col)) bossTriggerCells_[row][col] = true;
        }
    }

    // 功能：判断格子是否为已知 Boss 本体；输入：坐标；输出：是否为 Boss 本体。
    bool isBossCell(Position pos) const
    {
        if (!inBounds(pos.first, pos.second) || !observed_[pos.first][pos.second] ||
            knownMap_[pos.first][pos.second] != "B") {
            return false;
        }
        const int index = bossIndexAt(pos);
        return index < 0 || !defeatedBosses_[index];
    }

    // 功能：判断格子是否为任意已知 Boss 的上下左右触发区；输入：坐标；输出：是否会触发 Boss。
    bool isBossTriggerCell(Position pos) const
    {
        return inBounds(pos.first, pos.second) && bossTriggerCells_[pos.first][pos.second];
    }

    // 功能：判断格子是否为已知 Boss 的对角观察区；输入：坐标；输出：是否为对角观察格。
    bool isBossDiagonalObserveCell(Position pos) const
    {
        for (const auto &boss : knownBosses_) {
            if (std::abs(pos.first - boss.first) == 1 && std::abs(pos.second - boss.second) == 1) {
                return true;
            }
        }
        return false;
    }

    // 功能：判断格子能否作为 known_map 上的规划节点；输入：坐标；输出：是否可走。
    bool isWalkableForPlanning(Position pos) const
    {
        const auto [row, col] = pos;
        if (!inBounds(row, col) || !observed_[row][col]) return false;
        // Boss 本体不可直接通行；Boss 上下左右触发区可以走，进入后由结果层记录 Boss 战事件。
        if (knownMap_[row][col] == "#" || isBossCell(pos)) return false;
        (void)isBossTriggerCell(pos);
        (void)isBossDiagonalObserveCell(pos);
        return true;
    }

    // 功能：从已观察地图中生成候选目标；输入：无；输出：已观察、可达、未访问的目标集合。
    std::vector<Position> getCandidateTargets(Position current) const
    {
        std::vector<Position> targets;
        for (int row = 0; row < static_cast<int>(knownMap_.size()); ++row) {
            for (int col = 0; col < static_cast<int>(knownMap_[row].size()); ++col) {
                Position pos{row, col};
                if (!isWalkableForPlanning(pos) || visited_[row][col]) continue;
                if (findPathOnKnownMap(current, pos).empty()) continue;
                targets.push_back(pos);
            }
        }
        return targets;
    }

    // 功能：在 known_map 上执行 BFS；输入：起点和目标；输出：只经过已观察安全格子的最短路径。
    std::vector<Position> findPathOnKnownMap(Position start, Position target) const
    {
        if (!isWalkableForPlanning(start) || !isWalkableForPlanning(target)) return {};
        std::vector dist(knownMap_.size(), std::vector<int>(knownMap_[0].size(), INT_MAX));
        std::vector parent(knownMap_.size(), std::vector<Position>(knownMap_[0].size(), kInvalid));
        std::queue<Position> queue;
        queue.push(start);
        dist[start.first][start.second] = 0;

        while (!queue.empty()) {
            const auto [row, col] = queue.front();
            queue.pop();
            if (std::pair(row, col) == target) break;
            for (const auto [dr, dc] : kDirs) {
                Position next{row + dr, col + dc};
                if (!isWalkableForPlanning(next)) continue;
                if (dist[next.first][next.second] != INT_MAX) continue;
                dist[next.first][next.second] = dist[row][col] + 1;
                parent[next.first][next.second] = {row, col};
                queue.push(next);
            }
        }

        if (dist[target.first][target.second] == INT_MAX) return {};
        std::vector<Position> path;
        for (Position pos = target; pos != kInvalid; pos = parent[pos.first][pos.second]) {
            path.push_back(pos);
            if (pos == start) break;
        }
        std::reverse(path.begin(), path.end());
        return path;
    }

    // 功能：估算路径真实资源变化；输入：候选路径；输出：金币、陷阱一次性触发后的资源增量。
    int pathResourceDelta(const std::vector<Position> &path) const
    {
        int delta = 0;
        for (size_t i = 1; i < path.size(); ++i) {
            const auto [row, col] = path[i];
            if (knownMap_[row][col] == "G" && !collectedCoins_[row][col]) delta += kGoldValue;
            if (knownMap_[row][col] == "T" && !triggeredTraps_[row][col]) delta += kTrapValue;
        }
        return delta;
    }

    // 功能：计算目标点带来的新视野收益；输入：目标坐标；输出：目标 3x3 范围内未观察格数量。
    int newVisibleCount(Position target) const
    {
        int count = 0;
        for (int row = target.first - 1; row <= target.first + 1; ++row) {
            for (int col = target.second - 1; col <= target.second + 1; ++col) {
                if (inBounds(row, col) && !observed_[row][col]) ++count;
            }
        }
        return count;
    }

    // 功能：估计从目标点出发后续已知金币机会；输入：目标坐标；输出：当前 known_map 上最好的金币距离收益。
    double futureGain(Position target) const
    {
        double best = 0.0;
        for (int row = 0; row < static_cast<int>(knownMap_.size()); ++row) {
            for (int col = 0; col < static_cast<int>(knownMap_[row].size()); ++col) {
                if (knownMap_[row][col] != "G" || collectedCoins_[row][col]) continue;
                const auto path = findPathOnKnownMap(target, {row, col});
                if (path.empty()) continue;
                const int delta = pathResourceDelta(path);
                if (currentResource_ + delta < 0) continue;
                best = std::max(best, static_cast<double>(kGoldValue) / static_cast<double>(path.size()));
            }
        }
        return best;
    }

    // 功能：计算整条路径价值；输入：路径和目标；输出：用于选择目标的 score，非法路径返回极小值。
    double evaluatePath(const std::vector<Position> &path, Position target) const
    {
        if (path.size() <= 1) return -1e18;
        for (const auto &pos : path) {
            if (!isWalkableForPlanning(pos)) return -1e18;
        }

        const int delta = pathResourceDelta(path);
        const int projectedResource = currentResource_ + delta;
        if (projectedResource < 0) return -1e18;

        const int pathLen = static_cast<int>(path.size()) - 1;
        const double gain = delta + kAlpha * newVisibleCount(target) + kBeta * futureGain(target);
        if (gain <= 0.0) {
            return -1e9 - pathLen;
        }
        return gain / (pathLen + kEpsilon);
    }

    // 功能：遍历候选目标并选择价值最高路径；输入：当前位置；输出：下一步应沿着走的完整候选路径。
    std::vector<Position> selectBestPath(Position current)
    {
        std::vector<Position> heldPath;
        double heldScore = -1e18;
        if (currentTarget_ != kInvalid && isWalkableForPlanning(currentTarget_) &&
            !visited_[currentTarget_.first][currentTarget_.second]) {
            heldPath = findPathOnKnownMap(current, currentTarget_);
            heldScore = evaluatePath(heldPath, currentTarget_);
        }

        std::vector<Position> bestPath;
        double bestScore = -1e18;
        Position bestTarget = kInvalid;
        for (const auto &target : getCandidateTargets(current)) {
            auto path = findPathOnKnownMap(current, target);
            const double score = evaluatePath(path, target);
            if (score > bestScore) {
                bestScore = score;
                bestTarget = target;
                bestPath = std::move(path);
            }
        }

        // 保持当前目标可以减少每步重新估值造成的来回摇摆；只有新目标明显更好才切换。
        const bool shouldSwitchTarget = !bestPath.empty() &&
                                        (heldPath.empty() ||
                                         (heldScore > 0.0 ? bestScore > heldScore * 1.2
                                                          : bestScore > heldScore + 1.0));
        if (!heldPath.empty() && !shouldSwitchTarget) {
            currentTargetScore_ = heldScore;
            return heldPath;
        }
        if (!bestPath.empty()) {
            currentTarget_ = bestTarget;
            currentTargetScore_ = bestScore;
            return bestPath;
        }

        currentTarget_ = kInvalid;
        currentTargetScore_ = -1e18;
        return fallbackPath(current);
    }

    // 功能：候选目标为空时选择安全兜底路径；输入：当前位置；输出：通往出口或低访问安全格的路径。
    std::vector<Position> fallbackPath(Position current) const
    {
        if (observed_[maze_.exit.first][maze_.exit.second]) {
            auto exitPath = findPathOnKnownMap(current, maze_.exit);
            if (!exitPath.empty()) return exitPath;
        }

        std::vector<Position> bestPath;
        int bestNewVisible = -1;
        int bestVisit = INT_MAX;
        bool bestBacktracks = true;
        for (int row = 0; row < static_cast<int>(knownMap_.size()); ++row) {
            for (int col = 0; col < static_cast<int>(knownMap_[row].size()); ++col) {
                Position target{row, col};
                if (!isWalkableForPlanning(target) || target == current) continue;
                auto path = findPathOnKnownMap(current, target);
                if (path.empty()) continue;

                // fallback 不参与主评分，只用于脱困；优先走向还能点亮未知格的安全前沿。
                const int visibleGain = newVisibleCount(target);
                const bool backtracks = hasPreviousPosition_ && path.size() > 1 && path[1] == previousPosition_;
                if (visibleGain > bestNewVisible ||
                    (visibleGain == bestNewVisible && bestBacktracks && !backtracks) ||
                    (visibleGain == bestNewVisible && bestBacktracks == backtracks && visitCount_[row][col] < bestVisit) ||
                    (visibleGain == bestNewVisible && bestBacktracks == backtracks && visitCount_[row][col] == bestVisit &&
                     (bestPath.empty() || path.size() < bestPath.size()))) {
                    bestNewVisible = visibleGain;
                    bestVisit = visitCount_[row][col];
                    bestBacktracks = backtracks;
                    bestPath = std::move(path);
                }
            }
        }
        return bestPath;
    }
};
} // namespace

std::vector<Position> realtimeGreedyPath(const MazeData &data)
{
    MemoryGreedyAgent agent(data);
    return agent.run();
}
} // namespace ai_player
