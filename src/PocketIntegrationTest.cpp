#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "AIPlayerEngine.h"
#include "nlohmann/json.hpp"

namespace {
using Json = nlohmann::json;

/**
 * 功能：从磁盘读取测试用 JSON 文件。
 * 输入：
 *   - path：迷宫 JSON 文件路径，必须存在且可读。
 * 输出：
 *   - 返回文件完整文本。
 * 关键逻辑：
 *   - 集成测试需要直接调用桌面端公开接口，因此这里只负责最小文件读取。
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
 * 功能：检查测试条件，不满足时输出当前帧并失败。
 * 输入：
 *   - condition：需要满足的条件。
 *   - message：失败原因。
 *   - frame：用于定位问题的调试帧。
 * 输出：
 *   - 无返回值。
 * 关键逻辑：
 *   - pocket 问题通常来自某一帧状态错误，失败时直接打印该帧 JSON，便于继续定位。
 */
void requireFrame(bool condition, const std::string &message, const Json &frame)
{
    if (!condition) {
        std::cerr << message << "\n" << frame.dump(2) << "\n";
        throw std::runtime_error(message);
    }
}

/**
 * 功能：判断 JSON 坐标是否等于指定行列。
 * 输入：
 *   - pos：形如 {"row":r,"col":c} 的 JSON 坐标。
 *   - row/col：期望真实坐标。
 * 输出：
 *   - 返回坐标是否匹配。
 * 关键逻辑：
 *   - 前端和后端调试都使用真实坐标展示，本测试也直接检查真实坐标。
 */
bool isPosition(const Json &pos, int row, int col)
{
    return pos.is_object() && pos.value("row", -999999) == row && pos.value("col", -999999) == col;
}

} // namespace

/**
 * 功能：验证默认 15x15 样例中的局部资源口袋会触发 pocket-first-target。
 * 输入：
 *   - argv[1]：默认 15x15 迷宫 JSON 文件路径。
 * 输出：
 *   - 成功时输出 pocket_integration_tests.ok=1。
 * 关键逻辑：
 *   - 在真实坐标 (1,3) 这一帧，左右和下方都是未收集金币；左侧金币 Iproxy 高时，
 *     pocket 规则应保留左侧高潜力金币，第一目标选择右侧金币 (1,4)。
 */
int main(int argc, char **argv)
{
    try {
        if (argc != 2) throw std::runtime_error("usage: pocket_integration_tests <maze_15_15.json>");

        AIPlayerEngine engine;
        const Json result = Json::parse(engine.RunRealtimeGreedy(readTextFile(argv[1])));
        if (!result.value("ok", false)) {
            throw std::runtime_error(result.value("error", "RunRealtimeGreedy failed"));
        }

        bool sawFrame = false;
        for (const auto &frame : result.at("frames")) {
            if (frame.value("row", -1) != 1) continue;
            if (frame.value("col", -1) != 3) continue;
            sawFrame = true;

            const Json &debug = frame.at("debug");
            requireFrame(debug.value("decision", "") == "pocket-first-target",
                         "default pocket frame should use pocket-first-target", frame);
            requireFrame(!debug.at("pocket").is_null(), "default pocket frame should include pocket debug", frame);
            requireFrame(isPosition(debug.at("selectedReal"), 1, 4),
                         "default pocket frame should select right coin (1,4)", frame);
            requireFrame(isPosition(debug.at("pocket").at("realChosenPocketTarget"), 1, 4),
                         "pocket chosen target should be right coin (1,4)", frame);
            break;
        }

        if (!sawFrame) throw std::runtime_error("default run never reached frame (1,3)");
        std::cout << "pocket_integration_tests.ok=1\n";
        return 0;
    } catch (const std::exception &ex) {
        std::cerr << ex.what() << "\n";
        return 1;
    }
}
