#ifndef REWARD_CONFIG_H
#define REWARD_CONFIG_H

namespace ai_player::reward_config {

/*
 * 实时贪心主评分公式：
 *
 * Score(t) =
 *     DeltaR_real(path_t)
 *   + omegaI * alpha_t_smooth * I_proxy(t)
 *   + beta * FutureGain_marg_tailUB(t)
 *   - qEffLengthWeight * q_eff * len(path_t)
 *   - marginPenalty(R + DeltaR_real(path_t))
 *
 * 其中：
 * I_proxy(t) = kappaU * sum_C min(|C|, areaMax) * rho_area_value
 *
 * rho_area_value = clip(max(v_area, 0) / 50, rhoAreaValueMin, 1)
 *
 * v_area = 50 * rhoG - 30 * rhoT
 *
 * q_eff = max(q_ref, qMin)
 *
 * marginPenalty(r) =
 *     lambdaMargin * (max(0, safeResource - r) / safeResource)^2
 *
 * alpha_t_raw =
 *     alpha0
 *     * (1 + wU * rhoU + wV * max(v_unk, 0) / 50)
 *     / (1 + wR * rhoT)
 *
 * alpha_t_smooth = theta * alpha_{t-1}_smooth + (1 - theta) * clip(alpha_t_raw, alphaMin, alphaMax)
 *
 * v_unk = 50 * rhoG - 30 * rhoT
 */

// omegaI 位于 omegaI * alpha_t_smooth * I_proxy(t)，控制探索信息价值在总分中的权重。
inline constexpr double kOmegaI = 0.175;

// alpha0 位于 alpha_t_raw，是动态探索权重的基准值。
inline constexpr double kAlpha0 = 4.0;

// alphaMin / alphaMax 位于 clip(alpha_t_raw, alphaMin, alphaMax)，限制探索权重上下界。
inline constexpr double kAlphaMin = 1.0;
inline constexpr double kAlphaMax = 8.0;

// theta 位于 alpha_t_smooth，控制动态探索权重的平滑程度，越大越不容易剧烈波动。
inline constexpr double kTheta = 0.8;

// beta 位于 beta * FutureGain_marg_tailUB(t)，控制后续金币机会上界的影响；当前只作为弱 tail 上界。
inline constexpr double kBeta = 1.23;

// kappaU 位于 I_proxy(t)，控制未知连通块面积价值对探索信息价值的贡献。
inline constexpr double kKappaU = 60;

// areaMax 位于 min(|C|, areaMax) * rho_area_value，限制单个未知连通块按多少个未知格计算潜在价值。
inline constexpr int kAreaMax = 12;

// rhoAreaValueMin 位于 rho_area_value 的 clip 下界，避免价值密度被压到完全没有探索收益。
inline constexpr double kRhoAreaValueMin = 0.001;

// qMin 位于 q_eff = max(q_ref, qMin)，保证开局资源为 0 时路径长度仍有基础代价。
inline constexpr double kQMin = 1.0;

// qEffLengthWeight 位于 qEffLengthWeight * q_eff * len(path_t)，控制路径长度机会成本项的整体权重。
inline constexpr double kQEffLengthWeight = 0.82;

// safeResource 位于 marginPenalty(r)，表示低资源安全线。
inline constexpr int kSafeResource = 30;

// lambdaMargin 位于 marginPenalty(r)，控制低资源 barrier 惩罚强度。
inline constexpr double kLambdaMargin = 8.0;

// switchMargin 位于目标保持条件 Score(best) > Score(currentTarget) + switchMargin，防止目标频繁横跳。
inline constexpr double kSwitchMargin = 5.0;

// pocketRadius 位于局部资源口袋识别 d(h,g)<=pocketRadius，控制一个 hub 附近多远的金币会被视为同一口袋。
inline constexpr int kPocketRadius = 2;

// pocketMu 位于 RemainI(g)=Iproxy(u)/(1+pocketMu*dist(g,u))，控制保留后续出口时的距离折扣。
inline constexpr double kPocketMu = 0.2;

// pocketLambdaRemain 位于 Score_first=Base+lambdaRemain*RemainI，控制“把高 Iproxy 金币留到后面”的影响强度。
inline constexpr double kPocketLambdaRemain = 0.2;

// tau 位于停止探索条件 bestScore <= tau，控制何时停止继续探索并转向出口。
inline constexpr double kTau = 5.0;

// wU 位于 alpha_t_raw 分子，控制未知区域比例 rhoU 对探索权重的提升。
inline constexpr double kWU = 1.0;

// wV 位于 alpha_t_raw 分子，控制未知区域期望净值 max(v_unk, 0) 对探索权重的提升。
inline constexpr double kWV = 1.0;

// wR 位于 alpha_t_raw 分母，控制陷阱风险对探索权重的抑制。
inline constexpr double kWR = 2.0;

// lambda 位于 rhoG/rhoT 的分母 N_obs + lambda，用于平滑观察统计。
inline constexpr double kLambda = 10.0;

// lambdaG 位于 rhoG = (N_G + lambdaG) / (N_obs + lambda)，用于金币密度先验平滑。
inline constexpr double kLambdaG = 1.0;

// lambdaT 位于 rhoT = (N_T + lambdaT) / (N_obs + lambda)，用于陷阱密度先验平滑。
inline constexpr double kLambdaT = 1.0;

// epsilon 位于 q_ref 的分母，避免步数为 0 时除零。
inline constexpr double kEpsilon = 1e-6;

} // namespace ai_player::reward_config

#endif
