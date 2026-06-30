#include "AIPlayerEngine.h"

#include <array>
#include <cstdlib>
#include <iomanip>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "BossStrategy.h"
#include "GameTypes.h"
#include "RealtimeGreedyStrategy.h"
#include "ShortestPathStrategy.h"
#include "nlohmann/json.hpp"
#include "sha256.h"

using ai_player::Json;
using ai_player::MazeData;
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
            static const std::set<std::string> allowed{"#", " ", "S", "E", "G", "T", "L", "B"};
            if (!allowed.count(tile)) throw std::runtime_error("unknown maze cell: " + tile);
            data.grid[row][col] = tile;
            if (tile == "S") data.start = {row, col};
            if (tile == "E") data.exit = {row, col};
            if (tile == "L") data.locks.push_back({row, col});
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
            {"marginPenalty", candidate.marginPenalty},
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
            {"rejected", rejected}};
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

std::string sha256WithSalt(const std::string &input)
{
    BYTE salt[] = {
        0xB2, 0x53, 0x22, 0x65, 0x7D, 0xDF, 0xB0, 0xFE,
        0x9C, 0xDE, 0xDE, 0xFE, 0xF3, 0x1D, 0xDC, 0x3E
    };
    BYTE hash[SHA256_BLOCK_SIZE];
    SHA256_CTX ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, salt, sizeof(salt));
    sha256_update(&ctx, reinterpret_cast<const BYTE *>(input.c_str()), input.size());
    sha256_final(&ctx, hash);

    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (const auto byte : hash) out << std::setw(2) << static_cast<int>(byte);
    return out.str();
}

bool isPrimeDigit(int digit)
{
    return digit == 2 || digit == 3 || digit == 5 || digit == 7;
}

bool passwordMatchesClues(const std::array<int, 3> &password, const std::vector<std::vector<int>> &clues)
{
    for (const auto &clue : clues) {
        if (clue == std::vector<int>{-1, -1}) {
            std::set<int> digits(password.begin(), password.end());
            if (digits.size() != 3) return false;
            for (const int digit : password) {
                if (!isPrimeDigit(digit)) return false;
            }
        } else if (clue.size() == 2) {
            const int pos = clue[0] - 1;
            if (pos < 0 || pos >= 3) return false;
            if (clue[1] == 0 && password[pos] % 2 != 0) return false;
            if (clue[1] == 1 && password[pos] % 2 != 1) return false;
        } else if (clue.size() == 3) {
            for (int i = 0; i < 3; ++i) {
                if (clue[i] != -1 && clue[i] != password[i]) return false;
            }
        }
    }
    return true;
}

Json solveLockJson(const Json &source)
{
    if (!source.contains("L") || !source["L"].is_string()) {
        return {{"ok", false}, {"error", "missing L hash"}};
    }
    if (!source.contains("C") || !source["C"].is_array()) {
        return {{"ok", false}, {"error", "missing C clues"}};
    }
    std::vector<std::vector<int>> clues = source["C"].get<std::vector<std::vector<int>>>();
    int tries = 0;
    for (int a = 0; a <= 9; ++a) {
        for (int b = 0; b <= 9; ++b) {
            for (int c = 0; c <= 9; ++c) {
                std::array<int, 3> password{a, b, c};
                if (!passwordMatchesClues(password, clues)) continue;
                const std::string text = std::to_string(a) + std::to_string(b) + std::to_string(c);
                ++tries;
                if (sha256WithSalt(text) == source["L"].get<std::string>()) {
                    return {{"ok", true}, {"password", text}, {"tries", tries}};
                }
            }
        }
    }
    return {{"ok", false}, {"password", ""}, {"tries", tries}};
}

Json buildResult(const MazeData &data, const std::vector<Position> &path, const std::string &mode)
{
    Json result{{"ok", true}, {"mode", mode}, {"path", Json::array()}, {"frames", Json::array()}, {"events", Json::array()}};
    const Json lock = solveLockJson(data.source);
    const Json boss = runBossBattleJson(data.source);
    std::vector collected(data.grid.size(), std::vector<bool>(data.grid[0].size(), false));
    bool lockEvent = false;
    bool bossEvent = false;
    int resource = 0;

    for (size_t step = 0; step < path.size(); ++step) {
        const auto [row, col] = path[step];
        const std::string tile = data.grid[row][col];
        int delta = 0;
        if (!collected[row][col]) {
            delta = scoreDelta(tile);
            resource += delta;
            collected[row][col] = true;
        }
        result["path"].push_back({{"row", row}, {"col", col}});
        result["frames"].push_back({{"step", step},
                                    {"row", row},
                                    {"col", col},
                                    {"tile", tile},
                                    {"delta", delta},
                                    {"resource", resource}});
        if (tile == "L" && !lockEvent) {
            result["events"].push_back({{"step", step}, {"type", "lock"}, {"result", lock}});
            lockEvent = true;
        }
        if (isBossTriggerCell(data, {row, col}) && !bossEvent) {
            result["events"].push_back({{"step", step}, {"type", "boss"}, {"result", boss}});
            bossEvent = true;
        }
    }

    const int steps = path.empty() ? 0 : static_cast<int>(path.size()) - 1;
    result["resource"] = resource;
    result["steps"] = steps;
    result["score_ratio"] = steps == 0 ? 0.0 : static_cast<double>(resource) / steps;
    result["average_resource_per_step"] = steps == 0 ? 0.0 : static_cast<double>(resource) / steps;
    result["finished"] = !path.empty() && path.back() == data.exit;
    result["lock"] = lock;
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
        result["greedy_formula"] = "additive reward = DeltaR + omegaI*alpha*I_proxy + beta*tailUB - qEffLengthWeight*qEff*pathLen - marginPenalty; tailUB excludes coins already covered by path_t";
        result["memory_policy"] = "online memory: each decision uses the local_known_map updated by 3x3 observations";
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

std::string AIPlayerEngine::SolveLock(const std::string &inputJson)
{
    try {
        return solveLockJson(Json::parse(inputJson)).dump();
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
        for (const auto &pos : data.locks) requireReachable(pos, "lock");
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
