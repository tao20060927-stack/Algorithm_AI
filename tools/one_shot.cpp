#include <fstream>
#include <iostream>
#include <sstream>
#include "nlohmann/json.hpp"
#include "../src/GameTypes.h"
#include "../src/RealtimeGreedyStrategy.h"
using json = nlohmann::json;

int main(int argc, char **argv) {
    if (argc < 2) return 1;
    std::ifstream fin(argv[1]);
    if (!fin) return 1;
    std::ostringstream buf; buf << fin.rdbuf(); fin.close();
    auto data = json::parse(buf.str());
    const auto &mazeJson = data["maze"];
    int rows = mazeJson.size(), cols = mazeJson[0].size();
    ai_player::MazeData md; md.source = data;
    md.grid.assign(rows, std::vector<std::string>(cols));
    int goldTotal = 0, trapTotal = 0;
    for (int r = 0; r < rows; ++r) for (int c = 0; c < cols; ++c) {
        std::string t = mazeJson[r][c].get<std::string>();
        md.grid[r][c] = t;
        if (t == "S") md.start = {r, c};
        if (t == "E") md.exit = {r, c};
        if (t == "B") md.bosses.push_back({r, c});
        if (t == "G") md.golds.push_back({r, c}), ++goldTotal;
        if (t == "T") ++trapTotal;
    }
    auto path = ai_player::realtimeGreedyPath(md);
    int R = 0, L = path.empty() ? 0 : (int)path.size() - 1;
    std::vector vis(rows, std::vector<bool>(cols, false));
    for (auto &p : path) {
        int r = p.first, c = p.second;
        if (!vis[r][c]) { vis[r][c] = true;
            if (md.grid[r][c] == "G") R += 50;
            if (md.grid[r][c] == "T") R -= 30; }
    }
    bool exit = !path.empty() && path.back().first == md.exit.first && path.back().second == md.exit.second;
    json out; out["R"] = R; out["L"] = L; out["ratio"] = L > 0 ? (double)R/L : 0.0;
    out["reached_exit"] = exit; out["golds"] = goldTotal; out["traps"] = trapTotal;
    std::cout << out.dump() << "\n";
    return 0;
}
