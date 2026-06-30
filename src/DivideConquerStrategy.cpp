#include "DivideConquerStrategy.h"

#include "DijkstraStrategy.h"

namespace ai_player {

/**
 * 功能：分治策略入口。
 * 输入：
 *   - data：迷宫数据。
 *   - start：路径起点。
 *   - target：路径终点。
 * 输出：
 *   - 返回从 start 到 target 的路径。
 * 关键逻辑：
 *   - 第一版以 Dijkstra 作为稳定可达性基线；后续实现真正分治拆区、合并子路径时只需要替换本文件。
 */
std::vector<Position> divideConquerPath(const MazeData &data, Position start, Position target)
{
    return dijkstraPath(data, start, target);
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
 *   - 分治在第一版中复用 Dijkstra 作为子问题可达性基线。
 *   - 本函数不做完整探索，只在 reward 选定目标后求两点路径。
 */
std::vector<Position> divideConquerPath(const LocalKnownMap &localMap, Position start, Position target)
{
    return dijkstraPath(localMap, start, target);
}

} // namespace ai_player
