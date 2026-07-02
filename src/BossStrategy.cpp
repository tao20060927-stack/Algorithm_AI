#include "BossStrategy.h"

#include <algorithm>
#include <map>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace ai_player {
namespace {

struct BossPlanCandidate {
    bool ok = false;
    int turns = 0;
    std::vector<int> sequence;
    std::vector<int> cooldownAfter;
    int cooldownCostAfter = 0;
    int readyDamageAfter = 0;
    int robustScore = -1;
};

struct PlannerMemo {
    std::map<std::string, std::vector<BossPlanCandidate>> killExact;
    std::map<std::string, int> robustUnknown;
    std::map<std::string, int> knownPrefix;
};

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
 * 功能：把整数数组转为 memo 字符串键。
 * 输入：
 *   - values：冷却数组或其它小整数数组。
 * 输出：
 *   - 返回稳定字符串键。
 * 关键逻辑：
 *   - Boss 鲁棒递推大量复用同一冷却状态，用字符串键避免为 vector 自定义哈希。
 */
std::string vectorKey(const std::vector<int> &values)
{
    std::ostringstream key;
    for (const int value : values) key << value << ',';
    return key.str();
}

/**
 * 功能：构造 Kill(h,c,t) 的 memo key。
 * 输入：
 *   - hp：Boss 血量。
 *   - cooldown：起始冷却状态。
 *   - exactTurns：精确击杀回合数。
 * 输出：
 *   - 返回可复用的字符串 key。
 * 关键逻辑：
 *   - 同一血量、同一冷却、同一精确回合数的击杀终态集合完全相同。
 */
std::string killKey(int hp, const std::vector<int> &cooldown, int exactTurns)
{
    std::ostringstream key;
    key << hp << '|' << exactTurns << '|' << vectorKey(cooldown);
    return key.str();
}

/**
 * 功能：构造 G(u,r,c) 或 F(j,r,c) 的 memo key。
 * 输入：
 *   - index：未知 Boss 数 u 或已知 Boss 下标 j。
 *   - remainingTurns：剩余回合数。
 *   - cooldown：当前冷却状态。
 * 输出：
 *   - 返回可复用的字符串 key。
 * 关键逻辑：
 *   - Hmax、skills、knownHPs 在一次顶层求解中固定，因此不需要放进 key。
 */
std::string valueKey(int index, int remainingTurns, const std::vector<int> &cooldown)
{
    std::ostringstream key;
    key << index << '|' << remainingTurns << '|' << vectorKey(cooldown);
    return key.str();
}

/**
 * 功能：计算战后冷却成本。
 * 输入：
 *   - cooldown：战斗结束后每个技能的冷却状态。
 *   - skills：技能列表。
 * 输出：
 *   - 返回冷却成本，数值越小表示越适合进入下一个 Boss。
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
 *   - 该值只作为 robustScore 相同时的稳定 tie-break，不再压过回合数和鲁棒后缀价值。
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
 * 功能：执行一回合技能或等待动作。
 * 输入：
 *   - hp：当前 Boss 剩余血量。
 *   - cooldown：当前技能冷却。
 *   - skills：技能列表。
 *   - action：技能 id；-1 表示等待。
 * 输出：
 *   - 通过引用输出 nextHp 和 nextCooldown。
 * 关键逻辑：
 *   - 保持原有冷却语义：先造成伤害，再让其它冷却减 1，最后把当前技能设为自身 cooldown。
 */
void applyBossAction(int hp, const std::vector<int> &cooldown, const std::vector<Skill> &skills, int action,
                     int &nextHp, std::vector<int> &nextCooldown)
{
    nextHp = hp;
    nextCooldown = cooldown;
    if (action >= 0) nextHp -= skills[action].damage;
    for (int &value : nextCooldown) {
        if (value > 0) --value;
    }
    if (action >= 0) nextCooldown[action] = skills[action].cooldown;
}

/**
 * 功能：枚举当前冷却状态下的合法动作。
 * 输入：
 *   - cooldown：当前技能冷却。
 *   - skills：技能列表。
 * 输出：
 *   - 返回可使用技能 id；如果没有任何技能可用，则只返回 -1 表示等待。
 * 关键逻辑：
 *   - 按题意只有无技能可用时才等待，不允许在可击打时故意空过刷冷却。
 */
std::vector<int> availableBossActions(const std::vector<int> &cooldown, const std::vector<Skill> &skills)
{
    std::vector<int> actions;
    for (const Skill &skill : skills) {
        if (cooldown[skill.id] == 0) actions.push_back(skill.id);
    }
    if (actions.empty()) actions.push_back(-1);
    return actions;
}

/**
 * 功能：枚举从指定冷却状态出发，恰好 exactTurns 回合击杀血量 hp 的所有结束状态。
 * 输入：
 *   - hp：当前 Boss 血量。
 *   - startCooldown：起始冷却状态。
 *   - skills：技能列表。
 *   - exactTurns：必须恰好击杀的回合数。
 *   - memo：Kill 结果缓存。
 * 输出：
 *   - 返回所有合法击杀方案；同一 cooldownAfter 只保留字典序最小序列。
 * 关键逻辑：
 *   - Boss 必须在第 exactTurns 回合首次死亡；死亡前不能继续等待刷冷却。
 */
std::vector<BossPlanCandidate> enumerateKillExactTurns(int hp, const std::vector<int> &startCooldown,
                                                       const std::vector<Skill> &skills, int exactTurns,
                                                       PlannerMemo &memo)
{
    if (hp <= 0 || exactTurns <= 0) return {};
    const std::string memoKey = killKey(hp, startCooldown, exactTurns);
    if (const auto it = memo.killExact.find(memoKey); it != memo.killExact.end()) return it->second;

    struct State {
        int hp = 0;
        std::vector<int> cooldown;
        std::vector<int> sequence;
    };

    std::vector<State> states{{hp, startCooldown, {}}};
    std::map<std::string, BossPlanCandidate> dedup;
    for (int turn = 0; turn < exactTurns; ++turn) {
        std::vector<State> nextStates;
        for (const State &state : states) {
            if (state.hp <= 0) continue;
            for (const int action : availableBossActions(state.cooldown, skills)) {
                int nextHp = state.hp;
                std::vector<int> nextCooldown;
                applyBossAction(state.hp, state.cooldown, skills, action, nextHp, nextCooldown);
                std::vector<int> nextSequence = state.sequence;
                nextSequence.push_back(action);

                const bool lastTurn = turn + 1 == exactTurns;
                if (!lastTurn && nextHp <= 0) continue;
                if (lastTurn) {
                    if (nextHp > 0) continue;
                    BossPlanCandidate candidate;
                    candidate.ok = true;
                    candidate.turns = exactTurns;
                    candidate.sequence = std::move(nextSequence);
                    candidate.cooldownAfter = std::move(nextCooldown);
                    candidate.cooldownCostAfter = cooldownCostAfter(candidate.cooldownAfter, skills);
                    candidate.readyDamageAfter = readyDamageAfter(candidate.cooldownAfter, skills);
                    const std::string key = vectorKey(candidate.cooldownAfter);
                    const auto old = dedup.find(key);
                    if (old == dedup.end() || candidate.sequence < old->second.sequence) {
                        dedup[key] = std::move(candidate);
                    }
                } else {
                    nextStates.push_back({nextHp, std::move(nextCooldown), std::move(nextSequence)});
                }
            }
        }
        states = std::move(nextStates);
        if (states.empty() && turn + 1 < exactTurns) break;
    }

    std::vector<BossPlanCandidate> result;
    for (auto &item : dedup) result.push_back(std::move(item.second));
    std::sort(result.begin(), result.end(), [](const BossPlanCandidate &left, const BossPlanCandidate &right) {
        if (left.turns != right.turns) return left.turns < right.turns;
        return left.sequence < right.sequence;
    });
    memo.killExact[memoKey] = result;
    return result;
}

/**
 * 功能：枚举指定最大回合数内击杀 Boss 的所有候选方案。
 * 输入：
 *   - hp：当前 Boss 血量。
 *   - startCooldown：起始冷却状态。
 *   - skills：技能列表。
 *   - maxTurns：最多可用回合数。
 *   - memo：Kill 结果缓存。
 * 输出：
 *   - 返回 t=1..maxTurns 的所有合法击杀方案。
 * 关键逻辑：
 *   - 该函数只枚举候选，不做跨方案价值比较；比较交给鲁棒价值函数。
 */
std::vector<BossPlanCandidate> enumerateKillUpToTurns(int hp, const std::vector<int> &startCooldown,
                                                      const std::vector<Skill> &skills, int maxTurns,
                                                      PlannerMemo &memo)
{
    std::vector<BossPlanCandidate> result;
    for (int turns = 1; turns <= maxTurns; ++turns) {
        auto exact = enumerateKillExactTurns(hp, startCooldown, skills, turns, memo);
        result.insert(result.end(), exact.begin(), exact.end());
    }
    return result;
}

/**
 * 功能：计算未知 Boss 血量模型上界 Hmax。
 * 输入：
 *   - source：任务 JSON。
 *   - skills：技能列表。
 *   - knownHPs：当前已揭示 Boss 血量前缀。
 * 输出：
 *   - 返回未知 Boss 鲁棒计算使用的血量上界。
 * 关键逻辑：
 *   - 未提供 UnknownBossHpMax 时使用 2 * maxDamage；只用已揭示血量抬高下界，不读取未揭示 B。
 */
int computeUnknownBossHpMax(const Json &source, const std::vector<Skill> &skills, const std::vector<int> &knownHPs)
{
    int maxDamage = 0;
    for (const Skill &skill : skills) maxDamage = std::max(maxDamage, skill.damage);
    int hMax = source.contains("UnknownBossHpMax") ? source["UnknownBossHpMax"].get<int>() : 2 * maxDamage;
    for (const int hp : knownHPs) hMax = std::max(hMax, hp);
    return std::max(0, hMax);
}

int robustUnknownValue(int unknownBossCount, int remainingTurns, const std::vector<int> &cooldown,
                       const std::vector<Skill> &skills, int hMax, PlannerMemo &memo);

/**
 * 功能：计算已知 Boss 前缀从第 j 个 Boss 开始的鲁棒价值 F(j,r,c)。
 * 输入：
 *   - j：当前要处理的已知 Boss 下标。
 *   - remainingTurns：剩余回合预算。
 *   - cooldown：当前冷却状态。
 *   - knownHPs：当前已揭示 Boss 血量前缀。
 *   - totalBossCount：Boss 总数量。
 *   - skills：技能列表。
 *   - hMax：未知 Boss 先验血量上界。
 *   - memo：递推缓存。
 * 输出：
 *   - 返回完成已知后缀后，对未知 Boss 后缀的鲁棒血量阈值；不可行时返回 -1。
 * 关键逻辑：
 *   - 已知部分只使用 knownHPs 前缀；到达前缀末尾后交给 G 处理未知后缀。
 */
int knownPrefixValue(int j, int remainingTurns, const std::vector<int> &cooldown, const std::vector<int> &knownHPs,
                     int totalBossCount, const std::vector<Skill> &skills, int hMax, PlannerMemo &memo)
{
    if (remainingTurns < 0) return -1;
    const int knownCount = static_cast<int>(knownHPs.size());
    if (j == knownCount) {
        return robustUnknownValue(totalBossCount - knownCount, remainingTurns, cooldown, skills, hMax, memo);
    }
    if (j > knownCount) return -1;

    const std::string memoKey = valueKey(j, remainingTurns, cooldown);
    if (const auto it = memo.knownPrefix.find(memoKey); it != memo.knownPrefix.end()) return it->second;

    int best = -1;
    for (int turns = 1; turns <= remainingTurns; ++turns) {
        for (const auto &candidate : enumerateKillExactTurns(knownHPs[j], cooldown, skills, turns, memo)) {
            const int value = knownPrefixValue(j + 1, remainingTurns - turns, candidate.cooldownAfter, knownHPs,
                                               totalBossCount, skills, hMax, memo);
            best = std::max(best, value);
        }
    }
    memo.knownPrefix[memoKey] = best;
    return best;
}

/**
 * 功能：计算未知后缀鲁棒血量阈值 G(u,r,c)。
 * 输入：
 *   - unknownBossCount：还剩多少个未知 Boss。
 *   - remainingTurns：剩余回合预算。
 *   - cooldown：当前冷却状态。
 *   - skills：技能列表。
 *   - hMax：未知 Boss 血量上界。
 *   - memo：递推缓存。
 * 输出：
 *   - 返回整数 H，表示可以保证处理所有血量 <= H 的未知 Boss 后缀。
 * 关键逻辑：
 *   - 对每个 H 必须验证所有 h=1..H 都有可行击杀与后继鲁棒保证；不读取真实未揭示血量。
 */
int robustUnknownValue(int unknownBossCount, int remainingTurns, const std::vector<int> &cooldown,
                       const std::vector<Skill> &skills, int hMax, PlannerMemo &memo)
{
    if (unknownBossCount == 0) return remainingTurns >= 0 ? hMax : 0;
    if (remainingTurns <= 0) return 0;

    const std::string memoKey = valueKey(unknownBossCount, remainingTurns, cooldown);
    if (const auto it = memo.robustUnknown.find(memoKey); it != memo.robustUnknown.end()) return it->second;

    for (int candidateH = hMax; candidateH >= 0; --candidateH) {
        bool allHpHandled = true;
        for (int hp = 1; hp <= candidateH && allHpHandled; ++hp) {
            bool hpHandled = false;
            for (int turns = 1; turns <= remainingTurns && !hpHandled; ++turns) {
                for (const auto &kill : enumerateKillExactTurns(hp, cooldown, skills, turns, memo)) {
                    const int nextValue = robustUnknownValue(unknownBossCount - 1, remainingTurns - turns,
                                                             kill.cooldownAfter, skills, hMax, memo);
                    if (nextValue >= candidateH) {
                        hpHandled = true;
                        break;
                    }
                }
            }
            allHpHandled = hpHandled;
        }
        if (allHpHandled) {
            memo.robustUnknown[memoKey] = candidateH;
            return candidateH;
        }
    }

    memo.robustUnknown[memoKey] = 0;
    return 0;
}

/**
 * 功能：比较两个 Boss 候选方案。
 * 输入：
 *   - best：当前最优候选。
 *   - candidate：待比较候选。
 * 输出：
 *   - 返回 candidate 是否更优。
 * 关键逻辑：
 *   - 首先比较 unknown-suffix robust score；只有分数相同时才用回合、冷却成本、可用伤害和字典序稳定排序。
 */
bool betterBossPlanByRobustValue(const BossPlanCandidate &best, const BossPlanCandidate &candidate)
{
    if (!candidate.ok) return false;
    if (!best.ok) return true;
    if (candidate.robustScore != best.robustScore) return candidate.robustScore > best.robustScore;
    if (candidate.turns != best.turns) return candidate.turns < best.turns;
    if (candidate.cooldownCostAfter != best.cooldownCostAfter) {
        return candidate.cooldownCostAfter < best.cooldownCostAfter;
    }
    if (candidate.readyDamageAfter != best.readyDamageAfter) {
        return candidate.readyDamageAfter > best.readyDamageAfter;
    }
    return candidate.sequence < best.sequence;
}

/**
 * 功能：把 Boss 候选方案转为调试 JSON。
 * 输入：
 *   - candidate：候选方案。
 *   - remainingTurnsAfter：执行该方案后的剩余回合数。
 * 输出：
 *   - 返回包含 sequence、turns、cooldownAfter、robustScore 等字段的 JSON。
 * 关键逻辑：
 *   - candidateScores 只用于解释选择过程，不参与后续计算。
 */
Json candidateDebugJson(const BossPlanCandidate &candidate, int remainingTurnsAfter)
{
    return {{"sequence", candidate.sequence},
            {"turns", candidate.turns},
            {"cooldownAfter", candidate.cooldownAfter},
            {"cooldownCostAfter", candidate.cooldownCostAfter},
            {"readyDamageAfter", candidate.readyDamageAfter},
            {"remainingTurnsAfter", remainingTurnsAfter},
            {"robustScore", candidate.robustScore}};
}

/**
 * 功能：从当前 Boss 下标开始，为当前已揭示前缀选择本阶段最优方案。
 * 输入：
 *   - currentBossIndex：当前要打的 Boss 下标。
 *   - usedTurns：当前 attempt 已经使用的回合数。
 *   - startCooldown：当前冷却状态。
 *   - knownHPs：已揭示 Boss 血量前缀。
 *   - totalBossCount：Boss 总数量。
 *   - minRounds：总回合限制。
 *   - skills：技能列表。
 *   - hMax：未知 Boss 先验血量上界。
 *   - memo：递推缓存。
 *   - candidateScores：输出候选调试表。
 * 输出：
 *   - 返回当前 Boss 的最优阶段计划。
 * 关键逻辑：
 *   - 当前 Boss 血量只从 knownHPs[currentBossIndex] 读取；评分时只使用已揭示前缀和未知后缀鲁棒值。
 */
BossPlanCandidate solveCurrentBossByRobustValue(int currentBossIndex, int usedTurns,
                                                const std::vector<int> &startCooldown,
                                                const std::vector<int> &knownHPs, int totalBossCount, int minRounds,
                                                const std::vector<Skill> &skills, int hMax, PlannerMemo &memo,
                                                Json &candidateScores)
{
    BossPlanCandidate best;
    candidateScores = Json::array();
    if (currentBossIndex < 0 || currentBossIndex >= static_cast<int>(knownHPs.size())) return best;

    const int maxTurns = minRounds >= 0 ? minRounds - usedTurns : 20;
    if (maxTurns <= 0) return best;

    for (auto candidate :
         enumerateKillUpToTurns(knownHPs[currentBossIndex], startCooldown, skills, maxTurns, memo)) {
        const int remainingTurnsAfter = maxTurns - candidate.turns;
        candidate.robustScore =
            knownPrefixValue(currentBossIndex + 1, remainingTurnsAfter, candidate.cooldownAfter, knownHPs,
                             totalBossCount, skills, hMax, memo);
        candidateScores.push_back(candidateDebugJson(candidate, remainingTurnsAfter));
        if (betterBossPlanByRobustValue(best, candidate)) best = std::move(candidate);
    }
    return best;
}

/**
 * 功能：构造当前已知 Boss 血量数组。
 * 输入：
 *   - knownHPs：已经揭示的 Boss 血量前缀。
 *   - totalBossCount：Boss 总数量。
 * 输出：
 *   - 返回 JSON 数组，未知 Boss 用 null 表示。
 * 关键逻辑：
 *   - 输出调试信息时必须体现“已知保持已知，未知保持未知”的规则。
 */
Json knownHpJson(const std::vector<int> &knownHPs, int totalBossCount)
{
    Json known = Json::array();
    for (int i = 0; i < totalBossCount; ++i) {
        known.push_back(i < static_cast<int>(knownHPs.size()) ? Json(knownHPs[i]) : Json(nullptr));
    }
    return known;
}

/**
 * 功能：解析玩家技能数组。
 * 输入：
 *   - source：任务 JSON。
 * 输出：
 *   - 返回按输入顺序编号的技能列表。
 * 关键逻辑：
 *   - 技能 id 必须与前端显示和输出 sequence 的编号一致。
 */
std::vector<Skill> parseSkills(const Json &source)
{
    std::vector<Skill> skills;
    for (int i = 0; i < static_cast<int>(source["PlayerSkills"].size()); ++i) {
        const auto &item = source["PlayerSkills"][i];
        if (!item.is_array() || item.size() != 2) throw std::runtime_error("invalid PlayerSkills item");
        skills.push_back({i, item[0].get<int>(), item[1].get<int>()});
    }
    return skills;
}

} // namespace

