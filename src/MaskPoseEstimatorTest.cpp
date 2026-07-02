#include <iostream>
#include <stdexcept>
#include <string>

#include "Reward.h"

using ai_player::LocalKnownMap;
using ai_player::MapPoseEstimator;
using ai_player::Position;

namespace {

/**
 * 功能：检查测试条件，不满足时抛出异常。
 * 输入：
 *   - condition：需要验证的条件。
 *   - message：条件失败时输出的原因。
 * 输出：
 *   - 无返回值，失败时抛出 runtime_error。
 * 关键逻辑：
 *   - 掩码启用条件很容易和普通 BFS 混淆，因此每个断言都说明具体失败规则。
 */
void require(bool condition, const std::string &message)
{
    if (!condition) throw std::runtime_error(message);
}

/**
 * 功能：写入出生点周围的基础 3x3 视野。
 * 输入：
 *   - map：局部记忆地图。
 * 输出：
 *   - 无返回值，写入以局部原点为中心的普通可走格。
 * 关键逻辑：
 *   - 测试只构造 AI 已经看见的信息，不读取真实完整迷宫。
 */
void observeBirthView(LocalKnownMap &map)
{
    for (int row = -1; row <= 1; ++row) {
        for (int col = -1; col <= 1; ++col) {
            map.setObserved({row, col}, " ");
        }
    }
}

/**
 * 功能：验证上边缘出生但左右记忆长度不足 15 时不启用掩码。
 * 输入：
 *   - 无。
 * 输出：
 *   - 断言失败时抛出异常。
 * 关键逻辑：
 *   - 上方、左上、右上为迷宫外只能确定出生在上边缘中央；还不能完全确定横向 15 格范围。
 */
void testTopEdgeDoesNotClipBeforeFullWidth()
{
    LocalKnownMap map;
    observeBirthView(map);
    map.setOutside({-1, -1});
    map.setOutside({-1, 0});
    map.setOutside({-1, 1});

    MapPoseEstimator estimator;
    estimator.initialize();
    estimator.update(map, {0, 0});

    require(!estimator.isMaskActive(), "top edge should not activate mask before horizontal span reaches 15");
    require(estimator.isInsideEstimatedMaze({30, 0}), "inactive mask should not clip unknown BFS by estimated bounds");
    require(estimator.unknownComponentSizesTouchingView({0, 5}, map, 12).front() == 0,
            "partial top mask should treat the known top border as an outer wall before full mask activation");
}

/**
 * 功能：验证上边缘出生且局部左右范围覆盖 15 格后启用掩码。
 * 输入：
 *   - 无。
 * 输出：
 *   - 断言失败时抛出异常。
 * 关键逻辑：
 *   - 上边缘出生使用固定的上边缘中央假设；当局部列覆盖 [-7,7] 后，15x15 掩码才参与裁剪。
 */
void testTopEdgeActivatesAfterFullWidth()
{
    LocalKnownMap map;
    observeBirthView(map);
    map.setOutside({-1, -1});
    map.setOutside({-1, 0});
    map.setOutside({-1, 1});
    for (int col = -7; col <= 7; ++col) {
        map.setObserved({0, col}, " ");
    }

    MapPoseEstimator estimator;
    estimator.initialize();
    estimator.update(map, {0, 0});

    require(estimator.isMaskActive(), "top edge should activate mask after horizontal span reaches 15");
    require(estimator.localToEstimatedGlobal({0, 0}) == Position{0, 7}, "top edge origin should map to top-center");
    require(!estimator.isInsideEstimatedMaze({-1, 0}), "active top mask should clip cells above the maze");
    require(estimator.unknownComponentSizesTouchingView({-1, 0}, map, 12).front() == 0,
            "target outside active mask should have zero unknown component size");
}

/**
 * 功能：验证掩码边缘会像不可逾越墙一样阻止 |C| BFS 外泄。
 * 输入：
 *   - 无。
 * 输出：
 *   - 断言失败时抛出异常。
 * 关键逻辑：
 *   - 目标位于掩码上边缘内侧，周围掩码内邻居都已观察；若边缘没有当作墙，BFS 会向上越过掩码并错误计入未知格。
 */
void testMaskEdgeBlocksUnknownExpansion()
{
    LocalKnownMap map;
    observeBirthView(map);
    map.setOutside({-1, -1});
    map.setOutside({-1, 0});
    map.setOutside({-1, 1});
    for (int col = -7; col <= 7; ++col) {
        map.setObserved({0, col}, " ");
    }
    map.setObserved({1, 0}, " ");

    MapPoseEstimator estimator;
    estimator.initialize();
    estimator.update(map, {0, 0});

    require(estimator.isMaskActive(), "mask should be active before testing edge expansion");
    require(estimator.unknownComponentSizesTouchingView({0, 0}, map, 12).front() == 0,
            "active mask edge should block BFS expansion outside the maze");

    map.setObserved({1, -7}, " ");
    map.setObserved({0, -6}, " ");
    require(estimator.unknownComponentSizesTouchingView({0, -7}, map, 12).front() == 0,
            "active mask left edge should behave like an outer wall");

    map.setObserved({1, 7}, " ");
    map.setObserved({0, 6}, " ");
    require(estimator.unknownComponentSizesTouchingView({0, 7}, map, 12).front() == 0,
            "active mask right edge should behave like an outer wall");
}

/**
 * 功能：验证上边缘出生后，横向跨度达到 15 时按记忆边界对齐掩码，并把外围列当作墙。
 * 输入：
 *   - 无。
 * 输出：
 *   - 断言失败时抛出异常。
 * 关键逻辑：
 *   - 该结构对应样例中玩家在真实 (5,2) 时评估真实 (5,1)：目标格左侧没有被 3x3 直接看见，
 *     但根据 15x15 掩码它已经贴近外围墙，所以 |C| 不能向左展开成 12。
 */
void testSampleLeftCellNearMaskBoundaryIsClosed()
{
    LocalKnownMap map;
    observeBirthView(map);
    map.setOutside({-1, -1});
    map.setOutside({-1, 0});
    map.setOutside({-1, 1});
    for (int col = -11; col <= 3; ++col) {
        map.setObserved({0, col}, " ");
    }
    map.setObserved({5, -10}, " ");
    map.setObserved({5, -9}, " ");
    map.setObserved({4, -10}, "#");
    map.setObserved({6, -10}, "#");

    MapPoseEstimator estimator;
    estimator.initialize();
    estimator.update(map, {5, -9});

    require(estimator.isMaskActive(), "top-edge mask should activate once remembered horizontal span reaches 15");
    require(estimator.localToEstimatedGlobal({5, -10}) == Position{5, 1},
            "sample target should map to the inner cell next to the left boundary wall");
    require(estimator.unknownComponentSizesTouchingView({5, -10}, map, 12).front() == 0,
            "sample target next to mask boundary should not expand into an open component");
}

/**
 * 功能：验证横向记忆跨度达到 14 后，可以用已观察空格排除错误掩码候选。
 * 输入：
 *   - 无。
 * 输出：
 *   - 断言失败时抛出异常。
 * 关键逻辑：
 *   - 对应样例中玩家到真实 (1,2) 时，错误候选会把真实 (1,1) 的空格映射到外围墙；
 *     该候选被排除后，目标贴近真实左边界，|C| 不能再按开放区域计为 12。
 */
void testPruneMaskCandidateWhenSpanReachesFourteen()
{
    LocalKnownMap map;
    observeBirthView(map);
    map.setOutside({-1, -1});
    map.setOutside({-1, 0});
    map.setOutside({-1, 1});
    for (int col = -10; col <= 3; ++col) {
        map.setObserved({0, col}, "#");
    }
    map.setObserved({0, 0}, "S");
    map.setObserved({1, -10}, " ");
    map.setObserved({1, -9}, " ");
    map.setObserved({0, -10}, "#");
    map.setObserved({2, -10}, "#");

    MapPoseEstimator estimator;
    estimator.initialize();
    estimator.update(map, {1, -9});

    require(estimator.isMaskActive(), "span 14 should allow pruning to a determined top-edge mask");
    require(estimator.localToEstimatedGlobal({1, -10}) == Position{1, 1},
            "pruned mask should map the target next to the left boundary wall");
    require(estimator.unknownComponentSizesTouchingView({1, -10}, map, 12).front() == 0,
            "pruned mask should make the boundary-adjacent target closed");
}

/**
 * 功能：验证内部出生必须横向和纵向都达到 15 才启用掩码。
 * 输入：
 *   - 无。
 * 输出：
 *   - 断言失败时抛出异常。
 * 关键逻辑：
 *   - 没有出生点外部信息时，单独知道横向长度仍不能确定 15x15 掩码，需要上下左右两个方向都完整。
 */
void testInternalRequiresBothDimensions()
{
    LocalKnownMap map;
    observeBirthView(map);
    for (int col = -7; col <= 7; ++col) {
        map.setObserved({0, col}, " ");
    }

    MapPoseEstimator estimator;
    estimator.initialize();
    estimator.update(map, {0, 0});
    require(!estimator.isMaskActive(), "internal spawn should not activate mask with horizontal span only");

    for (int row = -7; row <= 7; ++row) {
        map.setObserved({row, 0}, " ");
    }
    estimator.update(map, {0, 0});
    require(estimator.isMaskActive(), "internal spawn should activate mask after both spans reach 15");
    require(estimator.localToEstimatedGlobal({0, 0}) == Position{7, 7},
            "internal fully determined mask should place local origin from observed bounding box");
}

} // namespace

int main()
{
    testTopEdgeDoesNotClipBeforeFullWidth();
    testTopEdgeActivatesAfterFullWidth();
    testMaskEdgeBlocksUnknownExpansion();
    testSampleLeftCellNearMaskBoundaryIsClosed();
    testPruneMaskCandidateWhenSpanReachesFourteen();
    testInternalRequiresBothDimensions();

    std::cout << "mask_pose_tests.ok=1\n";
    return 0;
}
