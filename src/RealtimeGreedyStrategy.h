#ifndef REALTIME_GREEDY_STRATEGY_H
#define REALTIME_GREEDY_STRATEGY_H

#include <string>
#include <vector>

#include "ClosedSingletonLookaheadGate.h"
#include "PocketAwareGreedy.h"
#include "Reward.h"

namespace ai_player {

// 单个候选目标的评分分解调试信息，前端监控页展示每一步所有候选的 reward 明细
struct GreedyCandidateDebug {
    Position localTarget{kInvalid};        // 候选目标的局部坐标
    Position realTarget{kInvalid};         // 候选目标的真实坐标（由 localToReal_ 映射）
    std::string tile;                      // 目标格子类型："G"/"T"/" "/"E"/"B"
    double score = 0.0;                    // 主 reward 评分 Score(t)
    int deltaR = 0;                        // 路径真实资源变化 dR
    double informationProxy = 0.0;         // 信息价值 I_proxy(t)
    double tailGain = 0.0;                // 边际尾部金币价值 V_tail^marg(t)
    double qEff = 0.0;                     // 当前 q_eff 值
    int pathLength = 0;                    // 路径步数 len(path_t)
    int projectedResource = 0;             // 走完路径后的预计资源 R + dR
    std::vector<int> unknownComponents;    // 与目标 3x3 视野接触的未知连通块大小列表
    int unknownComponentSum = 0;           // 上述列表的总和（用于排序和调试）
    bool selected = false;                 // 本轮是否被 AI 选中为目标
};

// 被拒绝的候选调试信息，记录为什么该候选无法参与评分
struct GreedyRejectedDebug {
    Position localTarget{kInvalid};    // 候选的局部坐标
    Position realTarget{kInvalid};     // 候选的真实坐标
    std::string tile;                  // 格子类型
    std::string reason;                // 拒绝原因（如 "blocked""infeasible""dead node"）
    int pathLength = 0;               // 路径步数（如有）
};

// 单步决策的完整调试信息，前端逐步播放时展示
struct GreedyStepDebug {
    int step = 0;                                        // 当前步编号（从起点 step=0 开始）
    Position localCurrent{kInvalid};                     // 当前局部坐标
    Position realCurrent{kInvalid};                      // 当前真实坐标
    double alpha = 0.0;                                  // 当前平滑 α_smooth
    double observedRatio = 0.0;                          // 已观察比例（estimatedObservedCount / 225）
    double qEff = 0.0;                                   // 当前 q_eff
    std::string decision;                                // 决策类型："best-target""hold-target""exit""fallback""pocket-first-target" 等
    Position selectedLocal{kInvalid};                    // 最终选中的局部目标
    Position selectedReal{kInvalid};                     // 最终选中的真实目标
    std::vector<GreedyCandidateDebug> candidates;        // 所有参与评分的候选及分数明细
    std::vector<GreedyRejectedDebug> rejected;           // 被拒绝的候选及原因
    ClosedSingletonGateDebug closedSingletonGate;        // Closed Singleton Gate 的本步执行结果
    PocketDebug pocket;                                  // Pocket-Aware Greedy 的本步执行结果
};

// 贪心算法的完整运行结果，包含路径和每步调试信息
struct GreedyRunResult {
    std::vector<Position> path;                 // 从起点到终点（或 GameOver 点）的真实坐标路径
    std::vector<GreedyStepDebug> debugSteps;    // 每一步的决策调试信息（与 path 一一对应）
    bool gameOver = false;                      // 是否因 Boss 战失败且无法复活而提前结束
};

// 运行 3x3 实时贪心探索，返回真实坐标路径
// routeAlgorithm 控制 reward 选定目标后使用哪种路由算法（默认 "greedy"=BFS）
std::vector<Position> realtimeGreedyPath(const MazeData &data, const std::string &routeAlgorithm = "greedy",
                                         RewardParameters parameters = {});

// 运行 3x3 实时贪心探索，返回路径 + 每步调试信息
GreedyRunResult realtimeGreedyRun(const MazeData &data, const std::string &routeAlgorithm = "greedy",
                                  RewardParameters parameters = {});
} // namespace ai_player

#endif
