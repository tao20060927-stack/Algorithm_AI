#include "AIPlayerEngine.h"

#include <cstdlib>
#include <queue>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "BossStrategy.h"
#include "GameTypes.h"
#include "RealtimeGreedyStrategy.h"
#include "ResourcePickupStrategy.h"
#include "ShortestPathStrategy.h"
#include "nlohmann/json.hpp"

using ai_player::Json;
using ai_player::MazeData;
using ai_player::ClosedSingletonGateDebug;
using ai_player::PocketCandidateDebug;
using ai_player::PocketDebug;
using ai_player::Position;
using ai_player::kDirs;
using ai_player::kInvalid;
using ai_player::planAdventurePath;
using ai_player::GreedyCandidateDebug;
using ai_player::GreedyRejectedDebug;
using ai_player::GreedyRunResult;
using ai_player::GreedyStepDebug;
using ai_player::realtimeGreedyRun;
using ai_player::runBossBattleJson;
using ai_player::scoreDelta;
using ai_player::solveResourcePickupJson;

namespace {
std::string errorJson(const std::string &message)
{
    return Json{{"ok", false}, {"error", message}}.dump();
}

/**
 * 功能：判断算法名称是否属于 reward 目标选择后的两点路由算法。
 * 输入：
 *   - algorithm：前端传入的算法名称。
 * 输出：
 *   - 返回该算法是否只作为实时探索中的路由器使用。
 * 关键逻辑：
 *   - 分治、分支限界、Dijkstra 和 A* 不再承担完整探险目标选择，只在 reward 选定目标后负责路径搜索。
 */
bool isRewardRouterAlgorithm(const std::string &algorithm)
{
    return algorithm == "dijkstra" || algorithm == "astar" || algorithm == "branch_bound" ||
           algorithm == "divide_conquer";
}

/**
 * 功能：生成运行结果缓存键。
 * 输入：
 *   - algorithm：算法名称，不同算法必须保留不同缓存结果。
 *   - inputJson：前端传入的任务 JSON 字符串。
 * 输出：
 *   - 返回算法名和规范化 JSON 拼接后的缓存键。
 * 关键逻辑：
 *   - 先解析再 dump，避免只因为 JSON 空格、换行不同就重新运行后端算法。
 */
std::string makeRunCacheKey(const std::string &algorithm, const std::string &inputJson)
{
    return algorithm + "\n" + Json::parse(inputJson).dump();
}

MazeData parseMaze(const std::string &inputJson)
{
    MazeData data;
    data.source = Json::parse(inputJson);
    if (!data.source.contains("maze") || !data.source["maze"].is_array()) {
        throw std::runtime_error("JSON must contain maze array");
    }

    const auto &maze = data.source["maze"];
    if (maze.empty() || !maze[0].is_array() || maze[0].empty()) {
        throw std::runtime_error("maze must be a non-empty 2D array");
    }

    const int rows = static_cast<int>(maze.size());
    const int cols = static_cast<int>(maze[0].size());
    data.grid.assign(rows, std::vector<std::string>(cols));
    for (int row = 0; row < rows; ++row) {
        if (!maze[row].is_array() || static_cast<int>(maze[row].size()) != cols) {
            throw std::runtime_error("maze rows must have the same width");
        }
        for (int col = 0; col < cols; ++col) {
            const std::string tile = maze[row][col].get<std::string>();
            static const std::set<std::string> allowed{"#", " ", "S", "E", "G", "T", "B"};
            if (!allowed.count(tile)) throw std::runtime_error("unknown maze cell: " + tile);
            data.grid[row][col] = tile;
            if (tile == "S") data.start = {row, col};
            if (tile == "E") data.exit = {row, col};
            if (tile == "B") data.bosses.push_back({row, col});
            if (tile == "G") data.golds.push_back({row, col});
        }
    }
    return data;
}

Json positionJson(Position pos)
{
    if (pos == kInvalid) return nullptr;
    return {{"row", pos.first}, {"col", pos.second}};
}

Json greedyCandidateDebugJson(const GreedyCandidateDebug &candidate)
{
    return {{"localTarget", positionJson(candidate.localTarget)},
            {"realTarget", positionJson(candidate.realTarget)},
            {"tile", candidate.tile},
            {"score", candidate.score},
            {"deltaR", candidate.deltaR},
            {"Iproxy", candidate.informationProxy},
            {"tailGain", candidate.tailGain},
            {"qEff", candidate.qEff},
            {"pathLen", candidate.pathLength},
            {"projectedResource", candidate.projectedResource},
            {"unknownComponents", candidate.unknownComponents},
            {"unknownComponentSum", candidate.unknownComponentSum},
            {"selected", candidate.selected}};
}

Json greedyRejectedDebugJson(const GreedyRejectedDebug &candidate)
{
    return {{"localTarget", positionJson(candidate.localTarget)},
            {"realTarget", positionJson(candidate.realTarget)},
            {"tile", candidate.tile},
            {"reason", candidate.reason},
            {"pathLen", candidate.pathLength}};
}

Json closedSingletonGateDebugJson(const ClosedSingletonGateDebug &gate)
{
    Json row = {{"checked", gate.checked},
                {"triggered", gate.triggered},
                {"candidateA", positionJson(gate.candidateA)},
                {"candidateAType", gate.candidateAType},
                {"componentSizeA", gate.componentSizeA},
                {"isClosedSingletonA", gate.isClosedSingletonA},
                {"rewardA", gate.rewardA},
                {"bestNonClosedB", positionJson(gate.bestNonClosedB)},
                {"rewardB", gate.rewardB},
                {"simulatedAfterA", gate.simulatedAfterA},
                {"bestAfterATarget", positionJson(gate.bestAfterATarget)},
                {"bestAfterAReward", gate.bestAfterAReward},
                {"gamma", gate.gamma},
                {"margin", gate.margin},
                {"combinedA", gate.combinedA},
                {"allowed", gate.allowed},
                {"reason", gate.reason}};
    if (!gate.disabledReason.empty()) row["disabledReason"] = gate.disabledReason;
    return row;
}

Json pocketCandidateDebugJson(const PocketCandidateDebug &candidate)
{
    return {{"target", positionJson(candidate.target)},
            {"realTarget", positionJson(candidate.realTarget)},
            {"pathLen", candidate.pathLength},
            {"deltaR", candidate.deltaR},
            {"baseScore", candidate.baseScore},
            {"ownIproxy", candidate.ownIproxy},
            {"bestRemainingTarget", positionJson(candidate.bestRemainingTarget)},
            {"realBestRemainingTarget", positionJson(candidate.realBestRemainingTarget)},
            {"bestRemainingIproxy", candidate.bestRemainingIproxy},
            {"remainI", candidate.remainI},
            {"scoreFirst", candidate.scoreFirst},
            {"selected", candidate.selected}};
}

Json pocketDebugJson(const PocketDebug &pocket)
{
    if (!pocket.enabled) return nullptr;
    Json resources = Json::array();
    for (size_t i = 0; i < pocket.pocketResources.size(); ++i) {
        resources.push_back({{"local", positionJson(pocket.pocketResources[i])},
                             {"real", i < pocket.realPocketResources.size() ? positionJson(pocket.realPocketResources[i])
                                                                             : Json(nullptr)}});
    }
    Json candidates = Json::array();
    for (const auto &candidate : pocket.candidates) {
        candidates.push_back(pocketCandidateDebugJson(candidate));
    }
    return {{"pocketHub", positionJson(pocket.pocketHub)},
            {"realPocketHub", positionJson(pocket.realPocketHub)},
            {"pocketResources", resources},
            {"candidates", candidates},
            {"chosenPocketTarget", positionJson(pocket.chosenPocketTarget)},
            {"realChosenPocketTarget", positionJson(pocket.realChosenPocketTarget)},
            {"reason", pocket.reason}};
}

Json greedyStepDebugJson(const GreedyStepDebug &step)
{
    Json candidates = Json::array();
    for (const auto &candidate : step.candidates) {
        candidates.push_back(greedyCandidateDebugJson(candidate));
    }
    Json rejected = Json::array();
    for (const auto &candidate : step.rejected) {
        rejected.push_back(greedyRejectedDebugJson(candidate));
    }
    return {{"step", step.step},
            {"localCurrent", positionJson(step.localCurrent)},
            {"realCurrent", positionJson(step.realCurrent)},
            {"alpha", step.alpha},
            {"observedRatio", step.observedRatio},
            {"qEff", step.qEff},
            {"decision", step.decision},
            {"selectedLocal", positionJson(step.selectedLocal)},
            {"selectedReal", positionJson(step.selectedReal)},
            {"candidates", candidates},
            {"rejected", rejected},
            {"closedSingletonGate", closedSingletonGateDebugJson(step.closedSingletonGate)},
            {"pocket", pocketDebugJson(step.pocket)}};
}

void attachGreedyDebug(Json &result, const GreedyRunResult &run)
{
    const size_t count = std::min(run.debugSteps.size(), result["frames"].size());
    for (size_t i = 0; i < count; ++i) {
        result["frames"][i]["debug"] = greedyStepDebugJson(run.debugSteps[i]);
    }
}

bool isBossTriggerCell(const MazeData &data, Position pos)
{
    for (const auto &boss : data.bosses) {
        const int distance = std::abs(pos.first - boss.first) + std::abs(pos.second - boss.second);
        if (distance == 1) return true;
    }
    return false;
}

/**
 * 功能：判断 Boss 战是否能在限定回合内成功。
 * 输入：
 *   - boss：runBossBattleJson 返回的 Boss 战结果。
 * 输出：
 *   - 返回 Boss 战是否视为成功。
 * 关键逻辑：
 *   - 如果任务没有提供 minRounds，则只要 Boss 求解成功就视为可击败；提供 minRounds 时必须 withinMinRounds=true。
 */
bool bossBattleCanWinWithinLimit(const Json &boss)
{
    if (!boss.value("ok", false)) return false;
    if (boss.contains("withinMinRounds")) return boss["withinMinRounds"].get<bool>();
    return true;
}

Json buildResult(const MazeData &data, const std::vector<Position> &path, const std::string &mode)
{
    Json result{{"ok", true}, {"mode", mode}, {"path", Json::array()}, {"frames", Json::array()}, {"events", Json::array()}};
    const Json boss = runBossBattleJson(data.source);
    const Json bossAttempts = boss.contains("attempts") && boss["attempts"].is_array() ? boss["attempts"] : Json::array();
    std::vector collected(data.grid.size(), std::vector<bool>(data.grid[0].size(), false));
    const int reviveCost = boss.value("CoinConsumption", 0);
    int bossAttemptIndex = 0;
    int reviveCount = 0;
    bool bossCleared = false;
    bool gameOver = false;
    int resource = 0;
    int steps = 0;

    for (size_t pathIndex = 0; pathIndex < path.size();) {
        const auto [row, col] = path[pathIndex];
        const std::string tile = data.grid[row][col];
        int delta = 0;
        int paidReviveCost = 0;
        std::string frameEvent;
        bool reviveToStart = false;
        if (pathIndex > 0) ++steps;
        if (!collected[row][col]) {
            delta = scoreDelta(tile);
            resource += delta;
            collected[row][col] = true;
        }
        if (isBossTriggerCell(data, {row, col}) && !bossCleared) {
            // BossStrategy 已经按"失败后保留已揭示血量"生成 attempts。
            // 这里按顺序消费当前 attempt，不能用最终 ok 结果直接覆盖第一次战斗。
            const bool hasAttempt = bossAttemptIndex < static_cast<int>(bossAttempts.size());
            const Json attempt = hasAttempt ? bossAttempts[bossAttemptIndex] : Json::object();
            const bool attemptFailed = hasAttempt ? attempt.value("failed", false) : !bossBattleCanWinWithinLimit(boss);
            const bool attemptCanWin = !attemptFailed && bossBattleCanWinWithinLimit(boss);
            if (attemptCanWin) {
                frameEvent = "boss";
                result["events"].push_back({{"step", result["frames"].size()},
                                            {"type", "boss"},
                                            {"attemptIndex", bossAttemptIndex},
                                            {"attempt", attempt},
                                            {"result", boss}});
                bossCleared = true;
            } else if (resource >= reviveCost * 50 && bossAttemptIndex + 1 < static_cast<int>(bossAttempts.size())) {
                paidReviveCost = reviveCost * 50;
                resource -= reviveCost * 50;
                frameEvent = "boss_revive";
                result["events"].push_back({{"step", result["frames"].size()},
                                            {"type", "boss_revive"},
                                            {"attemptIndex", bossAttemptIndex},
                                            {"attempt", attempt},
                                            {"reviveCost", reviveCost},
                                            {"paidResource", paidReviveCost},
                                            {"result", boss}});
                ++bossAttemptIndex;
                ++reviveCount;
                // 复活只改变迷宫流程位置：资源、已收集金币、已触发陷阱和已揭示 Boss 血量都保留。
                reviveToStart = true;
            } else {
                frameEvent = "boss_game_over";
                result["events"].push_back({{"step", result["frames"].size()},
                                            {"type", "boss_game_over"},
                                            {"attemptIndex", bossAttemptIndex},
                                            {"attempt", attempt},
                                            {"reviveCost", reviveCost},
                                            {"result", boss}});
                gameOver = true;
            }
        }

        Json frame{{"step", result["frames"].size()},
                   {"row", row},
                   {"col", col},
                   {"tile", tile},
                   {"delta", delta},
                   {"resource", resource}};
        if (paidReviveCost > 0) frame["reviveCost"] = paidReviveCost;
        if (!frameEvent.empty()) frame["event"] = frameEvent;
        result["path"].push_back({{"row", row}, {"col", col}});
        result["frames"].push_back(std::move(frame));
        if (gameOver) break;
        if (reviveToStart) {
            // 复活回 S 生成独立帧，便于前端展示；它不是玩家移动，所以不增加 steps。
            Json reviveFrame{{"step", result["frames"].size()},
                             {"row", data.start.first},
                             {"col", data.start.second},
                             {"tile", "S"},
                             {"delta", 0},
                             {"resource", resource},
                             {"event", "revive_start"},
                             {"reviveAttemptIndex", bossAttemptIndex}};
            result["path"].push_back({{"row", data.start.first}, {"col", data.start.second}});
            result["frames"].push_back(std::move(reviveFrame));
            pathIndex = path.size() > 1 ? 1 : path.size();
            continue;
        }
        ++pathIndex;
    }

    result["resource"] = resource;
    result["steps"] = steps;
    result["score_ratio"] = steps == 0 ? 0.0 : static_cast<double>(resource) / steps;
    result["average_resource_per_step"] = steps == 0 ? 0.0 : static_cast<double>(resource) / steps;
    result["finished"] = !gameOver && !result["path"].empty() &&
                         Position{result["path"].back()["row"].get<int>(), result["path"].back()["col"].get<int>()} ==
                             data.exit;
    result["game_over"] = gameOver;
    result["revive_count"] = reviveCount;
    result["boss_attempts_consumed"] = bossCleared ? bossAttemptIndex + 1 : bossAttemptIndex;
    result["boss"] = boss;
    return result;
}
} // namespace

