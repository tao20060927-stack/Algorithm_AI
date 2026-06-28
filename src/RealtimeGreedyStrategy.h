#ifndef REALTIME_GREEDY_STRATEGY_H
#define REALTIME_GREEDY_STRATEGY_H

#include <vector>

#include "GameTypes.h"

namespace ai_player {
std::vector<Position> realtimeGreedyPath(const MazeData &data);
} // namespace ai_player

#endif
