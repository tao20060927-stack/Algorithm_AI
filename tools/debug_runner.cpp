#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"
#include "../src/GameTypes.h"
#include "../src/RealtimeGreedyStrategy.h"

using json = nlohmann::json;

int main(int argc, char **argv)
{
    if (argc < 2) { std::cerr << "usage: debug_runner <maze.json> [output.json]\n"; return 1; }
    std::string inName = argv[1];
    std::string outName = argc >= 3 ? argv[2] : "debug_out.json";

    std::ifstream fin(inName);
    if (!fin) { std::cerr << "cannot open " << inName << "\n"; return 1; }
    std::ostringstream buf;
    buf << fin.rdbuf();
    fin.close();

    auto data = json::parse(buf.str());
    if (!data.contains("maze")) return 1;

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

    json out;
    out["maze"] = data["maze"];
    out["start"] = {mazeData.start.first, mazeData.start.second};
    out["exit"] = {mazeData.exit.first, mazeData.exit.second};
    out["path_length"] = path.size();
    out["path"] = json::array();
    for (const auto &p : path) {
        out["path"].push_back({p.first, p.second});
    }

    // Compute step-by-step state
    int resource = 0;
    json steps = json::array();
    std::vector visited(rows, std::vector<bool>(cols, false));
    for (size_t i = 0; i < path.size(); ++i) {
        int r = path[i].first, c = path[i].second;
        bool firstVisit = !visited[r][c];
        int delta = 0;
        if (firstVisit) {
            visited[r][c] = true;
            if (mazeData.grid[r][c] == "G") delta = 50;
            if (mazeData.grid[r][c] == "T") delta = -30;
        }
        resource += delta;
        json step;
        step["step"] = i;
        step["row"] = r;
        step["col"] = c;
        step["tile"] = mazeData.grid[r][c];
        step["delta"] = delta;
        step["resource"] = resource;
        step["first_visit"] = firstVisit;
        steps.push_back(step);
    }
    out["steps"] = steps;

    std::ofstream fout(outName);
    fout << out.dump(2);
    fout.close();
    std::cout << "Written " << outName << " (" << path.size() << " steps, R=" << resource << ")\n";
    return 0;
}
