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
    const char *subdirs[] = {"train", "val", "test"};
    int counts[] = {800, 100, 100};
    std::string base = "C:/Users/tao20/Desktop/Algorithm/AIPlayerDesktop/tools/all_tmp";

    for (int d = 0; d < 3; ++d) {
        for (int i = 1; i <= counts[d]; ++i) {
            char fname[512];
            snprintf(fname, sizeof(fname), "%s/%s/%s_%04d.json", base.c_str(), subdirs[d], subdirs[d], i);

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
                for (int r = 0; r < rows; ++r)
                    for (int c = 0; c < cols; ++c) {
                        std::string tile = mazeJson[r][c].get<std::string>();
                        mazeData.grid[r][c] = tile;
                        if (tile == "S") mazeData.start = {r, c};
                        if (tile == "E") mazeData.exit = {r, c};
                        if (tile == "B") mazeData.bosses.push_back({r, c});
                        if (tile == "G") mazeData.golds.push_back({r, c});
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

                int goldTotal = 0, trapTotal = 0;
                for (int r = 0; r < rows; ++r)
                    for (int c = 0; c < cols; ++c) {
                        if (mazeData.grid[r][c] == "G") ++goldTotal;
                        if (mazeData.grid[r][c] == "T") ++trapTotal;
                    }

                bool reachedExit = !path.empty() &&
                    path.back().first == mazeData.exit.first &&
                    path.back().second == mazeData.exit.second;

                double ratio = steps > 0 ? static_cast<double>(resource) / steps : 0.0;

                json out;
                out["file"] = std::string(subdirs[d]) + "_" + std::to_string(i);
                out["resource"] = resource;
                out["steps"] = steps;
                out["ratio"] = ratio;
                out["reached_exit"] = reachedExit;
                out["gold_total"] = goldTotal;
                out["trap_total"] = trapTotal;

                std::cout << out.dump() << "\n";
                std::cout.flush();
            } catch (const std::exception &e) {
                std::cerr << "error " << fname << ": " << e.what() << "\n";
            }
        }
    }
    return 0;
}