/**
 * 功能：按顺序揭示规则求解 Boss 战策略。
 * 输入：
 *   - source：任务 JSON，包含 B 血量数组、PlayerSkills 技能数组，可选 minRounds 和 CoinConsumption。
 * 输出：
 *   - 返回 JSON，包含总序列、总回合、每个 Boss 被揭示时的鲁棒阶段方案和复活规则字段。
 * 关键逻辑：
 *   - 一开始只知道第一只 Boss；失败复活后保留已揭示血量，重新从 Boss0 规划。
 *   - 当前阶段候选用已知前缀 F 和未知后缀 G 评分，不读取 knownHPs 之外的真实 B。
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
    try {
        skills = parseSkills(source);
    } catch (const std::exception &ex) {
        return {{"ok", false}, {"error", ex.what()}};
    }
    if (skills.empty()) return {{"ok", false}, {"error", "PlayerSkills must not be empty"}};

    const int minRounds = readMinRounds(source);
    const int totalBossCount = static_cast<int>(bossHPs.size());
    const int maxAttempts = source.value("maxBossAttempts", std::max(1, totalBossCount));
    const int coinConsumption = readCoinConsumption(source);

    std::vector<int> knownHPs{bossHPs[0]};
    Json attempts = Json::array();

    for (int attemptIndex = 0; attemptIndex < maxAttempts; ++attemptIndex) {
        int bossIndex = 0;
        int usedTurns = 0;
        std::vector<int> cooldown(skills.size(), 0);
        std::vector<int> sequence;
        const std::vector<int> attemptKnownHPsAtStart = knownHPs;
        Json phases = Json::array();
        bool attemptFailed = false;
        std::string failureReason;

        while (bossIndex < totalBossCount) {
            if (bossIndex >= static_cast<int>(knownHPs.size())) {
                knownHPs.push_back(bossHPs[bossIndex]);
            }

            PlannerMemo memo;
            const int hMax = computeUnknownBossHpMax(source, skills, knownHPs);
            Json candidateScores = Json::array();
            BossPlanCandidate plan =
                solveCurrentBossByRobustValue(bossIndex, usedTurns, cooldown, knownHPs, totalBossCount, minRounds,
                                              skills, hMax, memo, candidateScores);
            if (!plan.ok || plan.robustScore < 0) {
                attemptFailed = true;
                failureReason = "boss battle has no robust-feasible plan for current known prefix";
                phases.push_back({{"bossIndex", bossIndex},
                                  {"revealedHp", knownHPs[bossIndex]},
                                  {"knownBossHPsBeforeFight", knownHpJson(knownHPs, totalBossCount)},
                                  {"knownCount", static_cast<int>(knownHPs.size())},
                                  {"unknownBossCount", totalBossCount - static_cast<int>(knownHPs.size())},
                                  {"hMax", hMax},
                                  {"turnStart", usedTurns},
                                  {"candidateScores", candidateScores},
                                  {"failed", true},
                                  {"failureReason", failureReason}});
                break;
            }

            Json phase{{"bossIndex", bossIndex},
                       {"revealedHp", knownHPs[bossIndex]},
                       {"knownBossHPsBeforeFight", knownHpJson(knownHPs, totalBossCount)},
                       {"knownCount", static_cast<int>(knownHPs.size())},
                       {"unknownBossCount", totalBossCount - static_cast<int>(knownHPs.size())},
                       {"hMax", hMax},
                       {"turnStart", usedTurns},
                       {"turns", plan.turns},
                       {"sequence", plan.sequence},
                       {"cooldownAfter", plan.cooldownAfter},
                       {"cooldownCostAfter", plan.cooldownCostAfter},
                       {"readyDamageAfter", plan.readyDamageAfter},
                       {"robustScore", plan.robustScore},
                       {"candidateScores", candidateScores},
                       {"selectionReason", "max robust unknown suffix value"}};
            phases.push_back(std::move(phase));

            sequence.insert(sequence.end(), plan.sequence.begin(), plan.sequence.end());
            usedTurns += plan.turns;
            cooldown = plan.cooldownAfter;
            if (minRounds >= 0 && usedTurns > minRounds) {
                attemptFailed = true;
                failureReason = "boss battle exceeded minRounds";
                break;
            }
            ++bossIndex;
        }

        Json attempt{{"attemptIndex", attemptIndex},
                     {"knownBossHPsAtStart", knownHpJson(attemptKnownHPsAtStart, totalBossCount)},
                     {"phases", phases},
                     {"sequence", sequence},
                     {"turns", usedTurns},
                     {"failed", attemptFailed}};
        if (attemptFailed) {
            attempt["failureReason"] = failureReason;
            attempt["reviveRequired"] = true;
            attempt["reviveCoinCost"] = coinConsumption;
            attempts.push_back(std::move(attempt));
            continue;
        }

        attempts.push_back(attempt);
        Json result{{"ok", true},
                    {"algorithm", "unknown_suffix_robust_value_boss_planner"},
                    {"sequence", sequence},
                    {"turns", usedTurns},
                    {"knowledgePolicy", "sequential_reveal_current_boss_only"},
                    {"knownHpPersistenceImplemented", true},
                    {"initialKnownBossHPs", knownHpJson({bossHPs[0]}, totalBossCount)},
                    {"finalKnownBossHPs", knownHpJson(knownHPs, totalBossCount)},
                    {"phases", phases},
                    {"attempts", attempts},
                    {"CoinConsumption", coinConsumption},
                    {"reviveRule",
                     {{"coinCost", coinConsumption},
                      {"restartPosition", "S"},
                      {"knownHpPersistence", "revealed boss HP remains known; unrevealed boss HP remains unknown"}}}};

        if (minRounds >= 0) {
            result["minRounds"] = minRounds;
            result["withinMinRounds"] = usedTurns <= minRounds;
            result["matchesMinRounds"] = usedTurns == minRounds;
        }
        return result;
    }

    return {{"ok", false},
            {"error", "boss battle has no solution after revive-aware replanning"},
            {"algorithm", "unknown_suffix_robust_value_boss_planner"},
            {"knowledgePolicy", "sequential_reveal_current_boss_only"},
            {"knownHpPersistenceImplemented", true},
            {"initialKnownBossHPs", knownHpJson({bossHPs[0]}, totalBossCount)},
            {"finalKnownBossHPs", knownHpJson(knownHPs, totalBossCount)},
            {"attempts", attempts},
            {"CoinConsumption", coinConsumption},
            {"reviveRule",
             {{"coinCost", coinConsumption},
              {"restartPosition", "S"},
              {"knownHpPersistence", "revealed boss HP remains known; unrevealed boss HP remains unknown"}}}};
}

} // namespace ai_player
