#include "BossStrategy.h"

#include <queue>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace ai_player {

/**
 * 功能：搜索击败 Boss 队列所需的最短技能序列。
 * 输入：
 *   - source：任务 JSON，必须包含 B 血量数组和 PlayerSkills 技能数组。
 * 输出：
 *   - 返回 JSON，成功时包含 ok、turns、sequence，失败时包含错误信息。
 * 关键逻辑：
 *   - 使用分支界限法搜索状态；状态包含当前 Boss 血量、技能冷却、当前 Boss 下标和技能序列。
 *   - 优先扩展“已用回合 + 剩余回合乐观下界”更小的分支，已有最优解不劣于下界时直接剪枝。
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
    std::vector<Skill> skills;
    for (int i = 0; i < static_cast<int>(source["PlayerSkills"].size()); ++i) {
        const auto &item = source["PlayerSkills"][i];
        if (!item.is_array() || item.size() != 2) return {{"ok", false}, {"error", "invalid PlayerSkills item"}};
        skills.push_back({i, item[0].get<int>(), item[1].get<int>()});
    }

    int maxDamage = 0;
    for (const Skill &skill : skills) {
        if (skill.damage > maxDamage) maxDamage = skill.damage;
    }

    // 功能：计算当前分支到完成战斗至少还需要多少回合。
    // 输入：hp 为各 Boss 当前剩余血量，bossIndex 为正在攻击的 Boss 下标。
    // 输出：返回乐观估计的剩余回合数，用作分支界限法剪枝下界。
    // 关键逻辑：假设之后每一回合都能打出单技能最大伤害，因此结果不会高估真实最少回合。
    const auto lowerBound = [&](const std::vector<int> &hp, int bossIndex) {
        int remainingHp = 0;
        for (int i = bossIndex; i < static_cast<int>(hp.size()); ++i) {
            remainingHp += hp[i];
        }
        // 用单回合最大伤害估算剩余最少回合数，这是乐观下界。
        // 如果用平均输出估算，爆发技能可能让下界偏大，从而错误剪掉真实最优分支。
        if (maxDamage <= 0) return 0;
        return (remainingHp + maxDamage - 1) / maxDamage;
    };

    struct State {
        std::vector<int> hp;
        std::vector<int> cooldown;
        int bossIndex = 0;
        std::vector<int> sequence;
        int bound = 0;
    };

    struct CompareState {
        /**
         * 功能：定义分支界限优先队列的状态排序规则。
         * 输入：
         *   - left：候选状态。
         *   - right：候选状态。
         * 输出：
         *   - 返回值：true 表示 left 的优先级低于 right。
         * 关键逻辑：
         *   - 优先扩展估价 bound 更小的状态；bound 相同时优先扩展已用回合更少的状态。
         */
        bool operator()(const State &left, const State &right) const
        {
            if (left.bound != right.bound) return left.bound > right.bound;
            return left.sequence.size() > right.sequence.size();
        }
    };

    std::priority_queue<State, std::vector<State>, CompareState> queue;
    queue.push({bossHPs, std::vector<int>(skills.size(), 0), 0, {}, lowerBound(bossHPs, 0)});
    std::unordered_map<std::string, int> bestSeenTurns;
    int bestTurn = -1;
    std::vector<int> bestSequence;
    while (!queue.empty()) {
        State state = queue.top();
        queue.pop();
        if (bestTurn >= 0 && state.bound >= bestTurn) continue;

        std::ostringstream key;
        key << state.bossIndex << '|';
        for (const int hp : state.hp) key << hp << ',';
        key << '|';
        for (const int cd : state.cooldown) key << cd << ',';

        const int turn = static_cast<int>(state.sequence.size());
        const auto seen = bestSeenTurns.find(key.str());
        if (seen != bestSeenTurns.end() && seen->second <= turn) continue;
        bestSeenTurns[key.str()] = turn;

        if (state.bossIndex >= static_cast<int>(state.hp.size())) {
            bestTurn = turn;
            bestSequence = std::move(state.sequence);
            continue;
        }

        bool hasSkill = false;
        for (int i = 0; i < static_cast<int>(skills.size()); ++i) {
            if (state.cooldown[i] > 0) continue;
            hasSkill = true;
            State next = state;
            next.hp[next.bossIndex] -= skills[i].damage;
            if (next.hp[next.bossIndex] <= 0) {
                next.hp[next.bossIndex] = 0;
                ++next.bossIndex;
            }
            for (int &cooldown : next.cooldown) {
                if (cooldown > 0) --cooldown;
            }
            next.cooldown[i] = skills[i].cooldown;
            next.sequence.push_back(skills[i].id);
            next.bound = static_cast<int>(next.sequence.size()) + lowerBound(next.hp, next.bossIndex);
            if (bestTurn < 0 || next.bound < bestTurn) queue.push(std::move(next));
        }
        if (!hasSkill) {
            for (int &cooldown : state.cooldown) {
                if (cooldown > 0) --cooldown;
            }
            state.sequence.push_back(-1);
            state.bound = static_cast<int>(state.sequence.size()) + lowerBound(state.hp, state.bossIndex);
            if (bestTurn < 0 || state.bound < bestTurn) queue.push(std::move(state));
        }
    }
    if (bestTurn >= 0) {
        return {{"ok", true}, {"turns", bestTurn}, {"sequence", bestSequence}};
    }
    return {{"ok", false}, {"error", "boss battle has no solution"}};
}

} // namespace ai_player
