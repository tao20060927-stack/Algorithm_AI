#ifndef ASTAR_STRATEGY_H
#define ASTAR_STRATEGY_H

#include "GameTypes.h"

namespace ai_player {
class LocalKnownMap;

std::vector<Position> astarPath(const MazeData &data, Position start, Position target);
std::vector<Position> astarPath(const LocalKnownMap &localMap, Position start, Position target);
} // namespace ai_player

#endif
