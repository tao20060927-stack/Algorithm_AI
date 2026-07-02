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
    int lightScore = -1;
    int remainingTurnsAfter = 0;
    int remainingUnknownBossCount = 0;
};

struct PlannerMemo {
    std::map<std::string, std::vector<BossPlanCandidate>> killExact;
};

inline constexpr int kDefaultLightBossTurns = 20;
inline constexpr int kLightCapacityInfinity = 1000000000;

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
 *   - Boss 单体击杀枚举大量复用同一冷却状态，用字符串键避免为 vector 自定义哈希。
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
 * 功能：构造单 Boss 枚举中的中间状态 key。
 * 输入：
 *   - hp：当前 Boss 剩余血量。
 *   - cooldown：当前技能冷却。
 * 输出：
 *   - 返回同一层 BFS/DP 内用于去重的 key。
 * 关键逻辑：
 *   - 同一回合层里，hp 和 cooldown 相同的状态后续可达集合完全一致，只保留字典序最小序列即可避免动作序列爆炸。
 */
std::string liveStateKey(int hp, const std::vector<int> &cooldown)
{
    std::ostringstream key;
    key << hp << '|' << vectorKey(cooldown);
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
 *   - 该值只作为 lightScore 相同时的稳定 tie-break，不再压过回合数和轻量后缀容量。
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
 * 功能：计算每回合不可避免的最小正伤害。
 * 输入：
 *   - skills：技能列表。
 * 输出：
 *   - 若存在冷却为 0 且伤害为正的普通技能，返回其中最小伤害；否则返回 0。
 * 关键逻辑：
 *   - 冷却 0 技能每回合都可用，且有技能可用时不允许等待，因此 Boss 不可能被拖过该最小伤害允许的最长存活回合。
 */
int minimumForcedDamagePerTurn(const std::vector<Skill> &skills)
{
    int damage = 0;
    for (const Skill &skill : skills) {
        if (skill.cooldown != 0 || skill.damage <= 0) continue;
        damage = damage == 0 ? skill.damage : std::min(damage, skill.damage);
    }
    return damage;
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
    const int forcedDamage = minimumForcedDamagePerTurn(skills);
    if (forcedDamage > 0 && (exactTurns - 1) * forcedDamage >= hp) return {};
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
        std::map<std::string, State> nextDedup;
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
                    const std::string key = liveStateKey(nextHp, nextCooldown);
                    const auto old = nextDedup.find(key);
                    if (old == nextDedup.end() || nextSequence < old->second.sequence) {
                        nextDedup[key] = {nextHp, std::move(nextCooldown), std::move(nextSequence)};
                    }
                }
            }
        }
        std::vector<State> nextStates;
        nextStates.reserve(nextDedup.size());
        for (auto &item : nextDedup) nextStates.push_back(std::move(item.second));
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
    const int forcedDamage = minimumForcedDamagePerTurn(skills);
    if (forcedDamage > 0) maxTurns = std::min(maxTurns, (hp + forcedDamage - 1) / forcedDamage);
    for (int turns = 1; turns <= maxTurns; ++turns) {
        auto exact = enumerateKillExactTurns(hp, startCooldown, skills, turns, memo);
        result.insert(result.end(), exact.begin(), exact.end());
    }
    return result;
}

/**
 * 功能：从指定冷却状态出发，计算未来每个回合预算内的最大累计伤害。
 * 输入：
 *   - startCooldown：当前技能冷却状态。
 *   - maxTurns：最多向前估计的回合数，必须非负。
 *   - skills：技能列表。
 * 输出：
 *   - 返回 bestDamage，bestDamage[k] 表示 k 回合内可达到的最大累计伤害。
 * 关键逻辑：
 *   - 使用 cooldown -> 最大累计伤害的动态规划；每回合复用 availableBossActions 和 applyBossAction，保证冷却语义与真实 Boss 战一致。
 */
