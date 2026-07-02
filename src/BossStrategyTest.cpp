#include <iostream>
#include <stdexcept>
#include <string>

#include "AIPlayerEngine.h"
#include "BossStrategy.h"

namespace {
using ai_player::Json;
using ai_player::runBossBattleJson;

/**
 * 功能：检查测试条件，不满足时抛出异常。
 * 输入：
 *   - condition：测试条件。
 *   - message：失败说明。
 * 输出：
 *   - 无返回值。
 * 关键逻辑：
 *   - Boss 语义测试需要明确区分全知规划和顺序揭示规划，因此断言信息必须直接指向规则。
 */
void require(bool condition, const std::string &message)
{
    if (!condition) throw std::runtime_error(message);
}

} // namespace

/**
 * 功能：验证 Boss 战按顺序揭示血量，并输出复活金币规则字段。
 * 输入：
 *   - 无。
 * 输出：
 *   - 成功时输出 boss_strategy_tests.ok=1。
 * 关键逻辑：
 *   - 初始只知道第一个 Boss 血量；每个 phase 只能对应当前揭示 Boss。
 *   - CoinConsumption 作为复活成本字段保留在结果中。
 */
int main()
{
    const Json input{{"B", Json::array({11, 13, 9, 15})},
                     {"PlayerSkills", Json::array({Json::array({8, 4}), Json::array({2, 0}), Json::array({4, 2}),
                                                   Json::array({6, 3})})},
                     {"minRounds", 11},
                     {"CoinConsumption", 9}};

    const Json result = runBossBattleJson(input);
    require(result.value("ok", false), "boss strategy should solve the sample");
    require(result.value("knowledgePolicy", "") == "sequential_reveal_current_boss_only",
            "boss strategy should use sequential reveal policy");
    require(result.at("initialKnownBossHPs")[0] == 11, "first boss HP should be known initially");
    require(result.at("initialKnownBossHPs")[1].is_null(), "second boss HP should be unknown initially");
    require(result.at("phases").size() == 4, "sample should contain four sequential boss phases");
    require(result.value("CoinConsumption", 0) == 9, "CoinConsumption should be reported as revive coin cost");
    require(result.at("reviveRule").value("restartPosition", "") == "S", "revive should restart from S");
    require(result.value("turns", 0) <= 11, "rolling light-capacity strategy should meet minRounds=11");
    require(result.value("withinMinRounds", false), "sample should be defeated within minRounds");
    require(result.value("algorithm", "") == "rolling_horizon_damage_capacity_boss_planner",
            "boss strategy should report rolling light-capacity planner");
    require(result.value("knownHpPersistenceImplemented", false),
            "boss strategy should implement known HP persistence across attempts");

    const Json regressionInput{{"B", Json::array({11, 7, 18})},
                               {"PlayerSkills", Json::array({Json::array({10, 1}), Json::array({11, 4}),
                                                             Json::array({11, 5}), Json::array({3, 0}),
                                                             Json::array({4, 4})})},
                               {"minRounds", 4}};
    const Json regression = runBossBattleJson(regressionInput);
    require(regression.value("ok", false), "rolling light-capacity planner should solve [11,7,18] regression");
    require(regression.value("turns", 0) <= 4, "regression should be solved within four turns");
    require(regression.value("withinMinRounds", false), "regression should satisfy minRounds=4");
    const auto firstRegressionPhase = regression.at("phases")[0].at("sequence").get<std::vector<int>>();
    require(firstRegressionPhase != std::vector<int>{0, 3},
            "first phase should not spend two turns only to refresh cooldowns");
    require(firstRegressionPhase.size() == 1,
            "first phase should prefer a one-turn kill when suffix capacity is better");
    require(regression.at("finalKnownBossHPs") == Json::array({11, 7, 18}),
            "regression should reveal and persist all boss HPs");
    require(regression.at("phases")[0].at("candidateScores")[0].contains("lightScore"),
            "candidate scores should expose lightScore");

    const Json hiddenFutureA = regressionInput;
    const Json hiddenFutureB{{"B", Json::array({11, 99, 99})},
                             {"PlayerSkills", regressionInput.at("PlayerSkills")},
                             {"minRounds", 4}};
    const Json hiddenA = runBossBattleJson(hiddenFutureA);
    const Json hiddenB = runBossBattleJson(hiddenFutureB);
    require(hiddenA.at("attempts")[0].at("phases")[0].at("candidateScores") ==
                hiddenB.at("attempts")[0].at("phases")[0].at("candidateScores"),
            "first boss candidate scores must not depend on unrevealed future HPs");
    require(hiddenA.at("attempts")[0].at("phases")[0].at("sequence") ==
                hiddenB.at("attempts")[0].at("phases")[0].at("sequence"),
            "first boss selected sequence must not depend on unrevealed future HPs");

    const Json highHpInput{{"B", Json::array({135, 156, 107, 102})},
                           {"PlayerSkills",
                            Json::array({Json::array({5, 0}), Json::array({22, 4}), Json::array({40, 8})})},
                           {"minRounds", 45}};
    const Json highHpResult = runBossBattleJson(highHpInput);
    require(highHpResult.value("ok", false), "high-HP case should be solved by light-capacity planner");
    require(highHpResult.value("algorithm", "") == "rolling_horizon_damage_capacity_boss_planner",
            "high-HP case should use rolling light-capacity planner");
    require(highHpResult.at("attempts")[0].at("phases")[0].at("candidateScores")[0].contains("lightScore"),
            "high-HP candidate scores should include lightScore");

    const Json reviveInput{{"maze",
                            Json::array({Json::array({"#", "#", "#", "#", "#"}),
                                         Json::array({"#", "S", "G", " ", "#"}),
                                         Json::array({"#", " ", "B", "E", "#"}),
                                         Json::array({"#", " ", " ", " ", "#"}),
                                         Json::array({"#", "#", "#", "#", "#"})})},
                           {"B", Json::array({100})},
                           {"PlayerSkills", Json::array({Json::array({1, 0})})},
                           {"minRounds", 1},
                           {"CoinConsumption", 9}};
    AIPlayerEngine engine;
    const Json reviveResult = Json::parse(engine.RunRealtimeGreedy(reviveInput.dump()));
    bool sawRevive = false;
    for (const auto &event : reviveResult.at("events")) {
        if (event.value("type", "") == "boss_revive") sawRevive = true;
    }
    require(sawRevive, "failed boss battle with enough resource should emit boss_revive event");

    std::cout << "boss_strategy_tests.ok=1\n";
    return 0;
}
