#ifndef REWARD_H
#define REWARD_H

#include "GameTypes.h"
#include "RewardConfig.h"

#include <map>
#include <set>

namespace ai_player {

enum class Direction {
    Up,
    Down,
    Left,
    Right
};

struct RewardParameters {
    double omegaI = reward_config::kOmegaI;
    double alpha0 = reward_config::kAlpha0;
    double alphaMin = reward_config::kAlphaMin;
    double alphaMax = reward_config::kAlphaMax;
    double theta = reward_config::kTheta;
    double beta = reward_config::kBeta;
    double kappaU = reward_config::kKappaU;
    int areaMax = reward_config::kAreaMax;
    int bossEdgeAreaBonus = reward_config::kBossEdgeAreaBonus;
    double knownExitAreaCap = reward_config::kKnownExitAreaCap;
    double rhoAreaValueMin = reward_config::kRhoAreaValueMin;
    double qMin = reward_config::kQMin;
    double qEffLengthWeight = reward_config::kQEffLengthWeight;
    int safeResource = reward_config::kSafeResource;
    double lambdaMargin = reward_config::kLambdaMargin;
    double switchMargin = reward_config::kSwitchMargin;
    double gammaClosedSingleton = reward_config::kGammaClosedSingleton;
    double marginClosedSingleton = reward_config::kMarginClosedSingleton;
    int pocketRadius = reward_config::kPocketRadius;
    double pocketMu = reward_config::kPocketMu;
    double pocketLambdaRemain = reward_config::kPocketLambdaRemain;
    double tau = reward_config::kTau;
    double wU = reward_config::kWU;
    double wV = reward_config::kWV;
    double wR = reward_config::kWR;
    double lambda = reward_config::kLambda;
    double lambdaG = reward_config::kLambdaG;
    double lambdaT = reward_config::kLambdaT;
    double epsilon = reward_config::kEpsilon;
};

struct LocalCell {
    std::string tile = "U";
    bool observed = false;
    bool outside = false;
    bool visited = false;
    bool collected = false;
    bool triggered = false;
    bool bossTrigger = false;
};

class LocalKnownMap {
public:
    void setObserved(Position localPos, const std::string &tile);
    void setOutside(Position localPos);
    void markBossTriggers(Position localBoss);
    void clearBoss(Position localBoss);
    void markVisited(Position localPos);
    void markCollected(Position localPos);
    void markTriggered(Position localPos);
    bool has(Position localPos) const;
    bool isObserved(Position localPos) const;
    bool isOutside(Position localPos) const;
    bool isVisited(Position localPos) const;
    bool isCollected(Position localPos) const;
    bool isTriggered(Position localPos) const;
    bool isBossTrigger(Position localPos) const;
    bool isWalkableForPlanning(Position localPos) const;
    std::string tile(Position localPos) const;
    std::vector<Position> observedPositions() const;
    std::vector<Position> outsidePositions() const;
    std::vector<Position> knownCoins() const;

private:
    std::map<Position, LocalCell> cells_;
};

struct MapEmbeddingHypothesis {
    Position entry{kInvalid};
    Direction inwardDirection = Direction::Up;
    double score = 0.0;
    bool feasible = false;
};

class MapPoseEstimator {
public:
    MapPoseEstimator();
    void initialize();
    void update(const LocalKnownMap &localMap, Position localCurrent);
    MapEmbeddingHypothesis best() const;
    bool isMaskActive() const;
    Position localToEstimatedGlobal(Position localPos) const;
    bool isInsideEstimatedMaze(Position localPos) const;
    int estimatedUnknownCount() const;
    int estimatedObservedCount() const;
    std::vector<int> unknownComponentSizesTouchingView(Position localTarget, const LocalKnownMap &localMap,
                                                       int areaMax) const;
    bool unknownExtensionTouchesMazeEdge(Position localTarget, const LocalKnownMap &localMap) const;

private:
    static constexpr int kEstimatedSize = 15;
    enum class MaskSeedKind { Internal, Top, Bottom, Left, Right, TopLeft, TopRight, BottomLeft, BottomRight };

    std::vector<MapEmbeddingHypothesis> hypotheses_;
    MapEmbeddingHypothesis best_;
    std::set<Position> observedEstimated_;
    bool maskSeeded_ = false;
    bool maskActive_ = false;
    MaskSeedKind seedKind_ = MaskSeedKind::Internal;

    Position mapWithHypothesis(Position localPos, const MapEmbeddingHypothesis &hypothesis) const;
};

struct AgentState {
    int resource = 0;
    int steps = 0;
    int collectedGold = 0;
    double alphaSmooth = 4.0;
};

struct PathValueContext {
    AgentState state;
    std::vector<Position> exitPath;
    std::set<Position> bossGatedAreaMaxTargets;
};

class PathValueEvaluator {
public:
    explicit PathValueEvaluator(RewardParameters parameters = {});
    const RewardParameters &parameters() const;
    int pathResourceDelta(const std::vector<Position> &path, const LocalKnownMap &localMap) const;
    bool pathKeepsResourceNonNegative(const std::vector<Position> &path, int currentResource,
                                      const LocalKnownMap &localMap) const;
    double informationProxy(Position target, const LocalKnownMap &localMap,
                            const MapPoseEstimator &poseEstimator, double areaCap = -1.0,
                            bool forceAreaMax = false) const;
    double futureGainMarginal(Position target, const std::vector<Position> &path,
                              const LocalKnownMap &localMap) const;
    double computeQEff(const PathValueContext &context, const LocalKnownMap &localMap) const;
    double marginPenalty(int projectedResource) const;
    double updateAlphaSmooth(double previousAlpha, const LocalKnownMap &localMap,
                             const MapPoseEstimator &poseEstimator) const;
    double evaluate(const std::vector<Position> &path, Position target, const PathValueContext &context,
                    const LocalKnownMap &localMap, const MapPoseEstimator &poseEstimator) const;
    std::vector<Position> shortestPathOnKnownMap(Position start, Position target,
                                                 const LocalKnownMap &localMap) const;

private:
    RewardParameters parameters_;
};

} // namespace ai_player

#endif
