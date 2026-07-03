#ifndef REWARD_H
#define REWARD_H

#include "GameTypes.h"
#include "RewardConfig.h"

#include <map>
#include <set>

namespace ai_player {

// 迷宫入口朝向枚举，MapPoseEstimator 用其将局部坐标旋转映射到估计 15x15 掩码
enum class Direction {
    Up,     // 从上方边界进入迷宫，局部坐标 y 轴对应估计坐标从上到下
    Down,   // 从下方边界进入迷宫，局部坐标 y 轴对应估计坐标从下到上
    Left,   // 从左方边界进入迷宫，局部坐标需要行列交换加符号调整
    Right   // 从右方边界进入迷宫，局部坐标需要行列交换
};

// 奖励函数全部可调参数集合，每个算法实例可独立持有不同参数
struct RewardParameters {
    double omegaI = reward_config::kOmegaI;                   // 信息价值项权重，控制 I_proxy 在总分中的占比
    double alpha0 = reward_config::kAlpha0;                   // 动态探索权重基准值，位于 α_raw 分子最外层
    double alphaMin = reward_config::kAlphaMin;               // α_t 裁剪下限，风险极高时也保留最低探索倾向
    double alphaMax = reward_config::kAlphaMax;               // α_t 裁剪上限，防止虚拟信息项压过真实金币
    double theta = reward_config::kTheta;                     // α 的 EMA 平滑系数，越大越不容易剧烈波动
    double beta = reward_config::kBeta;                       // 边际尾部金币价值权重，控制 V_tail^marg 在总分中的占比
    double kappaU = reward_config::kKappaU;                   // I_proxy 连通块面积贡献权重，乘在 Σ min(|C|,cap)×ρ_area 前
    int areaMax = reward_config::kAreaMax;                    // 出口未知时 |C| 面积裁剪上限，单个连通块最多计入此数量
    int bossEdgeAreaBonus = reward_config::kBossEdgeAreaBonus;// Boss-gated 区域一律使用的 |C| 替代值
    double knownExitAreaCap = reward_config::kKnownExitAreaCap;// 出口已知后 |C| 面积裁剪上限，削弱开阔区域探索奖励
    double rhoAreaValueMin = reward_config::kRhoAreaValueMin; // ρ_area_value 的 clip 下界，避免价值密度被压到零
    double qMin = reward_config::kQMin;                       // q_eff 下限，保证开局 R=0 时路径长度仍有基础代价
    double qMax = reward_config::kQMax;                       // q_eff 上限，防止高资源状态下路径长度代价过度放大
    double qEffLengthWeight = reward_config::kQEffLengthWeight;// 路径长度代价项的全局缩放系数 η_q，乘在 q_eff × len 前
    double switchMargin = reward_config::kSwitchMargin;       // 目标保持切换阈值，新目标需比旧目标高出此分数才换
    double gammaClosedSingleton = reward_config::kGammaClosedSingleton;// Closed Singleton Gate 的 c_A 权重 γ
    double marginClosedSingleton = reward_config::kMarginClosedSingleton;// Closed Singleton Gate 拒绝 A 的保守门槛偏移
    int pocketRadius = reward_config::kPocketRadius;          // Pocket 识别半径，金币到 hub 距离 ≤ 此值才算同一口袋
    double pocketMu = reward_config::kPocketMu;               // remainI 的距离折扣系数，dist 每多 1 步除以 (1+μ)
    double pocketLambdaRemain = reward_config::kPocketLambdaRemain;// remainI 在 Score_first 中的权重
    double tau = reward_config::kTau;                         // 停止探索阈值，bestScore ≤ τ 且 obs 达标时转向出口
    double wU = reward_config::kWU;                           // α_raw 分子中 ρ_U 的系数，控制未知占比对探索的提升
    double wV = reward_config::kWV;                           // α_raw 分子中 v_unk/50 的系数，控制期望净值对探索的提升
    double wR = reward_config::kWR;                           // α_raw 分母中陷阱密度的系数，控制风险对探索的抑制
    double lambda = reward_config::kLambda;                   // 密度估计平滑伪计数，值越大早期密度越靠向先验
    double lambdaG = reward_config::kLambdaG;                 // 金币密度先验伪计数，加在 N_G 上
    double lambdaT = reward_config::kLambdaT;                 // 陷阱密度先验伪计数，加在 N_T 上
    double epsilon = reward_config::kEpsilon;                 // 防除零小量，用于 q_ref 分母 R/(L+ε)
};

// 局部记忆地图中的一个格子，存储 AI 通过 3x3 观察积累的所有已知信息
struct LocalCell {
    std::string tile = "U";   // 格子类型字符串："G"金币/"T"陷阱/"B"Boss/"E"出口/"#"墙/" "路/"U"未知
    bool observed = false;    // 是否已被 3x3 视野观察过（AI 知道这个格子的 tile）
    bool outside = false;     // 是否已被确认在迷宫外部（越界，不可通行也不计入未知区域）
    bool visited = false;     // AI 是否实际踩过这个格子（决定能否作为候选目标）
    bool collected = false;   // 该格金币是否已被拾取（防止重复加分）
    bool triggered = false;   // 该格陷阱是否已被触发（防止重复扣分）
    bool bossTrigger = false; // 是否被标记为 Boss 正邻接触发区（走到即强制触发 Boss 战）
};

// AI 的局部记忆地图，只保存通过 3x3 视野逐步观察和标记的信息，不读取完整迷宫
class LocalKnownMap {
public:
    void setObserved(Position localPos, const std::string &tile);   // 记录一个局部格子被观察到的真实类型
    void setOutside(Position localPos);                             // 标记局部格子位于迷宫外部（越界）
    void markBossTriggers(Position localBoss);                      // 根据 Boss 本体位置标记上下左右四个触发区
    void clearBoss(Position localBoss);                             // Boss 战击败后将 Boss 本体改为普通通路
    void markVisited(Position localPos);                            // 标记该格已被 AI 实际踩过
    void markCollected(Position localPos);                          // 标记该格金币已被拾取
    void markTriggered(Position localPos);                          // 标记该格陷阱已被触发
    bool has(Position localPos) const;                              // 该位置是否存在于记忆地图中（任意状态）
    bool isObserved(Position localPos) const;                       // 该位置是否已被 3x3 视野观察过
    bool isOutside(Position localPos) const;                        // 该位置是否被确认为迷宫外部
    bool isVisited(Position localPos) const;                        // AI 是否实际踩过该格
    bool isCollected(Position localPos) const;                      // 该格金币是否已被拾取
    bool isTriggered(Position localPos) const;                      // 该格陷阱是否已被触发
    bool isBossTrigger(Position localPos) const;                    // 该格是否为 Boss 正邻接触发区
    bool isWalkableForPlanning(Position localPos) const;            // 该格能否作为路径规划节点（已观察、非墙、非活 Boss）
    std::string tile(Position localPos) const;                      // 获取该格已知类型，未知格返回 "U"
    std::vector<Position> observedPositions() const;                // 列出所有已观察过的局部坐标
    std::vector<Position> outsidePositions() const;                 // 列出所有已确认越界的局部坐标
    std::vector<Position> knownCoins() const;                       // 列出当前已知且未拾取的金币坐标

private:
    std::map<Position, LocalCell> cells_;   // 以局部坐标为键的格子信息表，未插入的坐标视为从未被观察到
};

// 一个候选的 15x15 掩码嵌入假设，描述"AI 认为入口在 15x15 迷宫的哪个位置、朝向哪个方向"
struct MapEmbeddingHypothesis {
    Position entry{kInvalid};              // 估计的入口坐标（在 15x15 坐标系中）
    Direction inwardDirection = Direction::Up; // 从入口进入迷宫后的前进方向
    double score = 0.0;                    // 该假设的评分（越高越可信，基于 edgeTouches 和 centerDistance）
    bool feasible = false;                 // 当前已观察格是否都落在此假设的 15x15 范围内
};

// 地图姿态估计器：仅用局部观察推断 AI 在 15x15 迷宫中的大致位置和朝向
class MapPoseEstimator {
public:
    MapPoseEstimator();                                                          // 构造并初始化多入口假设
    void initialize();                                                           // 重置所有假设（四条边各 5 个偏移入口）
    void setMaskEnabled(bool enabled);                                            // 控制是否允许启用 15x15 掩码机制
    void update(const LocalKnownMap &localMap, Position localCurrent);           // 用最新观察更新最佳假设
    MapEmbeddingHypothesis best() const;                                         // 返回当前评分最高的可行入口假设
    bool isMaskActive() const;                                                   // 掩码是否已完全确定并启用（localMap 跨度覆盖 15 格）
    Position localToEstimatedGlobal(Position localPos) const;                     // 将局部坐标映射到估计 15x15 坐标
    bool isInsideEstimatedMaze(Position localPos) const;                         // 映射后坐标是否在 15x15 范围内
    int estimatedUnknownCount() const;                                           // 估计 15x15 中仍未知的格子数（225 - observed）
    int estimatedObservedCount() const;                                          // 映射去重后的已观察格数量
    std::vector<int> unknownComponentSizesTouchingView(Position localTarget,     // 统计与目标 3x3 视野接触的未知连通块大小
                                                       const LocalKnownMap &localMap,
                                                       int areaMax) const;
    bool unknownExtensionTouchesMazeEdge(Position localTarget,                   // 判断目标的未知延伸是否触达估计迷宫边缘
                                         const LocalKnownMap &localMap) const;

private:
    static constexpr int kEstimatedSize = 15;   // 估计迷宫固定为 15x15
    // 掩码种子类型：AI 根据出生点 3x3 观察判断自己在迷宫边界的哪一侧
    enum class MaskSeedKind { Internal, Top, Bottom, Left, Right, TopLeft, TopRight, BottomLeft, BottomRight };

