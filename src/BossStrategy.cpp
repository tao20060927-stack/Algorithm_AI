#include "BossStrategy.h"

#include <algorithm>
#include <queue>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace ai_player {
namespace {

struct BossPlan {
    bool ok = false;
    int turns = 0;
    std::vector<int> sequence;
    std::vector<int> cooldownAfter;
    int cooldownCostAfter = 0;
    int readyDamageAfter = 0;
};

inline constexpr int kOpeningBossReserveSlack = 1;

/**
 * 功能：读取任务 JSON 中的总回合参考值。
 * 输入：
 *   - source：任务 JSON，可能包含 minRounds、minRouds 或 min_turns。
 * 输出：
 *   - 返回回合数；不存在时返回 -1。
 * 关键逻辑：
 *   - 兼容历史样例里的拼写错误 minRouds，同时优先使用当前约定的 minRounds。
 */
int readMinRounds(const Json &source)
{
    if (source.contains("minRounds")) return source["minRounds"].get<int>();
    if (source.contains("minRouds")) return source["minRouds"].get<int>();
    if (source.contains("min_turns")) return source["min_turns"].get<int>();
    return -1;
}

/**
 * 功能：读取复活金币消耗。
 * 输入：
 *   - source：任务 JSON，可能包含 CoinConsumption。
 * 输出：
 *   - 返回复活所需金币；不存在时返回 0。
 * 关键逻辑：
 *   - BossStrategy 只输出战斗和复活规则信息，实际金币扣除由迷宫探索流程在触发失败时处理。
 */
int readCoinConsumption(const Json &source)
{
    if (source.contains("CoinConsumption")) return source["CoinConsumption"].get<int>();
    return 0;
}

/**
 * 功能：把冷却数组转成状态哈希键。
 * 输入：
 *   - hp：当前已揭示 Boss 的剩余血量。
 *   - cooldown：每个技能当前冷却。
 * 输出：
 *   - 返回可用于去重的字符串键。
 * 关键逻辑：
 *   - 同一血量和同一冷却状态下，如果已有更少回合到达，则当前分支没有继续扩展价值。
 */
std::string stateKey(int hp, const std::vector<int> &cooldown)
{
    std::ostringstream key;
    key << hp << '|';
    for (const int value : cooldown) key << value << ',';
    return key.str();
}

/**
 * 功能：计算战后冷却成本。
 * 输入：
 *   - cooldown：战斗结束后每个技能的冷却状态。
 *   - skills：技能列表。
 * 输出：
 *   - 返回冷却成本，数值越小表示越适合进入下一个未知 Boss。
 * 关键逻辑：
 *   - 高伤害、长冷却技能更珍贵，因此用 damage * (cooldown + 1) 作为技能价值权重。
 */
int cooldownCostAfter(const std::vector<int> &cooldown, const std::vector<Skill> &skills)
{
    int cost = 0;
    for (const Skill &skill : skills) {
        cost += cooldown[skill.id] * skill.damage * (skill.cooldown + 1);
    }
    return cost;
}

/**
 * 功能：计算战后立即可用技能的总伤害。
 * 输入：
 *   - cooldown：战斗结束后每个技能的冷却状态。
 *   - skills：技能列表。
 * 输出：
 *   - 返回冷却为 0 的技能伤害总和。
 * 关键逻辑：
 *   - 当冷却成本相同，优先保留更多可立即使用的伤害，提升下一个未知 Boss 的开局能力。
 */
int readyDamageAfter(const std::vector<int> &cooldown, const std::vector<Skill> &skills)
{
    int damage = 0;
    for (const Skill &skill : skills) {
        if (cooldown[skill.id] == 0) damage += skill.damage;
    }
    return damage;
}

/**
 * 功能：比较两个阶段 Boss 方案。
 * 输入：
 *   - best：当前最优方案。
 *   - candidate：待比较方案。
 * 输出：
 *   - 返回 candidate 是否更适合作为当前阶段方案。
 * 关键逻辑：
 *   - 调用方已经限制候选回合范围；在范围内优先保留下一阶段可立即使用的伤害。
 */
bool betterBossPlan(const BossPlan &best, const BossPlan &candidate)
{
    if (!best.ok) return true;
    if (candidate.readyDamageAfter != best.readyDamageAfter) {
        return candidate.readyDamageAfter > best.readyDamageAfter;
    }
    if (candidate.cooldownCostAfter != best.cooldownCostAfter) {
        return candidate.cooldownCostAfter < best.cooldownCostAfter;
    }
    if (candidate.turns != best.turns) return candidate.turns < best.turns;
    return candidate.sequence < best.sequence;
}

/**
 * 功能：按当前已揭示的单个 Boss 血量求最短击败序列。
 * 输入：
 *   - bossHp：当前已揭示 Boss 的血量，必须为正数。
 *   - startCooldown：进入该 Boss 阶段时每个技能的冷却状态。
 *   - skills：玩家技能列表，id 为技能编号，damage 为伤害，cooldown 为使用后冷却。
 *   - reserveSlack：允许比最短回合数最多多出的回合数。
 *   - phaseTurnLimit：已知 minRounds 推导出的本阶段最大可用回合数；负数表示不限制。
 * 输出：
 *   - 返回击败该 Boss 的阶段序列、耗费回合和结束后的冷却状态。
 * 关键逻辑：
 *   - 分支限界只使用当前 Boss 血量，不读取后续未知 Boss 血量。
 *   - 第一只 Boss 允许多 1 回合保留开局技能节奏；后续 Boss 只在最短回合方案内做冷却 tie-break。
 */
BossPlan solveRevealedBoss(int bossHp, const std::vector<int> &startCooldown, const std::vector<Skill> &skills,
                           int reserveSlack, int phaseTurnLimit)
{
    int maxDamage = 0;
    for (const Skill &skill : skills) {
        if (skill.damage > maxDamage) maxDamage = skill.damage;
    }
    if (bossHp <= 0) return {true, 0, {}, startCooldown};
    if (maxDamage <= 0) return {};

    const auto lowerBound = [&](int hp) {
        if (hp <= 0) return 0;
        return (hp + maxDamage - 1) / maxDamage;
    };

    struct State {
        int hp = 0;
        std::vector<int> cooldown;
        std::vector<int> sequence;
        int bound = 0;
    };

    struct CompareState {
        /**
         * 功能：定义当前 Boss 分支限界队列的优先级。
         * 输入：
         *   - left/right：两个候选状态。
         * 输出：
         *   - 返回 true 表示 left 优先级低于 right。
         * 关键逻辑：
         *   - 优先扩展估价回合更少的状态；估价相同时优先扩展已用回合更少的状态。
         */
        bool operator()(const State &left, const State &right) const
        {
            if (left.bound != right.bound) return left.bound > right.bound;
            return left.sequence.size() > right.sequence.size();
        }
    };

    std::priority_queue<State, std::vector<State>, CompareState> queue;
    queue.push({bossHp, startCooldown, {}, lowerBound(bossHp)});
    std::unordered_map<std::string, int> bestSeenTurns;

    BossPlan best;
    best.turns = -1;
    int minTurns = -1;

    while (!queue.empty()) {
        State state = queue.top();
        queue.pop();
        const int turn = static_cast<int>(state.sequence.size());
        const int allowedTurns = minTurns < 0 ? phaseTurnLimit
                                              : (phaseTurnLimit < 0 ? minTurns + reserveSlack
                                                                    : std::min(minTurns + reserveSlack, phaseTurnLimit));
        if (allowedTurns >= 0 && state.bound > allowedTurns) continue;

        const std::string key = stateKey(state.hp, state.cooldown);
        const auto seen = bestSeenTurns.find(key);
        if (seen != bestSeenTurns.end() && seen->second <= turn) continue;
        bestSeenTurns[key] = turn;

        if (state.hp <= 0) {
            if (minTurns < 0) minTurns = turn;
            const int candidateAllowedTurns = phaseTurnLimit < 0 ? minTurns + reserveSlack
                                                                 : std::min(minTurns + reserveSlack, phaseTurnLimit);
            if (turn <= candidateAllowedTurns) {
                BossPlan candidate;
                candidate.ok = true;
                candidate.turns = turn;
                candidate.sequence = state.sequence;
                candidate.cooldownAfter = state.cooldown;
                candidate.cooldownCostAfter = cooldownCostAfter(candidate.cooldownAfter, skills);
                candidate.readyDamageAfter = readyDamageAfter(candidate.cooldownAfter, skills);
                if (betterBossPlan(best, candidate)) best = std::move(candidate);
            }
            continue;
        }

        bool hasSkill = false;
        for (const Skill &skill : skills) {
            if (state.cooldown[skill.id] > 0) continue;
            hasSkill = true;
            State next = state;
            next.hp -= skill.damage;
            for (int &cooldown : next.cooldown) {
                if (cooldown > 0) --cooldown;
            }
            next.cooldown[skill.id] = skill.cooldown;
            next.sequence.push_back(skill.id);
            next.bound = static_cast<int>(next.sequence.size()) + lowerBound(next.hp);
            const int nextAllowedTurns =
                minTurns < 0 ? phaseTurnLimit
                             : (phaseTurnLimit < 0 ? minTurns + reserveSlack
                                                   : std::min(minTurns + reserveSlack, phaseTurnLimit));
            if (nextAllowedTurns < 0 || next.bound <= nextAllowedTurns) queue.push(std::move(next));
        }

        if (!hasSkill) {
            for (int &cooldown : state.cooldown) {
                if (cooldown > 0) --cooldown;
            }
            state.sequence.push_back(-1);
            state.bound = static_cast<int>(state.sequence.size()) + lowerBound(state.hp);
            const int waitAllowedTurns =
                minTurns < 0 ? phaseTurnLimit
                             : (phaseTurnLimit < 0 ? minTurns + reserveSlack
                                                   : std::min(minTurns + reserveSlack, phaseTurnLimit));
            if (waitAllowedTurns < 0 || state.bound <= waitAllowedTurns) queue.push(std::move(state));
        }
    }

    return best;
}

/**
 * 功能：构造当前已知 Boss 血量数组。
 * 输入：
 *   - bossHPs：真实 Boss 血量序列，仅用于模拟揭示。
 *   - revealedCount：当前已经揭示的 Boss 数量。
 * 输出：
 *   - 返回 JSON 数组，未知 Boss 用 null 表示。
 * 关键逻辑：
 *   - 输出调试信息时必须体现“已知保持已知，未知保持未知”的规则。
 */
Json knownHpJson(const std::vector<int> &bossHPs, int revealedCount)
{
    Json known = Json::array();
    for (int i = 0; i < static_cast<int>(bossHPs.size()); ++i) {
        known.push_back(i < revealedCount ? Json(bossHPs[i]) : Json(nullptr));
    }
    return known;
}

} // namespace

