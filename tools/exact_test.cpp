#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include "AIPlayerEngine.h"

int main(int argc, char **argv) {
    if (argc < 2) return 1;
    std::ifstream fin(argv[1]);
    std::ostringstream buf; buf << fin.rdbuf(); fin.close();
    std::string json = buf.str();

    AIPlayerEngine engine;

    auto t0 = std::chrono::steady_clock::now();
    auto v = engine.ValidateMaze(json);
    auto t1 = std::chrono::steady_clock::now();
    std::cout << "ValidateMaze: " << std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0).count() << "ms" << std::endl;

    t0 = std::chrono::steady_clock::now();
    auto b = engine.RunBoss(json);
    t1 = std::chrono::steady_clock::now();
    std::cout << "RunBoss: " << std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0).count() << "ms" << std::endl;

    t0 = std::chrono::steady_clock::now();
    auto g = engine.RunRealtimeGreedy(json);
    t1 = std::chrono::steady_clock::now();
    std::cout << "RunRealtimeGreedy: " << std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0).count() << "ms, len=" << g.size() << std::endl;

    t0 = std::chrono::steady_clock::now();
    auto s = engine.RunAdventure(json, "smart");
    t1 = std::chrono::steady_clock::now();
    std::cout << "RunAdventure(smart): " << std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0).count() << "ms" << std::endl;

    t0 = std::chrono::steady_clock::now();
    auto a = engine.RunAdventure(json, "astar");
    t1 = std::chrono::steady_clock::now();
    std::cout << "RunAdventure(astar): " << std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0).count() << "ms" << std::endl;

    std::cout << "ALL DONE" << std::endl;
    return 0;
}