    std::vector<MapEmbeddingHypothesis> hypotheses_;  // 所有候选入口假设（四条边界各 5 个位置 × 4 方向 = 20 个）
    MapEmbeddingHypothesis best_;                     // 当前评分最高的可行假设
    std::set<Position> observedEstimated_;            // 当前最佳假设下映射去重后的已观察估计坐标
    bool maskEnabled_ = true;                         // 是否允许启用 15x15 掩码机制，非 15x15 迷宫会关闭
    bool maskSeeded_ = false;                         // 是否已根据出生观察确定了 seedKind_
    bool maskActive_ = false;                         // 完整 15x15 掩码是否已启用（localMap 跨度覆盖 15 格）
    MaskSeedKind seedKind_ = MaskSeedKind::Internal;  // 出生点位于迷宫边界的哪一侧

    Position mapWithHypothesis(Position localPos,                                 // 按指定假设把局部坐标映射为估计 15x15 坐标
                               const MapEmbeddingHypothesis &hypothesis) const;
};

// AI 的状态快照，随每一步探索实时更新，传入 reward 上下文
struct AgentState {
    int resource = 0;           // 当前累计资源（金币 +50 / 陷阱 -30 / Boss 复活扣减）
    int steps = 0;              // 当前已走步数（从起点开始计数，初值为 0）
    int collectedGold = 0;      // 已拾取金币数量（用于停止探索规则中的低 ratio 保护）
    double alphaSmooth = 4.0;   // 当前平滑后的动态探索权重 α_t^smooth
};

// 传入 PathValueEvaluator 的评分上下文，包含当前状态和可选出口路径
struct PathValueContext {
    AgentState state;                               // 当前资源、步数、金币数和 α
    std::vector<Position> exitPath;                  // 从当前位置到出口的局部路径（出口未知或不可达时为空）
    std::set<Position> bossGatedAreaMaxTargets;      // 被判定为 Boss-gated 的候选目标集合（I_proxy 中按 bossEdgeAreaBonus 处理）
};

// 路径价值评估器：实现完整的 reward 公式，评估候选路径并管理 α 更新
class PathValueEvaluator {
public:
    explicit PathValueEvaluator(RewardParameters parameters = {});   // 用指定参数构造评估器
    const RewardParameters &parameters() const;                     // 读取当前参数（只读）

