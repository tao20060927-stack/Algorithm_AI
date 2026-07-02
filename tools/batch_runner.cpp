#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"
#include "../src/GameTypes.h"
#include "../src/RealtimeGreedyStrategy.h"

using json = nlohmann::json;

int main()
{
    std::string dir = "C:/Users/tao20/Desktop/Algorithm/AIPlayerDesktop/tools/train_tmp";
    int count = 200;

    std::cout << "file,resource,steps,ratio\n";
    for (int i = 1; i <= count; ++i) {
        char fname[256];
        snprintf(fname, sizeof(fname), "%s/train_%04d.json", dir.c_str(), i);

        std::ifstream fin(fname);
        if (!fin) { std::cerr << "skip " << fname << "\n"; continue; }
        std::ostringstream buf;
        buf << fin.rdbuf();
        fin.close();

        try {
            auto data = json::parse(buf.str());
            if (!data.contains("maze")) continue;

            ai_player::MazeData mazeData;
            mazeData.source = data;
            const auto &mazeJson = data["maze"];
            int rows = mazeJson.size(), cols = mazeJson[0].size();
            mazeData.grid.assign(rows, std::vector<std::string>(cols));
            for (int r = 0; r < rows; ++r) {
                for (int c = 0; c < cols; ++c) {
                    std::string tile = mazeJson[r][c].get<std::string>();
                    mazeData.grid[r][c] = tile;
                    if (tile == "S") mazeData.start = {r, c};
                    if (tile == "E") mazeData.exit = {r, c};
                    if (tile == "B") mazeData.bosses.push_back({r, c});
                    if (tile == "G") mazeData.golds.push_back({r, c});
                }
            }

            auto path = ai_player::realtimeGreedyPath(mazeData);

            int resource = 0, steps = path.empty() ? 0 : static_cast<int>(path.size()) - 1;
            std::vector visited(rows, std::vector<bool>(cols, false));
            for (const auto &p : path) {
                int r = p.first, c = p.second;
                if (!visited[r][c]) {
                    visited[r][c] = true;
                    if (mazeData.grid[r][c] == "G") resource += 50;
                    if (mazeData.grid[r][c] == "T") resource -= 30;
                }
            }

            double ratio = steps > 0 ? static_cast<double>(resource) / steps : 0.0;

            // Count golds/traps observed along path
            int goldObserved = 0, trapTriggered = 0;
            for (int r = 0; r < rows; ++r)
                for (int c = 0; c < cols; ++c)
                    if (visited[r][c]) {
                        if (mazeData.grid[r][c] == "G") ++goldObserved;
                        if (mazeData.grid[r][c] == "T") ++trapTriggered;
                    }

            std::cout << "train_" << (i < 10 ? "000" : i < 100 ? "00" : "0") << i
                      << "," << resource << "," << steps << "," << ratio << "\n";
            std::cout.flush();
        } catch (const std::exception &e) {
            std::cerr << "error " << fname << ": " << e.what() << "\n";
        }
    }
    return 0;
}
