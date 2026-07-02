#ifndef DIVIDE_CONQUER_STRATEGY_H
#define DIVIDE_CONQUER_STRATEGY_H

#include "GameTypes.h"

namespace ai_player {
class LocalKnownMap;    // 前向声明，用于局部已知地图版本的分治法重载

// 在全局迷宫上使用分治法求两点最短路径
std::vector<Position> divideConquerPath(const MazeData &data, Position start, Position target);

// 在 AI 的局部已知地图上使用分治法求两点路径（不读取隐藏地图内容）
std::vector<Position> divideConquerPath(const LocalKnownMap &localMap, Position start, Position target);

} // namespace ai_player

#endif