    // 计算路径上所有未拾取金币和未触发陷阱的一次性资源变化（跳过 path[0]）
    int pathResourceDelta(const std::vector<Position> &path, const LocalKnownMap &localMap) const;

    // 计算 I_proxy：候选目标的探索信息价值 = κ_u × Σ min(|C|, cap) × ρ_area_value
    double informationProxy(Position target, const LocalKnownMap &localMap,
                            const MapPoseEstimator &poseEstimator, double areaCap = -1.0,
                            bool forceAreaMax = false) const;

    // 计算 V_tail^marg：候选目标后已知金币的边际尾部价值上界
    double futureGainMarginal(Position target, const std::vector<Position> &path,
                              const LocalKnownMap &localMap) const;

    // 计算 q_eff：路径步数代价系数 = min(max(q_ref, q_min), q_max)
    double computeQEff(const PathValueContext &context, const LocalKnownMap &localMap) const;

    // 根据当前观察统计更新平滑动态 α
    double updateAlphaSmooth(double previousAlpha, const LocalKnownMap &localMap,
                             const MapPoseEstimator &poseEstimator) const;

    // 主评分函数：Score = ΔR + ωI·α·I_proxy + β·V_tail − ηq·qEff·len
    double evaluate(const std::vector<Position> &path, Position target, const PathValueContext &context,
                    const LocalKnownMap &localMap, const MapPoseEstimator &poseEstimator) const;

    // 在局部已知地图上 BFS 搜索两点最短路径（只走已观察可通行格）
    std::vector<Position> shortestPathOnKnownMap(Position start, Position target,
                                                 const LocalKnownMap &localMap) const;

private:
    RewardParameters parameters_;   // 本次评估使用的参数副本
};

} // namespace ai_player

#endif
