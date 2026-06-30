#ifndef DIJKSTRA_STRATEGY_H
#define DIJKSTRA_STRATEGY_H

#include "GameTypes.h"

namespace ai_player {
class LocalKnownMap;

std::vector<Position> dijkstraPath(const MazeData &data, Position start, Position target);
std::vector<Position> dijkstraPath(const LocalKnownMap &localMap, Position start, Position target);
} // namespace ai_player

#endif