/**
 * 功能：按顺序揭示规则求解 Boss 战策略。
 * 输入：
 *   - source：任务 JSON，包含 B 血量数组、PlayerSkills 技能数组，可选 minRounds 和 CoinConsumption。
 * 输出：
 *   - 返回 JSON，包含总序列、总回合、每个 Boss 被揭示时的局部最优阶段和复活规则字段。
 * 关键逻辑：
 *   - 一开始只把第一个 Boss 血量放入已知信息；每击败一个 Boss 后才揭示下一个 Boss 血量。
 *   - 每个阶段的分支限界只使用当前已揭示 Boss，不把后续隐藏血量放入搜索状态。
 */
Json runBossBattleJson(const Json &source)
{
    if (!source.contains("B") || !source["B"].is_array()) {
        return {{"ok", false}, {"error", "missing B boss HP array"}};
    }
    if (!source.contains("PlayerSkills") || !source["PlayerSkills"].is_array()) {
        return {{"ok", false}, {"error", "missing PlayerSkills array"}};
    }

    const auto bossHPs = source["B"].get<std::vector<int>>();
    if (bossHPs.empty()) return {{"ok", false}, {"error", "B boss HP array must not be empty"}};

    std::vector<Skill> skills;
    for (int i = 0; i < static_cast<int>(source["PlayerSkills"].size()); ++i) {
        const auto &item = source["PlayerSkills"][i];
        if (!item.is_array() || item.size() != 2) return {{"ok", false}, {"error", "invalid PlayerSkills item"}};
        skills.push_back({i, item[0].get<int>(), item[1].get<int>()});
    }

    const int minRounds = readMinRounds(source);
    std::vector<int> cooldown(skills.size(), 0);
    std::vector<int> sequence;
    Json phases = Json::array();

    for (int bossIndex = 0; bossIndex < static_cast<int>(bossHPs.size()); ++bossIndex) {
        const int revealedCount = bossIndex + 1;
        const int reserveSlack = bossIndex == 0 && bossHPs.size() > 1 ? kOpeningBossReserveSlack : 0;
        const int remainingBossCount = static_cast<int>(bossHPs.size()) - bossIndex - 1;
        const int phaseTurnLimit =
            minRounds < 0 ? -1 : minRounds - static_cast<int>(sequence.size()) - remainingBossCount;
        const BossPlan plan = solveRevealedBoss(bossHPs[bossIndex], cooldown, skills, reserveSlack, phaseTurnLimit);
        if (!plan.ok) {
            return {{"ok", false},
                    {"error", "boss battle has no solution"},
                    {"knowledgePolicy", "sequential_reveal_current_boss_only"},
                    {"knownBossHPs", knownHpJson(bossHPs, revealedCount)}};
        }

        Json phase{{"bossIndex", bossIndex},
                   {"revealedHp", bossHPs[bossIndex]},
                   {"knownBossHPsBeforeFight", knownHpJson(bossHPs, revealedCount)},
                   {"turnStart", static_cast<int>(sequence.size())},
                   {"turns", plan.turns},
                   {"sequence", plan.sequence},
                   {"cooldownAfter", plan.cooldownAfter},
                   {"cooldownCostAfter", plan.cooldownCostAfter},
                   {"readyDamageAfter", plan.readyDamageAfter},
                   {"reserveSlack", reserveSlack},
                   {"phaseTurnLimit", phaseTurnLimit}};
        phases.push_back(std::move(phase));
        sequence.insert(sequence.end(), plan.sequence.begin(), plan.sequence.end());
        cooldown = plan.cooldownAfter;
    }

    const int coinConsumption = readCoinConsumption(source);
    Json result{{"ok", true},
                {"sequence", sequence},
                {"turns", static_cast<int>(sequence.size())},
                {"knowledgePolicy", "sequential_reveal_current_boss_only"},
                {"stagePolicy", "opening boss may use 1 reserve slack; later bosses keep min turns and prefer ready damage"},
                {"initialKnownBossHPs", knownHpJson(bossHPs, 1)},
                {"finalKnownBossHPs", knownHpJson(bossHPs, static_cast<int>(bossHPs.size()))},
                {"phases", phases},
                {"CoinConsumption", coinConsumption},
                {"reviveRule",
                 {{"coinCost", coinConsumption},
                  {"restartPosition", "S"},
                  {"knownHpPersistence", "revealed boss HP remains known; unrevealed boss HP remains unknown"}}}};

    if (minRounds >= 0) {
        result["minRounds"] = minRounds;
        result["withinMinRounds"] = static_cast<int>(sequence.size()) <= minRounds;
        result["matchesMinRounds"] = static_cast<int>(sequence.size()) == minRounds;
    }
    return result;
}

} // namespace ai_player
