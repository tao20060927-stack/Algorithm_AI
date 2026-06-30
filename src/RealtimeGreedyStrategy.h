#ifndef REALTIME_GREEDY_STRATEGY_H
#define REALTIME_GREEDY_STRATEGY_H

#include <string>
#include <vector>

#include "Reward.h"

namespace ai_player {
struct GreedyCandidateDebug {
    Position localTarget{kInvalid};
    Position realTarget{kInvalid};
    std::string tile;
    double score = 0.0;
    int deltaR = 0;
    double informationProxy = 0.0;
    double tailGain = 0.0;
    double qEff = 0.0;
    int pathLength = 0;
    double marginPenalty = 0.0;
    int projectedResource = 0;
    std::vector<int> unknownComponents;
    int unknownComponentSum = 0;
    bool selected = false;
};

struct GreedyRejectedDebug {
    Position localTarget{kInvalid};
    Position realTarget{kInvalid};
    std::string tile;
    std::string reason;
    int pathLength = 0;
};

struct GreedyStepDebug {
    int step = 0;
    Position localCurrent{kInvalid};
    Position realCurrent{kInvalid};
    double alpha = 0.0;
    double observedRatio = 0.0;
    double qEff = 0.0;
    std::string decision;
    Position selectedLocal{kInvalid};
    Position selectedReal{kInvalid};
    std::vector<GreedyCandidateDebug> candidates;
    std::vector<GreedyRejectedDebug> rejected;
};

struct GreedyRunResult {
    std::vector<Position> path;
    std::vector<GreedyStepDebug> debugSteps;
};

std::vector<Position> realtimeGreedyPath(const MazeData &data, const std::string &routeAlgorithm = "greedy",
                                         RewardParameters parameters = {});
GreedyRunResult realtimeGreedyRun(const MazeData &data, const std::string &routeAlgorithm = "greedy",
                                  RewardParameters parameters = {});
} // namespace ai_player

#endif
