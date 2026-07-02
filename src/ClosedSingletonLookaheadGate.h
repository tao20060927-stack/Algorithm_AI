#ifndef CLOSED_SINGLETON_LOOKAHEAD_GATE_H
#define CLOSED_SINGLETON_LOOKAHEAD_GATE_H

#include "Reward.h"

#include <string>
#include <vector>

namespace ai_player {

// Closed Singleton Gate 处理的候选，来自已完成 reward 评分的正常候选队列
struct ClosedSingletonGateCandidate {
    Position target{kInvalid};             // 候选目标局部坐标
    std::string tile;                      // 目标格子类型
    double score = -1e18;                  // 正常 reward 评分（进入 gate 前的原始分数）
    int deltaR = 0;                        // 路径真实资源变化
    double informationProxy = 0.0;         // 信息价值 I_proxy
    double tailGain = 0.0;                 // 尾部金币价值 V_tail
    int pathLength = 0;                    // 路径步数
    int projectedResource = 0;             // 预计走完后资源
    int unknownComponentSum = 0;           // |C| 总和（=1 表示 closed singleton）
    std::vector<int> unknownComponents;    // 各连通块大小
    std::vector<Position> path;            // 从当前位置到目标的已知地图路径
};

// Closed Singleton Gate 的执行调试信息
struct ClosedSingletonGateDebug {
    bool checked = false;              // 是否执行了 gate 检查
    bool triggered = false;            // 是否满足触发条件（top-1 是 closed singleton）
    Position candidateA{kInvalid};    // 候选 A 的位置（closed singleton）
    std::string candidateAType;       // A 的格子类型
    int componentSizeA = 0;           // A 的 |C| 值（必须 = 1）
    bool isClosedSingletonA = false;  // A 是否确认为 closed singleton
    double rewardA = 0.0;             // A 的 reward 分数
    Position bestNonClosedB{kInvalid};// 候选 B 的位置（最优非封闭候选）
    double rewardB = 0.0;             // B 的 reward 分数
    bool simulatedAfterA = false;     // 是否成功模拟了 A 之后的 memory 状态
    Position bestAfterATarget{kInvalid}; // c_A 的目标位置
    double bestAfterAReward = -1e18;    // c_A 的 reward 值
    double gamma = 1.0;              // γ：c_A 的权重
    double margin = 0.0;             // margin：拒绝 A 的保守门槛
    double combinedA = -1e18;        // = reward(A) + γ × c_A
    bool allowed = false;            // A 是否通过 gate
    std::string reason;              // 决策原因文本
    std::string disabledReason;      // gate 未触发的原因（如有）
};

// Closed Singleton Gate 的最终决策
struct ClosedSingletonGateResult {
    bool hasSelection = false;             // gate 是否给出了最终目标选择
    bool changed = false;                  // 是否改变了原 top-1（即改为 B）
    Position selectedTarget{kInvalid};     // 最终选中的目标
    std::vector<Position> selectedPath;    // 到最终目标的路径
    double selectedScore = -1e18;          // 最终目标的分数
    ClosedSingletonGateDebug debug;        // 完整调试信息
};

// 进入 gate 的完整请求上下文
struct ClosedSingletonGateRequest {
    Position localCurrent{kInvalid};                          // AI 当前局部坐标
    Position localExit{kInvalid};                             // 出口局部坐标（未知时为 kInvalid）
    PathValueContext context;                                 // 当前评分上下文
    const LocalKnownMap *localMap = nullptr;                  // AI 局部记忆地图
    const MapPoseEstimator *poseEstimator = nullptr;          // 地图姿态估计器
    const PathValueEvaluator *evaluator = nullptr;            // reward 评估器
    std::vector<ClosedSingletonGateCandidate> candidates;     // 已完成评分的候选队列
};

// 对当前 top-1 封闭小候选执行非对称 memory-only lookahead gate
ClosedSingletonGateResult applyClosedSingletonLookaheadGate(const ClosedSingletonGateRequest &request);

} // namespace ai_player

#endif
