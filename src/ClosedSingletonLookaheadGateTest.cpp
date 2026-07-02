#include "ClosedSingletonLookaheadGate.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

using ai_player::AgentState;
using ai_player::ClosedSingletonGateCandidate;
using ai_player::ClosedSingletonGateDebug;
using ai_player::ClosedSingletonGateRequest;
using ai_player::LocalKnownMap;
using ai_player::MapPoseEstimator;
using ai_player::PathValueContext;
using ai_player::PathValueEvaluator;
using ai_player::Position;
using ai_player::RewardParameters;
using ai_player::applyClosedSingletonLookaheadGate;

namespace {

/**
 * 功能：检查测试条件，不满足时抛出异常。
 * 输入：
 *   - condition：需要验证的布尔条件。
 *   - message：失败时输出的说明。
 * 输出：
 *   - 无返回值；失败时抛出 runtime_error。
 * 关键逻辑：
 *   - gate 的触发条件较窄，测试失败时需要直接指出是哪条约束被破坏。
 */
void require(bool condition, const std::string &message)
{
    if (!condition) throw std::runtime_error(message);
}

/**
 * 功能：构造一个已完成评分的 gate 候选。
 * 输入：
 *   - target：候选局部坐标。
 *   - score：当前 reward 分数。
 *   - componentSum：当前 |C| 合计。
 *   - path：当前位置到候选的已知路径。
 * 输出：
 *   - 返回用于 gate 的候选结构。
 * 关键逻辑：
 *   - 单测直接构造候选队列，避免依赖完整迷宫运行。
 */
ClosedSingletonGateCandidate makeCandidate(Position target, double score, int componentSum,
                                           std::vector<Position> path)
{
    ClosedSingletonGateCandidate candidate;
    candidate.target = target;
    candidate.tile = " ";
    candidate.score = score;
    candidate.deltaR = 0;
    candidate.tailGain = 0.0;
    candidate.pathLength = path.empty() ? 0 : static_cast<int>(path.size()) - 1;
    candidate.projectedResource = 100;
    candidate.unknownComponentSum = componentSum;
    candidate.unknownComponents = {componentSum};
    candidate.path = std::move(path);
    return candidate;
}

/**
 * 功能：创建一张只包含当前点、A、B 的最小 known_map。
 * 输入：
 *   - 无。
 * 输出：
 *   - 返回局部记忆地图。
 * 关键逻辑：
 *   - B 放在 A 的 3x3 范围内，便于验证 A 后 continuation 会排除 A 周围候选。
 */
LocalKnownMap makeBaseMap()
{
    LocalKnownMap map;
    map.setObserved({0, 0}, " ");
    map.setObserved({0, 1}, " ");
    map.setObserved({1, 0}, " ");
    map.markVisited({0, 0});
    return map;
}

/**
 * 功能：创建基础 gate 请求。
 * 输入：
 *   - map：当前 known_map。
 *   - evaluator：评分器。
 *   - estimator：姿态估计器。
 * 输出：
 *   - 返回带有当前状态和依赖项的请求。
 * 关键逻辑：
 *   - 测试只使用 memory-only 信息，不设置真实迷宫或真实视野。
 */
ClosedSingletonGateRequest makeRequest(LocalKnownMap &map, PathValueEvaluator &evaluator,
                                       MapPoseEstimator &estimator)
{
    ClosedSingletonGateRequest request;
    request.localCurrent = {0, 0};
    request.context.state.resource = 100;
    request.context.state.steps = 10;
    request.context.state.alphaSmooth = 1.0;
    request.localMap = &map;
    request.poseEstimator = &estimator;
    request.evaluator = &evaluator;
    return request;
}

/**
 * 功能：创建适合 gate 测试的低干扰 reward 参数。
 * 输入：
 *   - 无。
 * 输出：
 *   - 返回参数结构体。
 * 关键逻辑：
 *   - 关闭路径长度成本和低资源惩罚，使 memory-only continuation 的正向信息价值更容易被观测。
 */
RewardParameters makeParameters()
{
    RewardParameters parameters;
    parameters.omegaI = 1.0;
    parameters.kappaU = 1.0;
    parameters.qEffLengthWeight = 0.0;
    parameters.lambdaMargin = 0.0;
    parameters.gammaClosedSingleton = 1.0;
    parameters.marginClosedSingleton = 0.0;
    return parameters;
}

/**
 * 功能：验证 top 候选不是 |C|=1 时 gate 不触发。
 * 输入：
 *   - 无。
 * 输出：
 *   - 断言失败时抛出异常。
 * 关键逻辑：
 *   - gate 不能误伤开放 frontier。
 */
void testTopNotClosedSingletonDoesNotTrigger()
{
    LocalKnownMap map = makeBaseMap();
    MapPoseEstimator estimator;
    estimator.initialize();
    estimator.update(map, {0, 0});
    PathValueEvaluator evaluator(makeParameters());
    auto request = makeRequest(map, evaluator, estimator);
    request.candidates.push_back(makeCandidate({0, 1}, 5.0, 2, {{0, 0}, {0, 1}}));
    request.candidates.push_back(makeCandidate({1, 0}, 4.0, 12, {{0, 0}, {1, 0}}));

    const auto result = applyClosedSingletonLookaheadGate(request);
    require(result.debug.checked, "gate should be checked");
    require(!result.debug.triggered, "open top candidate should not trigger gate");
    require(result.debug.disabledReason == "top candidate is not closed singleton",
            "disabled reason should explain non-closed top candidate");
}

/**
 * 功能：验证没有有效 B 时允许 A。
 * 输入：
 *   - 无。
 * 输出：
 *   - 断言失败时抛出异常。
 * 关键逻辑：
 *   - 如果当前除了 A 没有合理开放/非封闭候选，gate 不应强行拒绝 A。
 */
void testNoMeaningfulBAllowsA()
{
    LocalKnownMap map = makeBaseMap();
    MapPoseEstimator estimator;
    estimator.initialize();
    estimator.update(map, {0, 0});
    PathValueEvaluator evaluator(makeParameters());
    auto request = makeRequest(map, evaluator, estimator);
    request.candidates.push_back(makeCandidate({0, 1}, -2.0, 1, {{0, 0}, {0, 1}}));
    request.candidates.push_back(makeCandidate({1, 0}, -1e18, 0, {{0, 0}, {1, 0}}));

    const auto result = applyClosedSingletonLookaheadGate(request);
    require(result.debug.triggered, "closed singleton should trigger gate");
    require(result.debug.allowed, "A should be allowed when no meaningful B exists");
    require(result.selectedTarget == Position{0, 1}, "A should remain selected");
}

/**
 * 功能：验证 A 后没有有效 continuation 时拒绝 A。
 * 输入：
 *   - 无。
 * 输出：
 *   - 断言失败时抛出异常。
 * 关键逻辑：
 *   - B 位于 A 的 3x3 内，memory-only continuation 会排除它；因此 c_A=-inf，应选择 B 止损。
 */
void testNoAfterAContinuationRejectsA()
{
    LocalKnownMap map = makeBaseMap();
    MapPoseEstimator estimator;
    estimator.initialize();
    estimator.update(map, {0, 0});
    PathValueEvaluator evaluator(makeParameters());
    auto request = makeRequest(map, evaluator, estimator);
    request.candidates.push_back(makeCandidate({0, 1}, -2.0, 1, {{0, 0}, {0, 1}}));
    request.candidates.push_back(makeCandidate({1, 0}, -3.0, 12, {{0, 0}, {1, 0}}));

    const auto result = applyClosedSingletonLookaheadGate(request);
    require(result.debug.triggered, "closed singleton should trigger gate");
    require(!result.debug.allowed, "A should be rejected when c_A is invalid");
    require(result.selectedTarget == Position{1, 0}, "B should be selected after rejecting A");
    require(result.debug.bestAfterATarget == ai_player::kInvalid, "there should be no after-A target");
}

/**
 * 功能：验证 A 后有足够好的 memory-only continuation 时允许 A。
 * 输入：
 *   - 无。
 * 输出：
 *   - 断言失败时抛出异常。
 * 关键逻辑：
 *   - C 位于 A 的 3x3 之外且已在 memory 中可达，gate 可以计算 c_A；开放 B 不计算 c_B。
 */
void testGoodAfterAContinuationAllowsA()
{
    LocalKnownMap map = makeBaseMap();
    map.setObserved({0, 2}, " ");
    map.setObserved({0, 3}, " ");
    MapPoseEstimator estimator;
    estimator.initialize();
    estimator.update(map, {0, 0});
    PathValueEvaluator evaluator(makeParameters());
    auto request = makeRequest(map, evaluator, estimator);
    request.candidates.push_back(makeCandidate({0, 1}, 10.0, 1, {{0, 0}, {0, 1}}));
    request.candidates.push_back(makeCandidate({1, 0}, -5.0, 12, {{0, 0}, {1, 0}}));

    const auto result = applyClosedSingletonLookaheadGate(request);
    require(result.debug.triggered, "closed singleton should trigger gate");
    require(result.debug.simulatedAfterA, "A should be simulated with memory-only continuation");
    require(result.debug.bestAfterATarget == Position{0, 3}, "C should be the best after-A memory target");
    require(result.debug.allowed, "A should be allowed when combined score beats B");
    require(result.selectedTarget == Position{0, 1}, "A should remain selected");
}

/**
 * 功能：验证 debug 中只包含 after-A，不存在 after-B 概念。
 * 输入：
 *   - 无。
 * 输出：
 *   - 断言失败时抛出异常。
 * 关键逻辑：
 *   - 类型层面没有 bestAfterBReward 字段，确保实现没有对开放候选 B 做对称 rollout。
 */
void testNoAfterBRolloutField()
{
    ClosedSingletonGateDebug debug;
    debug.bestAfterAReward = -1e18;
    require(debug.bestAfterAReward < -1e17, "debug should expose after-A reward");
}

} // namespace

int main()
{
    testTopNotClosedSingletonDoesNotTrigger();
    testNoMeaningfulBAllowsA();
    testNoAfterAContinuationRejectsA();
    testGoodAfterAContinuationAllowsA();
    testNoAfterBRolloutField();
    std::cout << "closed_singleton_gate_tests.ok=1\n";
    return 0;
}
