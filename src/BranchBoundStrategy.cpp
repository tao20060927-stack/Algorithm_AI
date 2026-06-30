#include "BranchBoundStrategy.h"

#include <algorithm>
#include <climits>
#include <functional>
#include <set>

#include "Reward.h"

namespace ai_player {
namespace {

/**
 * 功能：判断完整迷宫中的格子是否可用于分支限界路由。
 * 输入：
 *   - data：完整迷宫数据，要求 grid 非空且各行等长。
 *   - pos：待判断的真实坐标，可以越界。
 * 输出：
 *   - 返回该坐标是否在迷宫内且不是墙。
 * 关键逻辑：
 *   - Boss 本体不再作为障碍，路由层只把墙视为不可通行。
 */
bool mazeWalkable(const MazeData &data, Position pos)
{
    return pos.first >= 0 && pos.second >= 0 && pos.first < static_cast<int>(data.grid.size()) &&
           pos.second < static_cast<int>(data.grid[0].size()) && data.grid[pos.first][pos.second] != "#";
}

/**
 * 功能：计算两个格子的曼哈顿距离下界。
 * 输入：
 *   - a：当前格子。
 *   - b：目标格子。
 * 输出：
 *   - 返回从 a 到 b 在四方向网格中的理论最短距离下界。
 * 关键逻辑：
 *   - 分支限界用该值估计“当前路径长度 + 未来至少还要走的步数”，从而剪掉不可能优于当前最优解的分支。
 */
int manhattan(Position a, Position b)
{
    return std::abs(a.first - b.first) + std::abs(a.second - b.second);
}

/**
 * 功能：使用分支限界法求两点路径。
 * 输入：
 *   - start：路径起点，必须可通行。
 *   - target：路径终点，必须可通行。
 *   - isWalkable：可通行性函数，用于适配完整迷宫和局部 known_map。
 * 输出：
 *   - 返回 start 到 target 的最短路径；不可达时返回空路径。
 * 关键逻辑：
 *   - 每个递归状态是一条从 start 到当前点的候选路径。
 *   - 分支按曼哈顿距离从小到大扩展，使搜索尽快得到一个可行上界。
 *   - 若“已走步数 + 曼哈顿下界”不优于当前最优路径长度，则剪枝。
 */
template <typename Walkable>
std::vector<Position> branchBoundSearch(Position start, Position target, Walkable isWalkable)
{
    if (!isWalkable(start) || !isWalkable(target)) return {};
    if (start == target) return {start};

    std::vector<Position> currentPath{start};
    std::vector<Position> bestPath;
    std::set<Position> visited{start};
    int bestLength = INT_MAX;

    std::function<void(Position)> dfs = [&](Position current) {
        const int currentLength = static_cast<int>(currentPath.size()) - 1;
        if (currentLength + manhattan(current, target) >= bestLength) return;
        if (current == target) {
            bestLength = currentLength;
            bestPath = currentPath;
            return;
        }

        std::vector<Position> branches;
        for (const auto [dr, dc] : kDirs) {
            const Position next{current.first + dr, current.second + dc};
            if (visited.count(next) || !isWalkable(next)) continue;
            branches.push_back(next);
        }
        std::sort(branches.begin(), branches.end(), [&](Position lhs, Position rhs) {
            return manhattan(lhs, target) < manhattan(rhs, target);
        });

        for (const auto &next : branches) {
            visited.insert(next);
            currentPath.push_back(next);
            dfs(next);
            currentPath.pop_back();
            visited.erase(next);
        }
    };

    dfs(start);
    return bestPath;
}

} // namespace

/**
 * 功能：分支限界策略入口。
 * 输入：
 *   - data：迷宫数据。
 *   - start：路径起点。
 *   - target：路径终点。
 * 输出：
 *   - 返回从 start 到 target 的路径。
 * 关键逻辑：
 *   - 使用路径状态树进行搜索，并用曼哈顿距离作为下界剪枝。
 */
std::vector<Position> branchBoundPath(const MazeData &data, Position start, Position target)
{
    return branchBoundSearch(start, target, [&](Position pos) { return mazeWalkable(data, pos); });
}

/**
 * 功能：分支限界策略的局部路由入口。
 * 输入：
 *   - localMap：AI 当前已观察的局部记忆地图。
 *   - start：局部路径起点。
 *   - target：reward 层已经选出的局部目标。
 * 输出：
 *   - 返回 start 到 target 的局部路径；不可达时返回空路径。
 * 关键逻辑：
 *   - 在已观察局部地图上展开路径分支，使用“已走长度 + 曼哈顿下界”剪枝。
 *   - 本函数不参与目标选择，只在 reward 已确定目标后负责两点路由。
 */
std::vector<Position> branchBoundPath(const LocalKnownMap &localMap, Position start, Position target)
{
    return branchBoundSearch(start, target, [&](Position pos) { return localMap.isWalkableForPlanning(pos); });
}

} // namespace ai_player