std::vector<int> maxDamageProfileFromCooldown(const std::vector<int> &startCooldown, int maxTurns,
                                              const std::vector<Skill> &skills)
{
    std::vector<int> bestDamage(std::max(0, maxTurns) + 1, 0);
    if (maxTurns <= 0 || skills.empty()) return bestDamage;

    std::map<std::vector<int>, int> states;
    states[startCooldown] = 0;
    for (int turn = 1; turn <= maxTurns; ++turn) {
        std::map<std::vector<int>, int> nextStates;
        for (const auto &[cooldown, totalDamage] : states) {
            for (const int action : availableBossActions(cooldown, skills)) {
                int ignoredHp = 0;
                std::vector<int> nextCooldown;
                applyBossAction(0, cooldown, skills, action, ignoredHp, nextCooldown);
                const int nextDamage = totalDamage + (action >= 0 ? skills[action].damage : 0);
                auto &slot = nextStates[nextCooldown];
                slot = std::max(slot, nextDamage);
                bestDamage[turn] = std::max(bestDamage[turn], nextDamage);
            }
        }
        bestDamage[turn] = std::max(bestDamage[turn], bestDamage[turn - 1]);
        states = std::move(nextStates);
        if (states.empty()) {
            for (int rest = turn + 1; rest <= maxTurns; ++rest) bestDamage[rest] = bestDamage[turn];
            break;
        }
    }
    return bestDamage;
}

/**
 * 功能：估计未知 Boss 后缀在剩余回合内可承受的单 Boss 血量阈值。
 * 输入：
 *   - cooldown：进入未知后缀前的技能冷却。
 *   - remainingTurns：剩余总回合数。
 *   - unknownCount：仍未揭示的 Boss 数量。
 *   - skills：技能列表。
 * 输出：
 *   - 返回轻量容量分 lightScore；不可行时返回 -1，无未知 Boss 时返回极大值。
 * 关键逻辑：
 *   - 不枚举未知 Boss 血量，只用伤害容量曲线按时间切片估计未来后缀的保底处理能力。
 */
int suffixCapacityThreshold(const std::vector<int> &cooldown, int remainingTurns, int unknownCount,
                            const std::vector<Skill> &skills)
{
    if (unknownCount == 0) return kLightCapacityInfinity;
    if (remainingTurns < unknownCount || remainingTurns <= 0) return -1;

    const std::vector<int> damageProfile = maxDamageProfileFromCooldown(cooldown, remainingTurns, skills);
    int threshold = kLightCapacityInfinity;
    for (int s = 1; s <= unknownCount; ++s) {
        const int deadline = (s * remainingTurns + unknownCount - 1) / unknownCount;
        const int avgCapacity = damageProfile[deadline] / s;
        threshold = std::min(threshold, avgCapacity);
    }
    return threshold;
}

/**
 * 功能：在已知 Boss 后缀中选择一个轻量滚动方案。
 * 输入：
 *   - candidates：当前已知 Boss 在回合限制内的击杀候选。
 * 输出：
 *   - 返回用于推进轻量后缀模拟的候选；无候选时 ok=false。
 * 关键逻辑：
 *   - 已知后缀只负责快速推进状态，比较顺序为回合少、冷却成本低、可用伤害高、序列字典序小。
 */
BossPlanCandidate chooseBestKnownSuffixCandidate(const std::vector<BossPlanCandidate> &candidates)
{
    BossPlanCandidate best;
    for (const BossPlanCandidate &candidate : candidates) {
        if (!candidate.ok) continue;
        if (!best.ok || candidate.turns < best.turns ||
            (candidate.turns == best.turns && candidate.cooldownCostAfter < best.cooldownCostAfter) ||
            (candidate.turns == best.turns && candidate.cooldownCostAfter == best.cooldownCostAfter &&
             candidate.readyDamageAfter > best.readyDamageAfter) ||
            (candidate.turns == best.turns && candidate.cooldownCostAfter == best.cooldownCostAfter &&
             candidate.readyDamageAfter == best.readyDamageAfter && candidate.sequence < best.sequence)) {
            best = candidate;
        }
    }
    return best;
}

/**
 * 功能：统一评估当前候选执行后的已知后缀和未知后缀轻量容量。
 * 输入：
 *   - currentBossIndex：当前候选已经击败的 Boss 下标。
 *   - remainingTurns：执行当前候选后的剩余回合。
 *   - cooldown：执行当前候选后的技能冷却。
 *   - knownHPs：当前已揭示 Boss 血量前缀。
 *   - totalBossCount：Boss 总数量。
 *   - skills：技能列表。
 * 输出：
 *   - 返回 lightScore；已知后缀无法在保留每个后续 Boss 至少一回合的情况下击败时返回 -1。
 * 关键逻辑：
 *   - 已揭示的后缀 Boss 用真实已知血量推进；未揭示后缀只用容量阈值估计，不读取未来 B 数组。
 */
