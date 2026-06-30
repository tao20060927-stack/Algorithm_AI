#include <fstream>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "GameTypes.h"
#include "RealtimeGreedyStrategy.h"
#include "Reward.h"
#include "nlohmann/json.hpp"

namespace {
using ai_player::GreedyRunResult;
using ai_player::Json;
using ai_player::MazeData;
using ai_player::Position;
using ai_player::RewardParameters;
using ai_player::kInvalid;
using ai_player::realtimeGreedyRun;
using ai_player::scoreDelta;

/**
 * 功能：从文件读取完整文本。
 * 输入：
 *   - path：需要读取的 JSON 文件路径，必须存在且可读。
 * 输出：
 *   - 返回文件全部内容字符串。
 * 关键逻辑：
 *   - 训练脚本会反复调用该评估器，因此这里保持最小 I/O 逻辑，只负责可靠读取单个迷宫。
 */
std::string readTextFile(const std::string &path)
{
    std::ifstream input(path);
    if (!input) throw std::runtime_error("failed to open input file: " + path);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

/**
 * 功能：解析训练评估用的迷宫 JSON。
 * 输入：
 *   - inputJson：包含 maze 字段的任务 JSON 字符串。
 * 输出：
 *   - 返回 MazeData，包含网格、起点、终点、金币、机关和 Boss 坐标。
 * 关键逻辑：
 *   - 与桌面端解析规则保持一致，只接受课程设计允许的格子符号，避免训练时把脏数据送入策略。
 */
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
            if (tile != "#" && tile != " " && tile != "S" && tile != "E" && tile != "G" && tile != "T" &&
                tile != "L" && tile != "B") {
                throw std::runtime_error("unknown maze cell: " + tile);
            }
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

/**
 * 功能：把 JSON 参数覆盖到 RewardParameters。
 * 输入：
 *   - paramsJson：可为空的参数 JSON 字符串或参数 JSON 文件路径，支持 omegaI、beta、lambdaMargin、kappaU、areaMax、rhoAreaValueMin、qEffLengthWeight。
 * 输出：
 *   - 返回覆盖后的 RewardParameters。
 * 关键逻辑：
 *   - Python 训练脚本传入临时 JSON 文件路径，避免 Windows 命令行吞掉 JSON 引号。
 *   - 未出现在 JSON 中的参数继续使用 RewardConfig.h 当前默认值，保证 SPSA 可以只调指定参数。
 */
RewardParameters parseParameters(const std::string &paramsJson)
{
    RewardParameters parameters;
    if (paramsJson.empty()) return parameters;

    std::string jsonText = paramsJson;
    std::ifstream paramsFile(paramsJson);
    if (paramsFile) {
        std::ostringstream buffer;
        buffer << paramsFile.rdbuf();
        jsonText = buffer.str();
    }

    const Json source = Json::parse(jsonText);
    if (source.contains("omegaI")) parameters.omegaI = source["omegaI"].get<double>();
    if (source.contains("beta")) parameters.beta = source["beta"].get<double>();
    if (source.contains("lambdaMargin")) parameters.lambdaMargin = source["lambdaMargin"].get<double>();
    if (source.contains("kappaU")) parameters.kappaU = source["kappaU"].get<double>();
    if (source.contains("areaMax")) {
        parameters.areaMax = std::max(1, static_cast<int>(std::lround(source["areaMax"].get<double>())));
    }
    if (source.contains("rhoAreaValueMin")) {
        parameters.rhoAreaValueMin = source["rhoAreaValueMin"].get<double>();
    }
    if (source.contains("qEffLengthWeight")) {
        parameters.qEffLengthWeight = source["qEffLengthWeight"].get<double>();
    }
    return parameters;
}

/**
 * 功能：根据路径重新结算资源和步数。
 * 输入：
 *   - data：迷宫数据。
 *   - path：C++ 贪心策略输出的真实坐标路径。
 * 输出：
 *   - 返回包含 resource、steps、scoreRatio、finished 的 JSON 指标。
 * 关键逻辑：
 *   - 金币和陷阱只在第一次踩到时结算，保持与桌面端结果面板一致。
 *   - SPSA 的目标函数主要读取 score_ratio，也就是最终资源 R / 路径长度 L。
 */
Json scorePath(const MazeData &data, const std::vector<Position> &path)
{
    int resource = 0;
    std::vector collected(data.grid.size(), std::vector<bool>(data.grid[0].size(), false));
    for (const auto &[row, col] : path) {
        if (row < 0 || col < 0 || row >= static_cast<int>(data.grid.size()) ||
            col >= static_cast<int>(data.grid[0].size())) {
            continue;
        }
        if (collected[row][col]) continue;
        resource += scoreDelta(data.grid[row][col]);
        collected[row][col] = true;
    }

    const int steps = path.empty() ? 0 : static_cast<int>(path.size()) - 1;
    const double ratio = steps == 0 ? 0.0 : static_cast<double>(resource) / steps;
    return {{"resource", resource},
            {"steps", steps},
            {"score_ratio", ratio},
            {"finished", !path.empty() && path.back() == data.exit}};
}

} // namespace

/**
 * 功能：训练用命令行评估入口。
 * 输入：
 *   - argv[1]：迷宫 JSON 文件路径。
 *   - argv[2]：可选，reward 参数 JSON 字符串。
 * 输出：
 *   - 标准输出写出单次评估 JSON，失败时返回非 0 状态码并写出错误 JSON。
 * 关键逻辑：
 *   - Python SPSA 每次用不同参数调用该程序，C++ 侧只运行现有实时贪心策略，不做任何训练逻辑。
 *   - 输出只保留指标，不输出完整路径，避免训练时大量 subprocess I/O 拖慢速度。
 */
int main(int argc, char **argv)
{
    try {
        if (argc < 2 || argc > 3) {
            throw std::runtime_error("usage: ai_player_eval <input.json> [params-json]");
        }

        const MazeData data = parseMaze(readTextFile(argv[1]));
        const RewardParameters parameters = parseParameters(argc == 3 ? argv[2] : "");
        const GreedyRunResult run = realtimeGreedyRun(data, "greedy", parameters);

        Json result = scorePath(data, run.path);
        result["ok"] = true;
        std::cout << result.dump() << "\n";
        return 0;
    } catch (const std::exception &ex) {
        std::cout << Json{{"ok", false}, {"error", ex.what()}}.dump() << "\n";
        return 1;
    }
}
