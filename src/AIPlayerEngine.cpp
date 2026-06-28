#include "AIPlayerEngine.h"

#include <array>
#include <cstdlib>
#include <iomanip>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "GameTypes.h"
#include "RealtimeGreedyStrategy.h"
#include "ShortestPathStrategy.h"
#include "nlohmann/json.hpp"
#include "sha256.h"

using ai_player::Json;
using ai_player::MazeData;
using ai_player::Position;
using ai_player::Skill;
using ai_player::kDirs;
using ai_player::kInvalid;
using ai_player::planAdventurePath;
using ai_player::realtimeGreedyPath;
using ai_player::scoreDelta;

namespace {
std::string errorJson(const std::string &message)
{
    return Json{{"ok", false}, {"error", message}}.dump();
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

Json visibleCells(const MazeData &data, Position pos)
{
    Json visible = Json::array();
    const int rows = static_cast<int>(data.grid.size());
    const int cols = static_cast<int>(data.grid[0].size());
    for (int row = pos.first - 1; row <= pos.first + 1; ++row) {
        for (int col = pos.second - 1; col <= pos.second + 1; ++col) {
            if (row >= 0 && col >= 0 && row < rows && col < cols) {
                visible.push_back({{"row", row}, {"col", col}, {"tile", data.grid[row][col]}});
            }
        }
    }
    return visible;
}

Json observedCellsJson(const MazeData &data, const std::vector<std::vector<bool>> &observed)
{
    Json cells = Json::array();
    for (int row = 0; row < static_cast<int>(data.grid.size()); ++row) {
        for (int col = 0; col < static_cast<int>(data.grid[row].size()); ++col) {
            if (observed[row][col]) {
                cells.push_back({{"row", row}, {"col", col}, {"tile", data.grid[row][col]}});
            }
        }
    }
    return cells;
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

Json runBossJson(const Json &source)
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

    struct State {
        std::vector<int> hp;
        std::vector<int> cooldown;
        int bossIndex = 0;
        std::vector<int> sequence;
    };

    std::queue<State> queue;
    queue.push({bossHPs, std::vector<int>(skills.size(), 0), 0, {}});
    std::unordered_set<std::string> visited;
    while (!queue.empty()) {
        State state = queue.front();
        queue.pop();
        if (state.bossIndex >= static_cast<int>(state.hp.size())) {
            return {{"ok", true}, {"turns", state.sequence.size()}, {"sequence", state.sequence}};
        }

        std::ostringstream key;
        key << state.bossIndex << '|';
        for (const int hp : state.hp) key << hp << ',';
        key << '|';
        for (const int cd : state.cooldown) key << cd << ',';
        if (!visited.insert(key.str()).second) continue;

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
            queue.push(std::move(next));
        }
        if (!hasSkill) {
            for (int &cooldown : state.cooldown) {
                if (cooldown > 0) --cooldown;
            }
            state.sequence.push_back(-1);
            queue.push(std::move(state));
        }
    }
    return {{"ok", false}, {"error", "boss battle has no solution"}};
}

Json buildResult(const MazeData &data, const std::vector<Position> &path, const std::string &mode)
{
    Json result{{"ok", true}, {"mode", mode}, {"path", Json::array()}, {"frames", Json::array()}, {"events", Json::array()}};
    const Json lock = solveLockJson(data.source);
    const Json boss = runBossJson(data.source);
    std::vector collected(data.grid.size(), std::vector<bool>(data.grid[0].size(), false));
    std::vector observed(data.grid.size(), std::vector<bool>(data.grid[0].size(), false));
    bool lockEvent = false;
    bool bossEvent = false;
    int resource = 0;

    for (size_t step = 0; step < path.size(); ++step) {
        const auto [row, col] = path[step];
        for (int r = row - 1; r <= row + 1; ++r) {
            for (int c = col - 1; c <= col + 1; ++c) {
                if (r >= 0 && c >= 0 && r < static_cast<int>(data.grid.size()) &&
                    c < static_cast<int>(data.grid[0].size())) {
                    observed[r][c] = true;
                }
            }
        }
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
                                    {"resource", resource},
                                    {"observed", observedCellsJson(data, observed)},
                                    {"visible", visibleCells(data, {row, col})}});
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
        const MazeData data = parseMaze(inputJson);
        Json result = buildResult(data, realtimeGreedyPath(data), "realtime-greedy");
        result["greedy_formula"] = "ratio = resource_value / local_steps_in_current_3x3; G=50, T=-30; targets with ratio <= 0 are not actively selected";
        result["memory_policy"] = "memoryless: each decision only uses the current 3x3 visible area and current game-state collection effect";
        return result.dump();
    } catch (const std::exception &ex) {
        return errorJson(ex.what());
    }
}

std::string AIPlayerEngine::RunAdventure(const std::string &inputJson, const std::string &algorithm)
{
    try {
        if (algorithm != "smart" && algorithm != "dijkstra" && algorithm != "astar") {
            throw std::runtime_error("unsupported algorithm: " + algorithm);
        }
        const MazeData data = parseMaze(inputJson);
        return buildResult(data, planAdventurePath(data, algorithm), algorithm).dump();
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
        return runBossJson(Json::parse(inputJson)).dump();
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
