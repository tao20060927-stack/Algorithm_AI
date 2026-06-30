#include "DivideConquerStrategy.h"

#include <algorithm>
#include <map>
#include <queue>
#include <set>

#include "Reward.h"

namespace ai_player {
namespace {

/**
 * 功能：判断完整迷宫中的格子是否可用于路由。
 * 输入：
 *   - data：完整迷宫数据，要求 grid 已经完成解析。
 *   - pos：待检查的真实坐标，可以位于迷宫外。
 * 输出：
 *   - 返回该真实坐标是否在范围内且不是墙。
 * 关键逻辑：
 *   - Boss 格按普通可通行格处理，保证路由层不再把 Boss 本体排除在价值和路径计算之外。
 */
bool mazeWalkable(const MazeData &data, Position pos)
{
    return pos.first >= 0 && pos.second >= 0 && pos.first < static_cast<int>(data.grid.size()) &&
           pos.second < static_cast<int>(data.grid[0].size()) && data.grid[pos.first][pos.second] != "#";
}

/**
 * 功能：根据双向父节点表合并 start 到 target 的路径。
 * 输入：
 *   - start：路径起点。
 *   - target：路径终点。
 *   - meeting：从两端搜索相遇的中间格。
 *   - fromStart：从起点侧记录的父节点表。
 *   - fromTarget：从终点侧记录的父节点表。
 * 输出：
 *   - 返回 start -> meeting -> target 的完整路径；父节点缺失时返回空路径。
 * 关键逻辑：
 *   - 起点侧父节点需要反转，终点侧父节点天然指向 target，因此两段在 meeting 处拼接。
 */
std::vector<Position> mergeMeetPath(Position start, Position target, Position meeting,
                                    const std::map<Position, Position> &fromStart,
                                    const std::map<Position, Position> &fromTarget)
{
    std::vector<Position> left;
    for (Position pos = meeting; pos != kInvalid; pos = fromStart.count(pos) ? fromStart.at(pos) : kInvalid) {
        left.push_back(pos);
        if (pos == start) break;
    }
    if (left.empty() || left.back() != start) return {};
    std::reverse(left.begin(), left.end());

    std::vector<Position> right;
    for (Position pos = meeting; pos != target;) {
        const auto it = fromTarget.find(pos);
        if (it == fromTarget.end()) return {};
        pos = it->second;
        right.push_back(pos);
    }

    left.insert(left.end(), right.begin(), right.end());
    return left;
}

/**
 * 功能：使用分治思想求两点路径。
 * 输入：
 *   - start：路径起点，必须可通行。
 *   - target：路径终点，必须可通行。
 *   - isWalkable：可通行性函数，用于适配完整迷宫和局部 known_map。
 * 输出：
 *   - 返回 start 到 target 的路径；不可达时返回空路径。
 * 关键逻辑：
 *   - 将路径搜索拆成“从起点出发”和“从终点出发”两个子问题。
 *   - 每次扩展当前规模较小的一侧 frontier，等两侧在某个格子相遇后合并两段子路径。
 *   - 该实现不调用 Dijkstra，只在无权网格上做双向会合搜索。
 */
template <typename Walkable>
std::vector<Position> divideMeetPath(Position start, Position target, Walkable isWalkable)
{
    if (!isWalkable(start) || !isWalkable(target)) return {};
    if (start == target) return {start};

    std::queue<Position> startQueue;
    std::queue<Position> targetQueue;
    std::set<Position> startVisited;
    std::set<Position> targetVisited;
    std::map<Position, Position> fromStart;
    std::map<Position, Position> fromTarget;

    startQueue.push(start);
    targetQueue.push(target);
    startVisited.insert(start);
    targetVisited.insert(target);
    fromStart[start] = kInvalid;
    fromTarget[target] = kInvalid;

    auto expandOneLevel = [&](std::queue<Position> &queue, std::set<Position> &ownVisited,
                              const std::set<Position> &otherVisited, std::map<Position, Position> &ownParent,
                              bool) -> Position {
        const size_t levelCount = queue.size();
        for (size_t i = 0; i < levelCount; ++i) {
            const Position current = queue.front();
            queue.pop();
            for (const auto [dr, dc] : kDirs) {
                const Position next{current.first + dr, current.second + dc};
                if (ownVisited.count(next) || !isWalkable(next)) continue;
                ownVisited.insert(next);
                ownParent[next] = current;
                if (otherVisited.count(next)) return next;
                queue.push(next);
            }
        }
        return kInvalid;
    };

    while (!startQueue.empty() && !targetQueue.empty()) {
        const bool expandStart = startQueue.size() <= targetQueue.size();
        const Position meeting = expandStart
                                     ? expandOneLevel(startQueue, startVisited, targetVisited, fromStart, true)
                                     : expandOneLevel(targetQueue, targetVisited, startVisited, fromTarget, false);
        if (meeting != kInvalid) return mergeMeetPath(start, target, meeting, fromStart, fromTarget);
    }
    return {};
}

} // namespace

/**
 * 功能：分治策略入口。
 * 输入：
 *   - data：迷宫数据。
 *   - start：路径起点。
 *   - target：路径终点。
 * 输出：
 *   - 返回从 start 到 target 的路径。
 * 关键逻辑：
 *   - 将完整迷宫路由拆成起点侧和终点侧两个搜索子问题，搜索相遇后合并路径。
 */
std::vector<Position> divideConquerPath(const MazeData &data, Position start, Position target)
{
    return divideMeetPath(start, target, [&](Position pos) { return mazeWalkable(data, pos); });
}

/**
 * 功能：分治策略的局部路由入口。
 * 输入：
 *   - localMap：AI 当前已观察的局部记忆地图。
 *   - start：局部路径起点。
 *   - target：reward 层已经选出的局部目标。
 * 输出：
 *   - 返回 start 到 target 的局部路径；不可达时返回空路径。
 * 关键逻辑：
 *   - 只在 localMap 已观察、非墙格子上做双向会合搜索。
 *   - reward 层负责选择目标，本函数只把当前点路由到该目标。
 */
std::vector<Position> divideConquerPath(const LocalKnownMap &localMap, Position start, Position target)
{
    return divideMeetPath(start, target, [&](Position pos) { return localMap.isWalkableForPlanning(pos); });
}

} // namespace ai_player
