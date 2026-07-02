#ifndef ASTAR_STRATEGY_H
#define ASTAR_STRATEGY_H

#include "GameTypes.h"

namespace ai_player {
class LocalKnownMap;    // 前向声明，用于局部已知地图版本的 A* 重载

// 在全局迷宫上使用 A*（曼哈顿启发式）求两点最短路径
std::vector<Position> astarPath(const MazeData &data, Position start, Position target);

// 在 AI 的局部已知地图上使用 A* 求两点路径（不读取隐藏地图内容）
std::vector<Position> astarPath(const LocalKnownMap &localMap, Position start, Position target);

} // namespace ai_player

#endif
