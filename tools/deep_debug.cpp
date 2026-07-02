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
    if (argc < 2) { std::cerr << "usage: deep_debug <maze.json>\n"; return 1; }

    std::ifstream fin(argv[1]);
    if (!fin) { std::cerr << "cannot open\n"; return 1; }
    std::ostringstream buf;
    buf << fin.rdbuf();
    fin.close();

    auto data = json::parse(buf.str());
    const auto &mazeJson = data["maze"];
    int rows = mazeJson.size(), cols = mazeJson[0].size();

    ai_player::MazeData mazeData;
    mazeData.source = data;
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

    auto result = ai_player::realtimeGreedyRun(mazeData);

    json out;
    out["maze"] = data["maze"];
    out["start"] = {mazeData.start.first, mazeData.start.second};
    out["exit"] = {mazeData.exit.first, mazeData.exit.second};

    int resource = 0;
    std::vector visited(rows, std::vector<bool>(cols, false));
    json jsteps = json::array();
    for (size_t i = 0; i < result.path.size(); ++i) {
        int r = result.path[i].first, c = result.path[i].second;
        bool firstVisit = !visited[r][c];
        int delta = 0;
        if (firstVisit) {
            visited[r][c] = true;
            if (mazeData.grid[r][c] == "G") delta = 50;
            if (mazeData.grid[r][c] == "T") delta = -30;
        }
        resource += delta;
        json s;
        s["step"] = i;
        s["row"] = r; s["col"] = c;
        s["tile"] = mazeData.grid[r][c];
        s["delta"] = delta;
        s["resource"] = resource;
        s["first_visit"] = firstVisit;
        jsteps.push_back(s);
    }
    out["path"] = jsteps;

    json jdebug = json::array();
    for (const auto &ds : result.debugSteps) {
        json jd;
        jd["step"] = ds.step;
        jd["local_r"] = ds.localCurrent.first;
        jd["local_c"] = ds.localCurrent.second;
        jd["real_r"] = ds.realCurrent.first;
        jd["real_c"] = ds.realCurrent.second;
        jd["alpha"] = ds.alpha;
        jd["observedRatio"] = ds.observedRatio;
        jd["qEff"] = ds.qEff;
        jd["decision"] = ds.decision;
        jd["selectedLocal"] = {ds.selectedLocal.first, ds.selectedLocal.second};
        jd["selectedReal"] = {ds.selectedReal.first, ds.selectedReal.second};

        json jcands = json::array();
        for (const auto &c : ds.candidates) {
            json jc;
            jc["local_r"] = c.localTarget.first;
            jc["local_c"] = c.localTarget.second;
            jc["real_r"] = c.realTarget.first;
            jc["real_c"] = c.realTarget.second;
            jc["tile"] = c.tile;
            jc["score"] = c.score;
            jc["deltaR"] = c.deltaR;
            jc["Iproxy"] = c.informationProxy;
            jc["tailGain"] = c.tailGain;
            jc["qEff"] = c.qEff;
            jc["pathLen"] = c.pathLength;
            jc["margin"] = c.marginPenalty;
            jc["projectedR"] = c.projectedResource;
            jc["selected"] = c.selected;
            jc["unknownCompSum"] = c.unknownComponentSum;
            jcands.push_back(jc);
        }
        jd["candidates"] = jcands;
        jdebug.push_back(jd);
    }
    out["debugSteps"] = jdebug;

    out["total_resource"] = resource;
    out["total_steps"] = result.path.empty() ? 0 : static_cast<int>(result.path.size()) - 1;

    std::string outName = argc >= 3 ? argv[2] : "deep_out.json";
    std::ofstream fout(outName);
    fout << out.dump(2);
    std::cout << "Written " << outName << " steps=" << result.path.size()-1 << " R=" << resource << "\n";
    return 0;
}
