#ifndef POCKET_AWARE_GREEDY_H
#define POCKET_AWARE_GREEDY_H

#include <string>
#include <vector>

#include "Reward.h"

namespace ai_player {

struct PocketCandidateDebug {
    Position target{kInvalid};
    Position realTarget{kInvalid};
    int pathLength = 0;
    int deltaR = 0;
    double baseScore = 0.0;
    double ownIproxy = 0.0;
    Position bestRemainingTarget{kInvalid};
    Position realBestRemainingTarget{kInvalid};
    double bestRemainingIproxy = 0.0;
    double remainI = 0.0;
    double scoreFirst = 0.0;
    bool selected = false;
};

struct PocketDebug {
    bool enabled = false;
    Position pocketHub{kInvalid};
    Position realPocketHub{kInvalid};
    std::vector<Position> pocketResources;
    std::vector<Position> realPocketResources;
    std::vector<PocketCandidateDebug> candidates;
    Position chosenPocketTarget{kInvalid};
    Position realChosenPocketTarget{kInvalid};
    std::string reason;
};

struct PocketDecision {
    bool enabled = false;
    Position target{kInvalid};
    std::vector<Position> path;
    double score = -1e18;
    PocketDebug debug;
};

/**
 * 功能：在局部资源口袋中选择本轮第一个金币目标。
 * 输入：
 *   - localCurrent：AI 当前局部坐标。
 *   - context：当前资源、步数和出口路径上下文。
 *   - localMap：AI 已知局部地图，只包含 3x3 逐步观察到的信息。
 *   - poseEstimator：现有 Iproxy 计算所需对象；本函数不读取真实迷宫。
 *   - evaluator：复用当前 reward 的路径收益、Iproxy 和 qEff 逻辑。
 * 输出：
 *   - 返回 pocket 是否启用、选中的第一个 target、到该 target 的路径和调试信息。
 * 关键逻辑：
 *   - 只决定第一个 target，不生成完整 pocket 清理路线；到达后由实时策略下一轮重新识别 pocket。
 */
PocketDecision choosePocketFirstTarget(Position localCurrent,
                                       const PathValueContext &context,
                                       const LocalKnownMap &localMap,
                                       const MapPoseEstimator &poseEstimator,
                                       const PathValueEvaluator &evaluator);

} // namespace ai_player

#endif