std::string AIPlayerEngine::RunRealtimeGreedy(const std::string &inputJson)
{
    try {
        const std::string cacheKey = makeRunCacheKey("greedy", inputJson);
        if (const auto it = resultCache_.find(cacheKey); it != resultCache_.end()) return it->second;

        const MazeData data = parseMaze(inputJson);
        const GreedyRunResult run = realtimeGreedyRun(data);
        Json result = buildResult(data, run.path, "realtime-greedy");
        attachGreedyDebug(result, run);
        result["greedy_formula"] = "additive reward = DeltaR + omegaI*alpha*I_proxy + beta*tailUB - qEffLengthWeight*qEff*pathLen; tailUB excludes coins already covered by path_t";
        result["memory_policy"] = "online memory: each decision uses the local_known_map updated by 3x3 observations";
        return resultCache_[cacheKey] = result.dump();
    } catch (const std::exception &ex) {
        return errorJson(ex.what());
    }
}

std::string AIPlayerEngine::RunResourcePickup(const std::string &inputJson)
{
    try {
        const std::string cacheKey = makeRunCacheKey("resource_pickup", inputJson);
        if (const auto it = resultCache_.find(cacheKey); it != resultCache_.end()) return it->second;

        const Json result = solveResourcePickupJson(Json::parse(inputJson));
        return resultCache_[cacheKey] = result.dump();
    } catch (const std::exception &ex) {
        return errorJson(ex.what());
    }
}

