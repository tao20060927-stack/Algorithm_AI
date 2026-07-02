#ifndef CLOSED_SINGLETON_LOOKAHEAD_GATE_H
#define CLOSED_SINGLETON_LOOKAHEAD_GATE_H

#include "Reward.h"

#include <string>
#include <vector>

namespace ai_player {

struct ClosedSingletonGateCandidate {
    Position target{kInvalid};
    std::string tile;
    double score = -1e18;
    int deltaR = 0;
    double informationProxy = 0.0;
    double tailGain = 0.0;
    int pathLength = 0;
    int projectedResource = 0;
    int unknownComponentSum = 0;
    std::vector<int> unknownComponents;
    std::vector<Position> path;
};

struct ClosedSingletonGateDebug {
    bool checked = false;
    bool triggered = false;
    Position candidateA{kInvalid};
    std::string candidateAType;
    int componentSizeA = 0;
    bool isClosedSingletonA = false;
    double rewardA = 0.0;
    Position bestNonClosedB{kInvalid};
    double rewardB = 0.0;
    bool simulatedAfterA = false;
    Position bestAfterATarget{kInvalid};
    double bestAfterAReward = -1e18;
    double gamma = 1.0;
    double margin = 0.0;
    double combinedA = -1e18;
    bool allowed = false;
    std::string reason;
    std::string disabledReason;
};

struct ClosedSingletonGateResult {
    bool hasSelection = false;
    bool changed = false;
    Position selectedTarget{kInvalid};
    std::vector<Position> selectedPath;
    double selectedScore = -1e18;
    ClosedSingletonGateDebug debug;
};

struct ClosedSingletonGateRequest {
    Position localCurrent{kInvalid};
    Position localExit{kInvalid};
    PathValueContext context;
    const LocalKnownMap *localMap = nullptr;
    const MapPoseEstimator *poseEstimator = nullptr;
    const PathValueEvaluator *evaluator = nullptr;
    std::vector<ClosedSingletonGateCandidate> candidates;
};

/**
 * 功能：对当前最高分的 |C|=1 封闭小候选执行非对称 memory-only lookahead gate。
 * 输入：
 *   - request：当前局部位置、known_map、评分器、上下文和已完成评分的候选队列。
 * 输出：
 *   - 返回 gate 是否触发、是否改选目标、最终目标以及调试信息。
 * 关键逻辑：
 *   - 只模拟封闭小节点 A；开放候选 B 不做后继 rollout，避免对未知区域做不可靠预测。
 */
ClosedSingletonGateResult applyClosedSingletonLookaheadGate(const ClosedSingletonGateRequest &request);

} // namespace ai_player

#endif
