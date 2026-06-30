#ifndef DIVIDE_CONQUER_STRATEGY_H
#define DIVIDE_CONQUER_STRATEGY_H

#include "GameTypes.h"

namespace ai_player {
class LocalKnownMap;

std::vector<Position> divideConquerPath(const MazeData &data, Position start, Position target);
std::vector<Position> divideConquerPath(const LocalKnownMap &localMap, Position start, Position target);
} // namespace ai_player

#endif