int evaluateSuffixLightCapacity(int currentBossIndex, int remainingTurns, std::vector<int> cooldown,
                                const std::vector<int> &knownHPs, int totalBossCount,
                                const std::vector<Skill> &skills)
{
    int nextIndex = currentBossIndex + 1;
    PlannerMemo memo;
    while (nextIndex < static_cast<int>(knownHPs.size())) {
        const int remainingBossesAfterThis = totalBossCount - nextIndex - 1;
        const int phaseLimit = remainingTurns - remainingBossesAfterThis;
        if (phaseLimit <= 0) return -1;

        auto candidates = enumerateKillUpToTurns(knownHPs[nextIndex], cooldown, skills, phaseLimit, memo);
        BossPlanCandidate bestKnown = chooseBestKnownSuffixCandidate(candidates);
        if (!bestKnown.ok) return -1;

        remainingTurns -= bestKnown.turns;
        cooldown = bestKnown.cooldownAfter;
        ++nextIndex;
    }

    return suffixCapacityThreshold(cooldown, remainingTurns, totalBossCount - nextIndex, skills);
}

/**
 * 功能：比较两个滚动视野伤害容量候选。
 * 输入：
 *   - best：当前最优候选。
 *   - candidate：待比较候选。
 * 输出：
 *   - candidate 更优时返回 true。
 * 关键逻辑：
 *   - 主优先级为 lightScore，随后才用回合数、冷却成本、可用伤害和字典序稳定选择。
 */
