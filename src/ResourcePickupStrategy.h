#ifndef RESOURCE_PICKUP_STRATEGY_H
#define RESOURCE_PICKUP_STRATEGY_H

#include "GameTypes.h"

namespace ai_player {

/**
 * 功能：求解 PDF 第一问的 3x3 局部资源贪心路径。
 * 输入：
 *   - source：包含 grid 二维数组的 JSON，grid 中 P 表示玩家，G 表示金币，T 表示陷阱，. 表示空地。
 * 输出：
 *   - 返回 JSON 结果，包含路径、逐步帧、资源值、步数、资源/步数比值和状态统计。
 * 关键逻辑：
 *   - 每轮只检查当前格子的上、右、下、左四邻域，按局部束 BundleScore 贪心选择。
 *   - 若执行该局部束会降低累计 R/L，则立即停止，不做 DP、状态压缩或全路径枚举。
 */
Json solveResourcePickupJson(const Json &source); // PDF 第一问入口：3x3 局部束贪心资源拾取

} // namespace ai_player

#endif
