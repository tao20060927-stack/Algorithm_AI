#ifndef RESOURCE_PICKUP_STRATEGY_H
#define RESOURCE_PICKUP_STRATEGY_H

#include "GameTypes.h"

namespace ai_player {

/**
 * 功能：求解 PDF 第一问的 3x3 资源拾取最优路径。
 * 输入：
 *   - source：包含 grid 二维数组的 JSON，grid 中 P 表示玩家，G 表示金币，T 表示陷阱，. 表示空地。
 * 输出：
 *   - 返回 JSON 结果，包含路径、逐步帧、资源值、步数、资源/步数比值和状态统计。
 * 关键逻辑：
 *   - 每轮选择能让“投影后资源/步数比值”最高且高于当前比值的资源目标，无法提升时立即停止。
 */
Json solveResourcePickupJson(const Json &source);

} // namespace ai_player

#endif
