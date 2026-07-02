#ifndef POCKET_AWARE_GREEDY_H
#define POCKET_AWARE_GREEDY_H

#include <string>
#include <vector>

#include "Reward.h"

namespace ai_player {

// 单个 pocket 金币候选的评分分解调试信息
struct PocketCandidateDebug {
    Position target{kInvalid};             // 候选金币的局部坐标
    Position realTarget{kInvalid};         // 候选金币的真实坐标
    int pathLength = 0;                    // 从当前位置到该金币的路径步数
    int deltaR = 0;                        // 路径真实资源变化 dR
    double baseScore = 0.0;                // baseScore = dR − η_q × q_eff × len（不含 I_proxy 和 tail）
    double ownIproxy = 0.0;               // 该金币自身的 I_proxy 值
    Position bestRemainingTarget{kInvalid};// remainI 最高的剩余金币坐标
    Position realBestRemainingTarget{kInvalid}; // 上述剩余金币的真实坐标
    double bestRemainingIproxy = 0.0;     // 上述剩余金币的 I_proxy 值
    double remainI = 0.0;                 // = max( I_proxy(remaining) / (1+μ×dist) )，留到最后的探索保留价值
    double scoreFirst = 0.0;              // = baseScore + λ_remain × remainI，先吃此金币的综合评分
    bool selected = false;                // 本轮是否被选中为第一目标
};

// Pocket 操作的完整调试信息
struct PocketDebug {
    bool enabled = false;                          // 本轮是否触发了 Pocket 模式
    Position pocketHub{kInvalid};                  // 选中的 hub 局部坐标
    Position realPocketHub{kInvalid};              // hub 的真实坐标
    std::vector<Position> pocketResources;         // pocket 内所有金币的局部坐标
    std::vector<Position> realPocketResources;    // 上述金币的真实坐标
    std::vector<PocketCandidateDebug> candidates;  // 所有候选的评分明细
    Position chosenPocketTarget{kInvalid};         // 最终选中的第一个目标（局部坐标）
    Position realChosenPocketTarget{kInvalid};     // 最终选中的第一个目标（真实坐标）
    std::string reason;                            // 决策原因描述
};

// Pocket-Aware Greedy 的决策结果
struct PocketDecision {
    bool enabled = false;             // 是否触发（至少 2 个金币在 pocketRadius 内）
    Position target{kInvalid};        // 选中的第一个目标
    std::vector<Position> path;       // 到该目标的已知地图路径
    double score = -1e18;             // 选中目标的 Score_first
    PocketDebug debug;                // 完整调试信息
};

// 在局部资源口袋中选择本轮第一个金币目标
PocketDecision choosePocketFirstTarget(Position localCurrent,
                                       const PathValueContext &context,
                                       const LocalKnownMap &localMap,
                                       const MapPoseEstimator &poseEstimator,
                                       const PathValueEvaluator &evaluator);

} // namespace ai_player

#endif
