#include <fstream>
#include <iostream>
#include <sstream>
#include "nlohmann/json.hpp"
#include "../src/ResourcePickupStrategy.h"

int main(int argc, char **argv) {
    if (argc < 2) return 1;
    std::ifstream fin(argv[1]);
    std::ostringstream buf; buf << fin.rdbuf(); fin.close();
    auto result = ai_player::solveResourcePickupJson(nlohmann::json::parse(buf.str()));
    std::cout << result.dump() << std::endl;
    return 0;
}
