#ifndef SHORTEST_PATH_STRATEGY_H
#define SHORTEST_PATH_STRATEGY_H

#include <string>
#include <vector>

#include "GameTypes.h"

namespace ai_player {

// 根据算法名称在全局迷宫中求两点最短路径；支持 smart/dijkstra/astar/branch_bound/divide_conquer
std::vector<Position> shortestPath(const MazeData &data, Position start, Position target, const std::string &algorithm);

// 使用全局探险策略规划完整路径：访问所有金币和目标事件后走向出口
// smart 是完整探险入口；其他算法是 reward 选目标后的路由器，不应在此独立调用
std::vector<Position> planAdventurePath(const MazeData &data, const std::string &algorithm);

} // namespace ai_player

#endif
