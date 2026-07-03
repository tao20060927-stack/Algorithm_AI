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
    // 用逗号拼接整数数组，生成稳定的字符串键，
    // 避免为 std::vector<int> 实现自定义哈希函数
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
    // 键格式：hp|exactTurns|cd0,cd1,...
    // 同一 (hp, cooldown, exactTurns) 的击杀终态集合完全一致，可安全缓存
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
    // 键格式：hp|cd0,cd1,cd2,...
    // 同一回合层内，hp 和 cooldown 相同的状态后续可达集合完全一致，
    // 只保留字典序最小序列即可避免动作序列爆炸
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
        // 冷却成本 = 剩余冷却回合 x 技能伤害 x (技能冷却时长 + 1)
        // 高伤害、长冷却的技能在冷却中时损失更大，因此权重更高
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
        // 统计冷却为 0 的技能总伤害：这些技能在下个 Boss 的第一回合立即可用
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
    // 第一步：若执行技能（非等待），立即造成伤害
    if (action >= 0) nextHp -= skills[action].damage;
    // 第二步：所有技能的冷却回合数减 1（冷却自然衰减）
    for (int &value : nextCooldown) {
        if (value > 0) --value;
    }
    // 第三步：使用的技能进入其自身的冷却周期
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
    // 收集所有冷却为 0 的技能（当前回合可用）
    for (const Skill &skill : skills) {
        if (cooldown[skill.id] == 0) actions.push_back(skill.id);
    }
    // 若无可用技能，只能等待（action = -1 表示等待一回合）
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
        // 只考虑冷却为 0 且伤害为正的技能（这些技能每回合都可用）
        if (skill.cooldown != 0 || skill.damage <= 0) continue;
        // 取最小正伤害：这是 Boss 每回合至少承受的伤害量，用于剪枝和回合上限估计
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
    // 计算每回合不可避免的最小正伤害，用于剪枝和加速搜索
    const int forcedDamage = minimumForcedDamagePerTurn(skills);
    // 剪枝：若在 exactTurns-1 回合内最小强制伤害已足够击杀 Boss，
    // 则存在更短击杀路径，当前 exactTurns 层无需搜索
    if (forcedDamage > 0 && (exactTurns - 1) * forcedDamage >= hp) return {};
    const std::string memoKey = killKey(hp, startCooldown, exactTurns);
    // 缓存命中：相同 (hp, cooldown, exactTurns) 的枚举结果可直接复用，
    // 避免重复展开相同的状态空间
    if (const auto it = memo.killExact.find(memoKey); it != memo.killExact.end()) return it->second;

    // BFS 状态定义：当前剩余血量、技能冷却数组、已执行的动作序列
    struct State {
        int hp = 0;
        std::vector<int> cooldown;
        std::vector<int> sequence;
    };

    // 初始 BFS 层：满血、起始冷却、空动作序列
    std::vector<State> states{{hp, startCooldown, {}}};
    // 最终回合的去重表：键为冷却终态，值为该终态下字典序最小的击杀方案
    std::map<std::string, BossPlanCandidate> dedup;
    // 逐层 BFS：每层代表执行一个回合的动作选择
    for (int turn = 0; turn < exactTurns; ++turn) {
        // 下一层状态去重表：同一 (hp, cooldown) 状态只保留字典序最小的动作序列
        std::map<std::string, State> nextDedup;
        for (const State &state : states) {
            // 已死亡状态不再扩展：要求恰好 exactTurns 回合首次击杀
            if (state.hp <= 0) continue;
            // 枚举当前冷却下的所有合法动作，每个动作产生一个状态分支
            for (const int action : availableBossActions(state.cooldown, skills)) {
                int nextHp = state.hp;
                std::vector<int> nextCooldown;
                // 模拟执行一个动作：扣血、所有冷却减 1、使用技能进入冷却周期
                applyBossAction(state.hp, state.cooldown, skills, action, nextHp, nextCooldown);
                std::vector<int> nextSequence = state.sequence;
                nextSequence.push_back(action);

                const bool lastTurn = turn + 1 == exactTurns;
                // 非最后一回合时 Boss 死亡视为过早击杀，不满足"恰好"约束，跳过
                if (!lastTurn && nextHp <= 0) continue;
                if (lastTurn) {
                    // 最后一回合：Boss 必须在此时恰好死亡，未死亡则分支无效
                    if (nextHp > 0) continue;
                    BossPlanCandidate candidate;
                    candidate.ok = true;
                    candidate.turns = exactTurns;
                    candidate.sequence = std::move(nextSequence);
                    candidate.cooldownAfter = std::move(nextCooldown);
                    // 计算战后冷却成本和立即可用伤害，供后续阶段方案比较使用
                    candidate.cooldownCostAfter = cooldownCostAfter(candidate.cooldownAfter, skills);
                    candidate.readyDamageAfter = readyDamageAfter(candidate.cooldownAfter, skills);
                    // 用冷却终态作为去重键：相同冷却的方案在进入下一 Boss 时完全等价
                    const std::string key = vectorKey(candidate.cooldownAfter);
                    const auto old = dedup.find(key);
                    // 同一冷却终态保留字典序最小的序列，减少后续阶段的分支组合爆炸
                    if (old == dedup.end() || candidate.sequence < old->second.sequence) {
                        dedup[key] = std::move(candidate);
                    }
                } else {
                    // 中间回合：用 (hp, cooldown) 去重，同一状态只保留字典序最小序列
                    const std::string key = liveStateKey(nextHp, nextCooldown);
                    const auto old = nextDedup.find(key);
                    if (old == nextDedup.end() || nextSequence < old->second.sequence) {
                        nextDedup[key] = {nextHp, std::move(nextCooldown), std::move(nextSequence)};
                    }
                }
            }
        }
        // 将去重后的下一层状态收集为 vector，作为下一次迭代的输入
        std::vector<State> nextStates;
        nextStates.reserve(nextDedup.size());
        for (auto &item : nextDedup) nextStates.push_back(std::move(item.second));
        states = std::move(nextStates);
        // 如果没有可继续的中间状态，提前终止 BFS（后续回合不可能产生有效击杀）
        if (states.empty() && turn + 1 < exactTurns) break;
    }

    // 收集所有去重后的最终击杀方案
    std::vector<BossPlanCandidate> result;
    for (auto &item : dedup) result.push_back(std::move(item.second));
    // 按回合数升序、序列字典序升序排列，供上层选择合适的回合预算
    std::sort(result.begin(), result.end(), [](const BossPlanCandidate &left, const BossPlanCandidate &right) {
        if (left.turns != right.turns) return left.turns < right.turns;
        return left.sequence < right.sequence;
    });
    // 缓存结果：同一 (hp, cooldown, exactTurns) 不重复枚举
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
    // 用最小强制伤害剪枝：理论上最短击杀回合数 = ceil(hp / 每回合最小伤害)
    if (forcedDamage > 0) maxTurns = std::min(maxTurns, (hp + forcedDamage - 1) / forcedDamage);
    // 枚举 t = 1..maxTurns 的所有精确回合击杀方案
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
    // bestDamage[k] 表示在 k 回合内可达到的最大累计伤害
    std::vector<int> bestDamage(std::max(0, maxTurns) + 1, 0);
    if (maxTurns <= 0 || skills.empty()) return bestDamage;

    // DP 状态映射：冷却数组 -> 累计伤害，去重时保留最大伤害
    std::map<std::vector<int>, int> states;
    states[startCooldown] = 0;
    // 逐回合 DP：每回合从所有可达冷却状态出发，尝试每个合法动作
    for (int turn = 1; turn <= maxTurns; ++turn) {
        std::map<std::vector<int>, int> nextStates;
        for (const auto &[cooldown, totalDamage] : states) {
            for (const int action : availableBossActions(cooldown, skills)) {
                int ignoredHp = 0;
                std::vector<int> nextCooldown;
                // applyBossAction 的 hp 参数在此仅用于驱动冷却状态转移，
                // 伤害最大值问题不关心具体血量，传 0 即可
                applyBossAction(0, cooldown, skills, action, ignoredHp, nextCooldown);
                const int nextDamage = totalDamage + (action >= 0 ? skills[action].damage : 0);
                // 同一冷却终态保留最大累计伤害（最优子结构）
                auto &slot = nextStates[nextCooldown];
                slot = std::max(slot, nextDamage);
                // 更新当前回合的最优值
                bestDamage[turn] = std::max(bestDamage[turn], nextDamage);
            }
        }
        // 单调性保证：更多回合不会产生更少伤害（至少可以等待）
        bestDamage[turn] = std::max(bestDamage[turn], bestDamage[turn - 1]);
        states = std::move(nextStates);
        // 状态耗尽：后续回合伤害不再增长，直接填充到数组末尾
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
    // 没有未知 Boss：无容量约束，返回极大值
    if (unknownCount == 0) return kLightCapacityInfinity;
    // 回合数不足：每个未知 Boss 至少需要 1 回合，无法满足则不可行
    if (remainingTurns < unknownCount || remainingTurns <= 0) return -1;

    // 计算从当前冷却出发，在 1..remainingTurns 回合内的最大累计伤害曲线
    const std::vector<int> damageProfile = maxDamageProfileFromCooldown(cooldown, remainingTurns, skills);
    int threshold = kLightCapacityInfinity;
    // 对每个未知 Boss 位置，按时间切片估算其可承受的单 Boss 血量上限
    for (int s = 1; s <= unknownCount; ++s) {
        // 第 s 个未知 Boss 的截止回合：按均匀分配策略计算，公式向上取整
        const int deadline = (s * remainingTurns + unknownCount - 1) / unknownCount;
        // 前 deadline 回合内，平均每个未知 Boss 可分配到的伤害值
        const int avgCapacity = damageProfile[deadline] / s;
        // 取所有切片的最小值作为整体容量瓶颈：最严格的阶段决定整体可行性
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
        // 已知后缀方案选择策略（按优先级）：
        // 回合少 > 冷却成本低 > 立即可用伤害高 > 序列字典序小
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
    // 逐个推进已揭示的后缀 Boss，用真实已知血量模拟击杀
    while (nextIndex < static_cast<int>(knownHPs.size())) {
        // 当前 Boss 的回合预算：剩余总回合减去还未处理的 Boss 各预留至少 1 回合
        const int remainingBossesAfterThis = totalBossCount - nextIndex - 1;
        const int phaseLimit = remainingTurns - remainingBossesAfterThis;
        // 预算不足，后缀路径不可行
        if (phaseLimit <= 0) return -1;

        auto candidates = enumerateKillUpToTurns(knownHPs[nextIndex], cooldown, skills, phaseLimit, memo);
        // 选择已知后缀中最优的方案（优先回合少、冷却成本低）
        BossPlanCandidate bestKnown = chooseBestKnownSuffixCandidate(candidates);
        if (!bestKnown.ok) return -1;

        // 推进状态：扣除已用回合，更新冷却状态
        remainingTurns -= bestKnown.turns;
        cooldown = bestKnown.cooldownAfter;
        ++nextIndex;
    }

    // 已揭示后缀处理完毕，剩余未知 Boss 用容量阈值估计单 Boss 血量上限
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
    // 无效候选或负容量分直接排除
    if (!candidate.ok || candidate.lightScore < 0) return false;
    // best 尚未初始化时任意有效候选均更优
    if (!best.ok) return true;
    // 主优先级：轻量后缀容量分（越大越好）
    // lightScore 表示剩余回合处理未知 Boss 的保底能力，是滚动视野的核心评估指标
    if (candidate.lightScore != best.lightScore) return candidate.lightScore > best.lightScore;
    // 次优先级：回合数（越少越好，留给后缀更多时间）
    if (candidate.turns != best.turns) return candidate.turns < best.turns;
    // 第三优先级：战后冷却成本（越低越好，高伤害技能更早可用）
    if (candidate.cooldownCostAfter != best.cooldownCostAfter) {
        return candidate.cooldownCostAfter < best.cooldownCostAfter;
    }
    // 第四优先级：战后立即可用伤害（越高越好）
    if (candidate.readyDamageAfter != best.readyDamageAfter) {
        return candidate.readyDamageAfter > best.readyDamageAfter;
    }
    // 最终平局规则：字典序更小的动作序列，保证选择结果稳定可复现
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
    // 边界检查：无效的 Boss 下标直接返回空方案
    if (currentBossIndex < 0 || currentBossIndex >= static_cast<int>(knownHPs.size())) return best;

    // 计算当前 Boss 可用的回合预算：总回合限制减去已用回合
    const int maxTurns = minRounds >= 0 ? minRounds - usedTurns : kDefaultLightBossTurns;
    if (maxTurns <= 0) return best;

    // 枚举当前 Boss 在回合预算内的所有击杀方案（分支定界）
    PlannerMemo memo;
    const auto allCandidates = enumerateKillUpToTurns(knownHPs[currentBossIndex], startCooldown, skills, maxTurns, memo);
    if (allCandidates.empty()) return best;

    // 找到最快击杀回合数 minTurns（下界）
    int minTurns = maxTurns + 1;
    for (const BossPlanCandidate &candidate : allCandidates) {
        if (candidate.ok) minTurns = std::min(minTurns, candidate.turns);
    }
    if (minTurns > maxTurns) return best;

    // 计算本地松弛量 localSlack：
    // 在满足最快击杀和后续 Boss 至少各 1 回合的前提下，剩余回合可分配给当前 Boss
    const int remainingBosses = totalBossCount - currentBossIndex - 1;
    const int minFutureTurns = remainingBosses;
    const int spareAfterMinPlan = maxTurns - minTurns - minFutureTurns;
    int localSlack = 0;
    // 松弛策略：余量 >= 2 时允许多用 1 回合，>= 5 时允许多用 2 回合
    // 这样做为当前 Boss 提供合理的回合弹性，同时保证后缀 Boss 有足够时间
    if (spareAfterMinPlan >= 2) localSlack = 1;
    if (spareAfterMinPlan >= 5) localSlack = 2;
    // 候选方案的回合上限 = min(总预算, 最快击杀 + 松弛量)
    const int candidateTurnLimit = std::min(maxTurns, minTurns + localSlack);

    // 遍历所有满足回合限制的候选，用轻量后缀容量评分选出最优方案
    for (auto candidate : allCandidates) {
        if (!candidate.ok || candidate.turns > candidateTurnLimit) continue;
        candidate.remainingTurnsAfter = maxTurns - candidate.turns;
        candidate.remainingUnknownBossCount = totalBossCount - static_cast<int>(knownHPs.size());
        // 核心评估：计算执行该候选后的后缀容量——包括已知和未知 Boss
        // 已揭示的后缀用真实血量模拟，未揭示的用伤害容量曲线保守估计
        candidate.lightScore = evaluateSuffixLightCapacity(currentBossIndex, candidate.remainingTurnsAfter,
                                                           candidate.cooldownAfter, knownHPs, totalBossCount, skills);
        candidateScores.push_back(candidateLightDebugJson(candidate));
        // 按轻量容量评分规则比较（lightScore > 回合数 > 冷却成本 > 可用伤害 > 字典序）
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
    // 验证输入：必须包含 B（Boss 血量数组）和 PlayerSkills（玩家技能数组）
    if (!source.contains("B") || !source["B"].is_array()) {
        return {{"ok", false}, {"error", "missing B boss HP array"}};
    }
    if (!source.contains("PlayerSkills") || !source["PlayerSkills"].is_array()) {
        return {{"ok", false}, {"error", "missing PlayerSkills array"}};
    }

    // 静态缓存：相同任务 JSON 的结果可复用，避免重复规划
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

    // 解析技能列表：按 JSON 中的顺序编号，id 与输出 sequence 中的技能编号一致
    std::vector<Skill> skills;
    try {
        skills = parseSkills(source);
    } catch (const std::exception &ex) {
        return {{"ok", false}, {"error", ex.what()}};
    }
    if (skills.empty()) return {{"ok", false}, {"error", "PlayerSkills must not be empty"}};

    const int minRounds = readMinRounds(source);
    const int totalBossCount = static_cast<int>(bossHPs.size());
    // 默认至少生成两次尝试，保证迷宫流程能表达"第一次失败 -> 扣金币复活 -> 第二次重新打 Boss"。
    // 如果输入显式提供 maxBossAttempts，则以输入为准。
    const int maxAttempts = source.value("maxBossAttempts", std::max(2, totalBossCount));
    const int coinConsumption = readCoinConsumption(source);

    // 初始已知血量：按顺序揭示规则，一开始只知道第一只 Boss 的血量
    std::vector<int> knownHPs{bossHPs[0]};
    Json attempts = Json::array();

    // 多次尝试循环：每次尝试从 Boss 0 重新规划，保留已揭示的所有血量信息
    for (int attemptIndex = 0; attemptIndex < maxAttempts; ++attemptIndex) {
        int bossIndex = 0;
        int usedTurns = 0;
        // 每轮尝试开始时，所有技能冷却清零（从头开始打 Boss 序列）
        std::vector<int> cooldown(skills.size(), 0);
        std::vector<int> sequence;
        // 记录本次尝试开始时的已知血量状态（用于调试输出）
        const std::vector<int> attemptKnownHPsAtStart = knownHPs;
        Json phases = Json::array();
        bool attemptFailed = false;
        std::string failureReason;

        // 顺序推进每个 Boss：当前只看到 knownHPs 中已揭示的血量
        while (bossIndex < totalBossCount) {
            // 揭示当前 Boss 的血量（如果尚未揭示）
            if (bossIndex >= static_cast<int>(knownHPs.size())) {
                knownHPs.push_back(bossHPs[bossIndex]);
            }

            Json candidateScores = Json::array();
            // 用轻量后缀容量法求解当前 Boss 在滚动视野下的最优方案
            BossPlanCandidate plan =
                solveCurrentBossByLightCapacity(bossIndex, usedTurns, cooldown, knownHPs, totalBossCount, minRounds,
                                                skills, candidateScores);
            // 无可行的轻量容量方案：记录失败并跳出当前尝试
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

            // 记录当前阶段的方案详情（用于调试和验证滚动视野决策）
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
                       {"selectionReason", "剩余回合最大后缀伤害容量"}};
            phases.push_back(std::move(phase));

            // 执行方案：拼接动作序列，更新已用回合和冷却状态
            sequence.insert(sequence.end(), plan.sequence.begin(), plan.sequence.end());
            usedTurns += plan.turns;
            cooldown = plan.cooldownAfter;
            // 检查是否超出总回合限制
            if (minRounds >= 0 && usedTurns > minRounds) {
                attemptFailed = true;
                failureReason = "boss battle exceeded minRounds";
                break;
            }
            ++bossIndex;
        }

        // 记录本次尝试的完整信息（包含所有阶段的决策）
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
            // 尝试失败但不退出循环：下次尝试时 knownHPs 已包含更多揭示的血量，
            // 滚动时域规划可以利用更多信息重新决策
            continue;
        }

        // 尝试成功：所有 Boss 均已击败，返回完整结果
        attempts.push_back(attempt);
        Json result{{"ok", true},
                    {"algorithm", "滚动时域Boss规划"},
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

    // 所有尝试均失败：返回带复活规则和完整尝试记录的失败结果
    return cacheAndReturn({{"ok", false},
                            {"error", "boss battle has no solution after revive-aware replanning"},
                           {"algorithm", "滚动时域Boss规划"},
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
