#ifndef GAME_TYPES_H
#define GAME_TYPES_H

#include <array>
#include <string>
#include <utility>
#include <vector>

#include "nlohmann/json.hpp"

namespace ai_player {
using Json = nlohmann::json;
using Position = std::pair<int, int>;

inline constexpr int kGoldValue = 50;
inline constexpr int kTrapValue = -30;
inline constexpr Position kInvalid{-1000000000, -1000000000};
inline constexpr std::array<Position, 4> kDirs{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};

struct Skill {
    int id;
    int damage;
    int cooldown;
};

struct MazeData {
    Json source;
    std::vector<std::vector<std::string>> grid;
    Position start{kInvalid};
    Position exit{kInvalid};
    std::vector<Position> locks;
    std::vector<Position> bosses;
    std::vector<Position> golds;
};

inline bool passable(const MazeData &data, int row, int col)
{
    return row >= 0 && col >= 0 && row < static_cast<int>(data.grid.size()) &&
           col < static_cast<int>(data.grid[0].size()) && data.grid[row][col] != "#" && data.grid[row][col] != "B";
}

inline int scoreDelta(const std::string &tile)
{
    if (tile == "G") return kGoldValue;
    if (tile == "T") return kTrapValue;
    return 0;
}
} // namespace ai_player

#endif
