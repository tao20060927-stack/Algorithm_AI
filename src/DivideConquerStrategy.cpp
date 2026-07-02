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
    // 从相遇点 meeting 出发，沿起点侧的父节点链回溯到 start。
    // fromStart 的父节点方向是 child -> parent（指向 start 方向）。
    std::vector<Position> left;
    for (Position pos = meeting; pos != kInvalid; pos = fromStart.count(pos) ? fromStart.at(pos) : kInvalid) {
        left.push_back(pos);
        if (pos == start) break;
    }
    // 回溯结束后的 left 是 meeting -> ... -> start 的逆序。
    // 如果 left 的最后一个元素不是 start，说明父节点链断裂，返回空路径。
    if (left.empty() || left.back() != start) return {};
    // 反转 left 得到 start -> ... -> meeting 的正向路径（不含 meeting 之后的点）。
    std::reverse(left.begin(), left.end());

    // 从相遇点 meeting 出发，沿终点侧的父节点链向 target 方向前进。
    // fromTarget 的父节点方向也是 child -> parent（指向 target 方向）。
    std::vector<Position> right;
    for (Position pos = meeting; pos != target;) {
        const auto it = fromTarget.find(pos);
        // 如果某一步找不到父节点，说明 fromTarget 中路径不完整。
        if (it == fromTarget.end()) return {};
        pos = it->second;
        right.push_back(pos);
    }

    // 拼接：left（start -> meeting） + right（meeting 的下一个 -> ... -> target）。
    // 注意 right 不包含 meeting 本身（meeting 在 left 末尾已经存在）。
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
    // 起点或终点不可通行则无法搜索。
    if (!isWalkable(start) || !isWalkable(target)) return {};
    // 起点等于终点，无需搜索。
    if (start == target) return {start};

    // 双向 BFS 的数据结构：从 start 和 target 各维护一个队列和已访问集合。
    std::queue<Position> startQueue;
    std::queue<Position> targetQueue;
    std::set<Position> startVisited;
    std::set<Position> targetVisited;
    // fromStart / fromTarget 记录各自搜索侧的父节点关系，方向均为 child -> parent。
    std::map<Position, Position> fromStart;
    std::map<Position, Position> fromTarget;

    // 初始化两端搜索的起点状态。
    startQueue.push(start);
    targetQueue.push(target);
    startVisited.insert(start);
    targetVisited.insert(target);
    fromStart[start] = kInvalid;   // start 没有父节点。
    fromTarget[target] = kInvalid; // target 没有父节点。

    // 扩展一整层的 lambda：每次把队列中当前层的所有节点一次性扩展完。
    // 这样实现了"按层扩展"，而不是逐个节点扩展，便于选择较小的一侧扩展以平衡搜索。
    auto expandOneLevel = [&](std::queue<Position> &queue, std::set<Position> &ownVisited,
                              const std::set<Position> &otherVisited, std::map<Position, Position> &ownParent,
                              bool) -> Position {
        // 当前层中有多少个节点，就只扩展这一层，不扩展新加入的节点。
        const size_t levelCount = queue.size();
        for (size_t i = 0; i < levelCount; ++i) {
            const Position current = queue.front();
            queue.pop();
            // 在四方向上扩展邻居。
            for (const auto [dr, dc] : kDirs) {
                const Position next{current.first + dr, current.second + dc};
                // 跳过已访问和不可通行的格子。
                if (ownVisited.count(next) || !isWalkable(next)) continue;
                // 标记为已访问并记录父节点（current -> next 方向）。
                ownVisited.insert(next);
                ownParent[next] = current;
                // 如果新扩展的格子出现在对面的已访问集合中，说明两端搜索在此相遇。
                if (otherVisited.count(next)) return next;
                // 将该格子加入队列，等待下一层扩展。
                queue.push(next);
            }
        }
        // 本层没有相遇，返回无效坐标。
        return kInvalid;
    };

    // 分治核心循环：每次选择队列较小的一侧扩展一层。
    // 这样做是为了平衡两端的搜索规模，减少总扩展节点数。
    while (!startQueue.empty() && !targetQueue.empty()) {
        // 比较两端队列大小，选择节点较少的一侧进行本轮的层级扩展。
        const bool expandStart = startQueue.size() <= targetQueue.size();
        const Position meeting = expandStart
                                     ? expandOneLevel(startQueue, startVisited, targetVisited, fromStart, true)
                                     : expandOneLevel(targetQueue, targetVisited, startVisited, fromTarget, false);
        // 如果两端搜索在某格子相遇，合并两段子路径并返回完整路径。
        if (meeting != kInvalid) return mergeMeetPath(start, target, meeting, fromStart, fromTarget);
    }
    // 某一端队列已空但尚未相遇，说明起点和终点在搜索空间内不连通。
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
    // 将完整迷宫的可通行性判断适配为 divideMeetPath 的 Walkable 回调。
    // mazeWalkable 仅把"#"墙视为障碍，Boss 格可正常通过。
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
    // 在局部已知地图上做双向会合搜索。
    // isWalkableForPlanning 限制搜索范围在已观察且非墙的格子内。
    return divideMeetPath(start, target, [&](Position pos) { return localMap.isWalkableForPlanning(pos); });
}

} // namespace ai_player
