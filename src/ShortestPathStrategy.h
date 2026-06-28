#ifndef SHORTEST_PATH_STRATEGY_H
#define SHORTEST_PATH_STRATEGY_H

#include <string>
#include <vector>

#include "GameTypes.h"

namespace ai_player {
std::vector<Position> shortestPath(const MazeData &data, Position start, Position target, const std::string &algorithm);
std::vector<Position> planAdventurePath(const MazeData &data, const std::string &algorithm);
} // namespace ai_player

#endif
