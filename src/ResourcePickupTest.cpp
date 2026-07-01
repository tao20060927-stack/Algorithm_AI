#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

#include "ResourcePickupStrategy.h"

using ai_player::Json;
using ai_player::solveResourcePickupJson;

namespace {
/**
 * 功能：检查布尔条件，不满足时终止测试。
 * 输入：
 *   - condition：需要成立的条件。
 *   - message：失败时输出的错误说明。
 * 输出：
 *   - 无返回值；失败时抛出异常。
 * 关键逻辑：
 *   - 让测试失败点带有明确文本，方便定位是资源、步数、比值还是路径不符合预期。
 */
void require(bool condition, const std::string &message)
{
    if (!condition) throw std::runtime_error(message);
}

/**
 * 功能：检查浮点数是否接近期望值。
 * 输入：
 *   - actual：实际计算结果。
 *   - expected：期望结果。
 *   - message：失败时输出的错误说明。
 * 输出：
 *   - 无返回值；误差过大时抛出异常。
 * 关键逻辑：
 *   - 比值计算涉及 double，使用极小误差避免格式化差异造成误报。
 */
void requireNear(double actual, double expected, const std::string &message)
{
    if (std::abs(actual - expected) > 1e-9) {
        throw std::runtime_error(message + ": actual=" + std::to_string(actual));
    }
}

/**
 * 功能：运行一个 3x3 贪心测试用例并检查核心输出。
 * 输入：
 *   - source：测试输入 JSON。
 *   - expectedResource：期望最终资源值。
 *   - expectedSteps：期望路径步数。
 *   - expectedRatio：期望资源/步数比值。
 * 输出：
 *   - 返回后端求解结果 JSON，供调用者继续检查路径等细节。
 * 关键逻辑：
 *   - 统一检查 ok、resource、steps 和 score_ratio，保证所有测试都覆盖核心任务指标。
 */
Json runCase(const Json &source, int expectedResource, int expectedSteps, double expectedRatio)
{
    Json result = solveResourcePickupJson(source);
    require(result.value("ok", false), "result ok should be true");
    require(result["resource"].get<int>() == expectedResource, "unexpected resource");
    require(result["steps"].get<int>() == expectedSteps, "unexpected steps");
    requireNear(result["score_ratio"].get<double>(), expectedRatio, "unexpected ratio");
    return result;
}
} // namespace

int main()
{
    const Json case2 = Json::parse(R"JSON({
      "case_id": 2,
      "grid": [[".","T","G"],[".","P","T"],[".","T","G"]]
    })JSON");
    Json result = runCase(case2, 70, 4, 17.5);
    require(result["path"].size() == 5, "case2 path size");
    require(result["path"][1]["row"].get<int>() == 1 && result["path"][1]["col"].get<int>() == 2,
            "case2 should move right first");

    runCase(Json::parse(R"JSON({"grid":[[".",".","."],["T","P","T"],[".",".","."]]})JSON"), 0, 0, 0.0);
    runCase(Json::parse(R"JSON({"grid":[[".",".","."],[".","P","G"],[".",".","."]]})JSON"), 50, 1, 50.0);
    runCase(Json::parse(R"JSON({"grid":[["G",".","G"],[".","P","."],[".",".","."]]})JSON"), 100, 4, 25.0);
    runCase(Json::parse(R"JSON({"grid":[[".",".","."],[".","P","."],[".",".","."]]})JSON"), 0, 0, 0.0);

    std::cout << "resource_pickup_tests.ok=1\n";
    return 0;
}
