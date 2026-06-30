#ifndef AI_PLAYER_ENGINE_H
#define AI_PLAYER_ENGINE_H

#include <string>
#include <unordered_map>

class AIPlayerEngine {
public:
    std::string RunRealtimeGreedy(const std::string &inputJson);
    std::string RunAdventure(const std::string &inputJson, const std::string &algorithm);
    std::string SolveLock(const std::string &inputJson);
    std::string RunBoss(const std::string &inputJson);
    std::string ValidateMaze(const std::string &inputJson);

private:
    std::unordered_map<std::string, std::string> resultCache_;
};

#endif
