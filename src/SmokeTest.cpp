#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "AIPlayerEngine.h"
#include "nlohmann/json.hpp"

int main(int argc, char **argv)
{
    if (argc != 2) {
        std::cerr << "usage: ai_player_smoke <input.json>\n";
        return 2;
    }

    std::ifstream input(argv[1]);
    if (!input) {
        std::cerr << "failed to open input file\n";
        return 2;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    const std::string jsonText = buffer.str();
    const auto source = nlohmann::json::parse(jsonText);

    AIPlayerEngine engine;
    const auto validate = nlohmann::json::parse(engine.ValidateMaze(jsonText));
    const auto lock = nlohmann::json::parse(engine.SolveLock(jsonText));
    const auto boss = nlohmann::json::parse(engine.RunBoss(jsonText));
    const auto greedy = nlohmann::json::parse(engine.RunRealtimeGreedy(jsonText));
    const auto smart = nlohmann::json::parse(engine.RunAdventure(jsonText, "smart"));
    const auto astar = nlohmann::json::parse(engine.RunAdventure(jsonText, "astar"));

    std::cout << "validate.ok=" << validate.value("ok", false) << "\n";
    std::cout << "lock.ok=" << lock.value("ok", false) << "\n";
    std::cout << "boss.ok=" << boss.value("ok", false) << "\n";
    std::cout << "greedy.ok=" << greedy.value("ok", false) << "\n";
    std::cout << "smart.ok=" << smart.value("ok", false) << "\n";
    std::cout << "astar.ok=" << astar.value("ok", false) << "\n";
    std::cout << "astar.finished=" << astar.value("finished", false) << "\n";
    std::cout << "resource=" << astar.value("resource", 0) << "\n";
    std::cout << "steps=" << astar.value("steps", 0) << "\n";

    const bool requireLockSolved = source.contains("password");
    return validate.value("ok", false) && (!requireLockSolved || lock.value("ok", false)) &&
           boss.value("ok", false) && greedy.value("ok", false) &&
           smart.value("ok", false) && astar.value("ok", false) &&
           astar.value("finished", false)
        ? 0
        : 1;
}