std::string AIPlayerEngine::RunAdventure(const std::string &inputJson, const std::string &algorithm)
{
    try {
        if (algorithm != "smart" && algorithm != "dijkstra" && algorithm != "astar" &&
            algorithm != "branch_bound" && algorithm != "divide_conquer") {
                throw std::runtime_error("unsupported algorithm: " + algorithm);
        }
        const std::string cacheKey = makeRunCacheKey(algorithm, inputJson);
        if (const auto it = resultCache_.find(cacheKey); it != resultCache_.end()) return it->second;

        const MazeData data = parseMaze(inputJson);
        if (isRewardRouterAlgorithm(algorithm)) {
            const GreedyRunResult run = realtimeGreedyRun(data, algorithm);
            Json result = buildResult(data, run.path, algorithm);
            attachGreedyDebug(result, run);
            result["router_mode"] = "reward selects target; selected algorithm only routes on local_known_map";
            return resultCache_[cacheKey] = result.dump();
        }
        return resultCache_[cacheKey] = buildResult(data, planAdventurePath(data, algorithm), algorithm).dump();
    } catch (const std::exception &ex) {
        return errorJson(ex.what());
    }
}

std::string AIPlayerEngine::RunBoss(const std::string &inputJson)
{
    try {
        return runBossBattleJson(Json::parse(inputJson)).dump();
    } catch (const std::exception &ex) {
        return errorJson(ex.what());
    }
}

