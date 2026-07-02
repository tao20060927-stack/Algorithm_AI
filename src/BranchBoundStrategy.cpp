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
    // 起点或终点不可通行，直接返回空路径。
    if (!isWalkable(start) || !isWalkable(target)) return {};
    // 起点等于终点时无需搜索，直接返回单点路径。
    if (start == target) return {start};

    // currentPath 维护当前递归栈上从 start 到当前节点的路径序列。
    std::vector<Position> currentPath{start};
    // bestPath 记录迄今为止找到的最短完整路径。
    std::vector<Position> bestPath;
    // visited 记录当前路径上已走过的格子，用于避免回路（每个格子只能走一次）。
    std::set<Position> visited{start};
    // bestLength 是当前最优解的长度，初始为 INT_MAX 表示未找到任何解。
    // 分支限界的核心：任何分支的"已走步数 + 下界估计"不小于 bestLength 时都可以剪枝。
    int bestLength = INT_MAX;

    std::function<void(Position)> dfs = [&](Position current) {
        // currentLength 是从 start 到 current 已走的实际步数（不含起点本身）。
        const int currentLength = static_cast<int>(currentPath.size()) - 1;
        // 剪枝判断：若"已走步数 + 曼哈顿距离下界"已经不小于当前最优解长度，
        // 继续走这条路不可能得到更短的路径，直接回溯。
        if (currentLength + manhattan(current, target) >= bestLength) return;
        // 到达目标格子，更新当前最优解。
        if (current == target) {
            bestLength = currentLength;
            bestPath = currentPath;
            return;
        }

        // 收集当前格子的所有合法分支（邻居）。
        std::vector<Position> branches;
        for (const auto [dr, dc] : kDirs) {
            const Position next{current.first + dr, current.second + dc};
            // 已经访问过或不可通行的邻居不加入分支列表，避免重复走和撞墙。
            if (visited.count(next) || !isWalkable(next)) continue;
            branches.push_back(next);
        }
        // 将分支按曼哈顿距离升序排列：优先扩展离目标"看起来近"的分支。
        // 这样做的目的是让搜索尽早找到一个可行解（上界），从而更有效地剪掉后续分支。
        std::sort(branches.begin(), branches.end(), [&](Position lhs, Position rhs) {
            return manhattan(lhs, target) < manhattan(rhs, target);
        });

        // 按启发式排序后的顺序递归扩展每个分支。
        for (const auto &next : branches) {
            visited.insert(next);        // 标记为已访问，避免在更深层递归中形成回路。
            currentPath.push_back(next); // 将邻居加入当前路径。
            dfs(next);                   // 递归搜索该分支。
            currentPath.pop_back();      // 回溯：撤销路径中的这一步。
            visited.erase(next);         // 回溯：取消访问标记。
        }
    };

    // 从起点开始深度优先搜索。
    dfs(start);
    // 如果 bestPath 仍然为空，说明在搜索空间中未找到任何可达路径。
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
    // 将迷宫格子可通行性判断适配为 branchBoundSearch 所需的 Walkable 可调用对象。
    // mazeWalkable 把墙"#"视为不可通行，Boss 格按普通可通行格处理。
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
    // 在局部已知地图上做分支限界搜索。
    // isWalkableForPlanning 只把已观察且非墙的格子标记为可通行。
    return branchBoundSearch(start, target, [&](Position pos) { return localMap.isWalkableForPlanning(pos); });
}

} // namespace ai_player
