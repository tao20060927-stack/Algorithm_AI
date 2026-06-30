#include "BranchBoundStrategy.h"

#include "AStarStrategy.h"

namespace ai_player {

/**
 * 功能：分支限界策略入口。
 * 输入：
 *   - data：迷宫数据。
 *   - start：路径起点。
 *   - target：路径终点。
 * 输出：
 *   - 返回从 start 到 target 的路径。
 * 关键逻辑：
 *   - 第一版以 A* 的 g+h 优先队列作为分支限界基线；后续调试分支状态和剪枝上界时只需要替换本文件。
 */
std::vector<Position> branchBoundPath(const MazeData &data, Position start, Position target)
{
    return astarPath(data, start, target);
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
 *   - 分支限界在第一版中复用 A* 的 g+h 优先队列作为限界基线。
 *   - 本函数不参与目标选择，只在 reward 已确定目标后负责路由。
 */
std::vector<Position> branchBoundPath(const LocalKnownMap &localMap, Position start, Position target)
{
    return astarPath(localMap, start, target);
}

} // namespace ai_player
