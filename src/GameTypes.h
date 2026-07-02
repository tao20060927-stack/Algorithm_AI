#ifndef GAME_TYPES_H
#define GAME_TYPES_H

#include <array>
#include <string>
#include <utility>
#include <vector>

#include "nlohmann/json.hpp"

namespace ai_player {
using Json = nlohmann::json;                     // 项目统一使用的 JSON 库别名
using Position = std::pair<int, int>;             // 网格坐标 (row, col)，first 为行号，second 为列号

inline constexpr int kGoldValue = 50;             // 每枚金币的资源价值
inline constexpr int kTrapValue = -30;            // 每个陷阱的资源惩罚（负值）
inline constexpr Position kInvalid{-1000000000, -1000000000}; // 表示"无效/未初始化"坐标的哨兵值
inline constexpr std::array<Position, 4> kDirs{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}}; // 四方向：下/上/右/左

// 玩家技能：id 为技能编号，damage 为单次伤害，cooldown 为使用后冷却回合数
struct Skill {
    int id;         // 技能编号（0-based index）
    int damage;     // 单次使用造成的伤害值
    int cooldown;   // 使用后需要冷却的回合数（冷却期间不能再次使用）
};

// 由输入 JSON 解析出的完整迷宫任务数据，供全局路径规划算法直接读取
struct MazeData {
    Json source;                                // 原始输入 JSON，Boss 战等子模块可能需要读取 B/PlayerSkills 等字段
    std::vector<std::vector<std::string>> grid; // 15x15 网格，每个格子为 "S""E""G""T""B""#"" " 之一
    Position start{kInvalid};                   // 起点 S 的坐标
    Position exit{kInvalid};                    // 出口 E 的坐标
    std::vector<Position> bosses;               // 所有 Boss 本体 B 的坐标列表
    std::vector<Position> golds;                // 所有金币 G 的坐标列表（供全局 TSP 贪心使用）
};

// 判断全局迷宫坐标是否可通行：在边界内、不是墙 #、不是活着的 Boss 本体 B
inline bool passable(const MazeData &data, int row, int col)
{
    return row >= 0 && col >= 0 && row < static_cast<int>(data.grid.size()) &&
           col < static_cast<int>(data.grid[0].size()) && data.grid[row][col] != "#" && data.grid[row][col] != "B";
}

// 计算单个格子的资源变化：金币 +50，陷阱 -30，其余 0
inline int scoreDelta(const std::string &tile)
{
    if (tile == "G") return kGoldValue;
    if (tile == "T") return kTrapValue;
    return 0;
}
} // namespace ai_player

#endif