bool betterBossPlanByLightCapacity(const BossPlanCandidate &best, const BossPlanCandidate &candidate)
{
    if (!candidate.ok || candidate.lightScore < 0) return false;
    if (!best.ok) return true;
    if (candidate.lightScore != best.lightScore) return candidate.lightScore > best.lightScore;
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
 * 功能：把轻量容量候选方案转为调试 JSON。
 * 输入：
 *   - candidate：候选方案。
 * 输出：
 *   - 返回包含 lightScore、剩余回合和未知 Boss 数量的调试对象。
 * 关键逻辑：
 *   - candidateScores 用于解释滚动视野选择过程，不参与后续计算。
 */
Json candidateLightDebugJson(const BossPlanCandidate &candidate)
{
    return {{"sequence", candidate.sequence},
            {"turns", candidate.turns},
            {"cooldownAfter", candidate.cooldownAfter},
            {"cooldownCostAfter", candidate.cooldownCostAfter},
            {"readyDamageAfter", candidate.readyDamageAfter},
            {"remainingTurnsAfter", candidate.remainingTurnsAfter},
            {"remainingUnknownBossCount", candidate.remainingUnknownBossCount},
            {"lightScore", candidate.lightScore}};
}

/**
 * 功能：为当前已揭示 Boss 选择滚动视野伤害容量最优方案。
 * 输入：
 *   - currentBossIndex：当前要打的 Boss 下标。
 *   - usedTurns：本次 attempt 已经使用的回合数。
 *   - startCooldown：当前技能冷却。
 *   - knownHPs：已揭示 Boss 血量前缀。
 *   - totalBossCount：Boss 总数量。
 *   - minRounds：总回合限制；不存在时使用默认轻量预算。
 *   - skills：技能列表。
 *   - candidateScores：输出候选评分调试表。
 * 输出：
 *   - 返回当前 Boss 的最优轻量容量方案。
 * 关键逻辑：
 *   - 先找当前 Boss 最短击杀回合，再只允许少量 localSlack；后缀用 lightScore 评价，不枚举未知 Boss 血量。
 */
BossPlanCandidate solveCurrentBossByLightCapacity(int currentBossIndex, int usedTurns,
                                                  const std::vector<int> &startCooldown,
                                                  const std::vector<int> &knownHPs, int totalBossCount,
                                                  int minRounds, const std::vector<Skill> &skills,
                                                  Json &candidateScores)
{
    BossPlanCandidate best;
    candidateScores = Json::array();
    if (currentBossIndex < 0 || currentBossIndex >= static_cast<int>(knownHPs.size())) return best;

    const int maxTurns = minRounds >= 0 ? minRounds - usedTurns : kDefaultLightBossTurns;
    if (maxTurns <= 0) return best;

    PlannerMemo memo;
    const auto allCandidates = enumerateKillUpToTurns(knownHPs[currentBossIndex], startCooldown, skills, maxTurns, memo);
    if (allCandidates.empty()) return best;

    int minTurns = maxTurns + 1;
    for (const BossPlanCandidate &candidate : allCandidates) {
        if (candidate.ok) minTurns = std::min(minTurns, candidate.turns);
    }
    if (minTurns > maxTurns) return best;

    const int remainingBosses = totalBossCount - currentBossIndex - 1;
    const int minFutureTurns = remainingBosses;
    const int spareAfterMinPlan = maxTurns - minTurns - minFutureTurns;
    int localSlack = 0;
    if (spareAfterMinPlan >= 2) localSlack = 1;
    if (spareAfterMinPlan >= 5) localSlack = 2;
    const int candidateTurnLimit = std::min(maxTurns, minTurns + localSlack);

    for (auto candidate : allCandidates) {
        if (!candidate.ok || candidate.turns > candidateTurnLimit) continue;
        candidate.remainingTurnsAfter = maxTurns - candidate.turns;
        candidate.remainingUnknownBossCount = totalBossCount - static_cast<int>(knownHPs.size());
        candidate.lightScore = evaluateSuffixLightCapacity(currentBossIndex, candidate.remainingTurnsAfter,
                                                           candidate.cooldownAfter, knownHPs, totalBossCount, skills);
        candidateScores.push_back(candidateLightDebugJson(candidate));
        if (betterBossPlanByLightCapacity(best, candidate)) best = std::move(candidate);
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
 *   - 返回 JSON，包含总序列、总回合、每个 Boss 被揭示时的滚动视野阶段方案和复活规则字段。
 * 关键逻辑：
 *   - 一开始只知道第一只 Boss；失败复活后保留已揭示血量，重新从 Boss0 规划。
 *   - 当前阶段候选用轻量伤害容量评分，不读取 knownHPs 之外的真实 B。
 */
Json runBossBattleJson(const Json &source)
{
    if (!source.contains("B") || !source["B"].is_array()) {
        return {{"ok", false}, {"error", "missing B boss HP array"}};
    }
    if (!source.contains("PlayerSkills") || !source["PlayerSkills"].is_array()) {
        return {{"ok", false}, {"error", "missing PlayerSkills array"}};
    }

    static std::map<std::string, Json> bossResultCache;
    const std::string cacheKey = source.dump();
    if (const auto cached = bossResultCache.find(cacheKey); cached != bossResultCache.end()) {
        return cached->second;
    }
    const auto cacheAndReturn = [&](Json result) {
        bossResultCache[cacheKey] = result;
        return result;
    };

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

            Json candidateScores = Json::array();
            BossPlanCandidate plan =
                solveCurrentBossByLightCapacity(bossIndex, usedTurns, cooldown, knownHPs, totalBossCount, minRounds,
                                                skills, candidateScores);
            if (!plan.ok || plan.lightScore < 0) {
                attemptFailed = true;
                failureReason = "boss battle has no light-capacity-feasible plan for current known prefix";
                phases.push_back({{"bossIndex", bossIndex},
                                  {"revealedHp", knownHPs[bossIndex]},
                                  {"knownBossHPsBeforeFight", knownHpJson(knownHPs, totalBossCount)},
                                  {"knownCount", static_cast<int>(knownHPs.size())},
                                  {"unknownBossCount", totalBossCount - static_cast<int>(knownHPs.size())},
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
                       {"turnStart", usedTurns},
                       {"turns", plan.turns},
                       {"sequence", plan.sequence},
                       {"cooldownAfter", plan.cooldownAfter},
                       {"cooldownCostAfter", plan.cooldownCostAfter},
                       {"readyDamageAfter", plan.readyDamageAfter},
                       {"lightScore", plan.lightScore},
                       {"remainingTurnsAfter", plan.remainingTurnsAfter},
                       {"remainingUnknownBossCount", plan.remainingUnknownBossCount},
                       {"candidateScores", candidateScores},
                       {"selectionReason", "max light suffix damage capacity under remaining turns"}};
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
                    {"algorithm", "rolling_horizon_damage_capacity_boss_planner"},
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
        return cacheAndReturn(result);
    }

    return cacheAndReturn({{"ok", false},
                            {"error", "boss battle has no solution after revive-aware replanning"},
                           {"algorithm", "rolling_horizon_damage_capacity_boss_planner"},
                           {"knowledgePolicy", "sequential_reveal_current_boss_only"},
                           {"knownHpPersistenceImplemented", true},
                           {"initialKnownBossHPs", knownHpJson({bossHPs[0]}, totalBossCount)},
                           {"finalKnownBossHPs", knownHpJson(knownHPs, totalBossCount)},
                           {"attempts", attempts},
                           {"CoinConsumption", coinConsumption},
                           {"reviveRule",
                            {{"coinCost", coinConsumption},
                             {"restartPosition", "S"},
                             {"knownHpPersistence",
                              "revealed boss HP remains known; unrevealed boss HP remains unknown"}}}});
}

} // namespace ai_player