std::string AIPlayerEngine::ValidateMaze(const std::string &inputJson)
{
    try {
        const MazeData data = parseMaze(inputJson);
        Json errors = Json::array();
        if (data.start == kInvalid) errors.push_back("missing start S");
        if (data.exit == kInvalid) errors.push_back("missing exit E");
        if (data.bosses.empty()) errors.push_back("missing boss B");
        if (data.start == kInvalid || data.exit == kInvalid) {
            return Json{{"ok", errors.empty()}, {"errors", errors}}.dump();
        }

        auto runReachability = [&](bool bossCleared) {
            std::vector visited(data.grid.size(), std::vector<bool>(data.grid[0].size(), false));
            std::queue<Position> queue;
            queue.push(data.start);
            visited[data.start.first][data.start.second] = true;
            while (!queue.empty()) {
                const auto [row, col] = queue.front();
                queue.pop();
                for (const auto [dr, dc] : kDirs) {
                    const int nr = row + dr;
                    const int nc = col + dc;
                    if (nr < 0 || nc < 0 || nr >= static_cast<int>(data.grid.size()) ||
                        nc >= static_cast<int>(data.grid[0].size())) {
                        continue;
                    }
                    if (data.grid[nr][nc] == "#" || (!bossCleared && data.grid[nr][nc] == "B")) continue;
                    if (!visited[nr][nc]) {
                        visited[nr][nc] = true;
                        queue.push({nr, nc});
                    }
                }
            }
            return visited;
        };

        const auto beforeBoss = runReachability(false);
        const auto afterBoss = runReachability(true);

        auto requireReachable = [&](Position pos, const std::string &name) {
            if (!afterBoss[pos.first][pos.second]) errors.push_back(name + " is unreachable");
        };
        requireReachable(data.exit, "exit");
        for (const auto &boss : data.bosses) {
            bool hasReachableTrigger = false;
            for (const auto [dr, dc] : kDirs) {
                const int row = boss.first + dr;
                const int col = boss.second + dc;
                if (row >= 0 && col >= 0 && row < static_cast<int>(beforeBoss.size()) &&
                    col < static_cast<int>(beforeBoss[0].size()) && beforeBoss[row][col]) {
                    hasReachableTrigger = true;
                }
            }
            if (!hasReachableTrigger) errors.push_back("boss trigger is unreachable");
        }
        return Json{{"ok", errors.empty()}, {"errors", errors}}.dump();
    } catch (const std::exception &ex) {
        return errorJson(ex.what());
    }
}
