#ifndef AI_PLAYER_ENGINE_H
#define AI_PLAYER_ENGINE_H

#include <string>
#include <unordered_map>

// 顶层引擎接口：接收 JSON 输入，调度各子模块运行，返回 JSON 结果
class AIPlayerEngine {
public:
    // 运行 3x3 实时贪心探索（Dinkelbach 加性 surrogate reward + 局部记忆地图）
    std::string RunRealtimeGreedy(const std::string &inputJson);

    // 运行资源拾取策略（PDF 第一问的 3x3 投影比值贪心）
    std::string RunResourcePickup(const std::string &inputJson);

    // 运行全局探险路径规划，algorithm 可选 smart/dijkstra/astar/branch_bound/divide_conquer
    std::string RunAdventure(const std::string &inputJson, const std::string &algorithm);

    // 运行 Boss 战求解（滚动时域伤害容量规划 + 分支限界）
    std::string RunBoss(const std::string &inputJson);

    // 校验迷宫可达性（起点到出口、Boss、金币是否连通）
    std::string ValidateMaze(const std::string &inputJson);

private:
    // 结果缓存：相同 inputJson + algorithm 组合不重复计算，直接返回已缓存 JSON
    std::unordered_map<std::string, std::string> resultCache_;
};

#endif
