#ifndef BRANCH_BOUND_STRATEGY_H
#define BRANCH_BOUND_STRATEGY_H

#include "GameTypes.h"

namespace ai_player {
class LocalKnownMap;

std::vector<Position> branchBoundPath(const MazeData &data, Position start, Position target);
std::vector<Position> branchBoundPath(const LocalKnownMap &localMap, Position start, Position target);
} // namespace ai_player

#endif
