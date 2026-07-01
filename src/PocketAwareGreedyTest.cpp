#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

#include "PocketAwareGreedy.h"

using ai_player::AgentState;
using ai_player::LocalKnownMap;
using ai_player::MapPoseEstimator;
using ai_player::PathValueContext;
using ai_player::PathValueEvaluator;
using ai_player::PocketDecision;
using ai_player::Position;
using ai_player::RewardParameters;
using ai_player::choosePocketFirstTarget;

namespace {

/**
 * 功能：检查测试条件，不满足时抛出异常。
 * 输入：
 *   - condition：测试条件。
 *   - message：失败说明。
 * 输出：
 *   - 无返回值。
 * 关键逻辑：
 *   - pocket 测试需要定位具体规则失败点，因此每个断言都带明确说明。
 */
void require(bool condition, const std::string &message)
{
    if (!condition) throw std::runtime_error(message);
}

/**
 * 功能：构造 pocket 测试专用 reward 参数。
 * 输入：
 *   - 无。
 * 输出：
 *   - 返回稳定、容易验证的参数组合。
 * 关键逻辑：
 *   - 保留真实 qEff 路径成本，适当放大 lambdaRemain，使“保留高 Iproxy 出口”的行为在小图中可观测。
 */
RewardParameters testParameters()
{
    RewardParameters parameters;
    parameters.pocketRadius = 3;
    parameters.pocketMu = 0.2;
    parameters.pocketLambdaRemain = 0.4;
    parameters.kappaU = 60.0;
    parameters.areaMax = 12;
    parameters.rhoAreaValueMin = 0.001;
    parameters.qEffLengthWeight = 1.0;
    return parameters;
}

/**
 * 功能：把一个局部格写入 known_map。
 * 输入：
 *   - map：局部记忆地图。
 *   - pos：局部坐标。
 *   - tile：格子类型。
 * 输出：
 *   - 无返回值。
 * 关键逻辑：
 *   - 测试只构造 AI 已观察到的局部地图，不读取真实完整迷宫。
 */
void observe(LocalKnownMap &map, Position pos, const std::string &tile)
{
    map.setObserved(pos, tile);
}

/**
 * 功能：构造用于左右金币口袋的基础地图。
 * 输入：
 *   - blockRightUnknown：是否把右金币周围全部观察掉，从而让右金币 Iproxy 低。
 * 输出：
 *   - 返回包含当前位置、左右金币和必要已知通路的局部地图。
 * 关键逻辑：
 *   - 左金币旁保留未知邻居，右金币旁用已观察格包住，以制造 left Iproxy > right Iproxy。
 */
LocalKnownMap twoCoinPocketMap(bool blockRightUnknown)
{
    LocalKnownMap map;
    observe(map, {0, 0}, " ");
    observe(map, {0, -1}, "G");
    observe(map, {0, 1}, "G");
    observe(map, {-1, 0}, " ");
    observe(map, {1, 0}, " ");
    if (blockRightUnknown) {
        observe(map, {-1, 1}, " ");
        observe(map, {1, 1}, " ");
        observe(map, {0, 2}, " ");
    }
    return map;
}

/**
 * 功能：执行 pocket 决策。
 * 输入：
 *   - map：局部记忆地图。
 *   - current：当前局部坐标。
 *   - state：当前资源和步数。
 *   - parameters：reward 参数。
 * 输出：
 *   - 返回 pocket 决策结果。
 * 关键逻辑：
 *   - MapPoseEstimator 当前 Iproxy 不依赖真实尺寸，测试只初始化即可。
 */
PocketDecision decide(const LocalKnownMap &map, Position current, AgentState state, RewardParameters parameters)
{
    MapPoseEstimator poseEstimator;
    poseEstimator.initialize();
    PathValueEvaluator evaluator(parameters);
    PathValueContext context;
    context.state = state;
    return choosePocketFirstTarget(current, context, map, poseEstimator, evaluator);
}

/**
 * 功能：从 pocket 调试候选中查找指定目标。
 * 输入：
 *   - decision：pocket 决策结果。
 *   - target：要查找的局部目标。
 * 输出：
 *   - 返回匹配候选指针；找不到返回 nullptr。
 * 关键逻辑：
 *   - 用于验证已触发陷阱回踩时 deltaR 不重复扣分。
 */
const ai_player::PocketCandidateDebug *findCandidate(const PocketDecision &decision, Position target)
{
    for (const auto &candidate : decision.debug.candidates) {
        if (candidate.target == target) return &candidate;
    }
    return nullptr;
}

} // namespace

int main()
{
    const auto parameters = testParameters();

    {
        const auto map = twoCoinPocketMap(true);
        const auto decision = decide(map, {0, 0}, AgentState{}, parameters);
        require(decision.enabled, "two coin pocket should be enabled");
        require(decision.target == Position{0, 1}, "low-Iproxy right coin should be selected first");
    }

    {
        LocalKnownMap map;
        observe(map, {0, 0}, " ");
        observe(map, {0, -1}, "G");
        observe(map, {0, 1}, " ");
        observe(map, {0, 2}, " ");
        observe(map, {0, 3}, "G");
        observe(map, {-1, 0}, " ");
        observe(map, {1, 0}, " ");
        observe(map, {-1, 3}, " ");
        observe(map, {1, 3}, " ");
        AgentState state;
        state.resource = 100;
        state.steps = 10;
        const auto decision = decide(map, {0, 0}, state, parameters);
        require(decision.enabled, "high-cost right coin pocket should be enabled");
        require(decision.target == Position{0, -1}, "high path cost should allow selecting left coin first");
    }

    {
        auto map = twoCoinPocketMap(true);
        observe(map, {1, 0}, "G");
        observe(map, {2, 0}, " ");
        observe(map, {1, -1}, " ");
        const auto decision = decide(map, {0, 0}, AgentState{}, parameters);
        require(decision.enabled, "three coin pocket should be enabled");
        require(decision.target != Position{0, -1}, "highest-Iproxy left coin should be kept for later");
    }

    {
        LocalKnownMap map;
        observe(map, {0, 0}, "T");
        observe(map, {0, -1}, "G");
        observe(map, {0, 1}, "G");
        observe(map, {1, 0}, "G");
        observe(map, {-1, -1}, "#");
        observe(map, {-1, 0}, "#");
        observe(map, {-1, 1}, "#");
        observe(map, {1, -1}, "#");
        observe(map, {1, 1}, "#");
        observe(map, {0, 2}, "#");
        observe(map, {2, 0}, " ");
        map.markTriggered({0, 0});
        const auto decision = decide(map, {0, 0}, AgentState{}, parameters);
        require(decision.enabled, "default 15x15 local pocket should be enabled");
        require(decision.target == Position{0, 1}, "default local pocket should select right coin first");
    }

    {
        LocalKnownMap map;
        observe(map, {0, 0}, " ");
        observe(map, {0, 1}, "G");
        const auto decision = decide(map, {0, 0}, AgentState{}, parameters);
        require(!decision.enabled, "single coin should not form pocket");
    }

    {
        LocalKnownMap map;
        observe(map, {0, 0}, " ");
        observe(map, {0, 1}, "T");
        observe(map, {0, 2}, "G");
        observe(map, {0, -1}, "G");
        map.markTriggered({0, 1});
        const auto decision = decide(map, {0, 0}, AgentState{}, parameters);
        const auto *candidate = findCandidate(decision, {0, 2});
        require(candidate != nullptr, "right coin candidate should exist");
        require(candidate->deltaR == 50, "triggered trap on path should not deduct again");
    }

    std::cout << "pocket_aware_greedy_tests.ok=1\n";
    return 0;
}
