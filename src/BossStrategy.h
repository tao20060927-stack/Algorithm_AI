#ifndef BOSS_STRATEGY_H
#define BOSS_STRATEGY_H

#include "GameTypes.h"

namespace ai_player {

// 求解 Boss 战最优策略：顺序揭示 + 滚动时域伤害容量规划 + 分支限界
// 输入 source 包含 B（Boss HP 数组）、PlayerSkills（技能列表）、可选的 minRounds 和 CoinConsumption
// 输出 JSON 包含 sequence（技能序列）、turns（总回合）、phases（每阶段明细）、reviveRule（复活规则）
Json runBossBattleJson(const Json &source);

} // namespace ai_player

#endif
