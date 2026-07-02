# Boss 战未知后缀鲁棒价值算法说明

## 旧算法的问题

旧版 `betterBossPlan` 把 `readyDamageAfter` 和 `cooldownCostAfter` 放在 `turns` 前面做硬优先级比较。这样会出现一个错误倾向：当前 Boss 多打一回合来保留更好的战后冷却状态，但后续 Boss 的总回合预算被压缩，最终无法满足 `minRounds`。

典型样例：

```json
{
  "B": [11, 7, 18],
  "PlayerSkills": [[10, 1], [11, 4], [11, 5], [3, 0], [4, 4]],
  "minRounds": 4
}
```

旧策略可能为了保留可用伤害，在第一只 11 血 Boss 上选择 `[0, 3]` 两回合方案。这个方案本阶段看起来冷却更好，但只给后两只 Boss 留 2 回合，无法稳定处理最后的 18 血 Boss。

## 知识状态

Boss 血量按顺序揭示：

- 开始时只知道当前 Boss 的血量；
- 击败当前 Boss 后，才揭示下一只 Boss；
- 失败复活后，已经揭示过的 `knownHPs` 前缀保持已知；
- 未揭示的 Boss 血量不能进入评分函数。

规划器只允许读取：

```text
knownHPs 前缀
totalBossCount
当前 cooldown
剩余回合
技能列表
UnknownBossHpMax / Hmax 先验
```

不得读取 `knownHPs.size()` 之后的真实 `B[i]`。

## 冷却语义

使用技能 `i` 时：

```text
hp' = hp - damage[i]
cooldown'[j] =
  cooldown[i],        if j == i
  max(cooldown[j]-1), if j != i
```

如果没有任何技能可用，允许等待一回合：

```text
hp' = hp
cooldown'[j] = max(cooldown[j]-1)
```

Boss 一旦死亡，本阶段立即结束，不允许死亡后继续等待刷新冷却。

## Hmax

未知 Boss 使用固定先验上界：

```text
maxDamage = max(skill.damage)
base = UnknownBossHpMax if provided else 2 * maxDamage
Hmax = max(base, max(knownHPs))
```

`max(knownHPs)` 只使用已揭示前缀，不能扫描完整 `B`。

## Kill(h, c, t)

`Kill(h, c, t)` 表示从冷却状态 `c` 出发，恰好 `t` 回合首次击杀血量 `h` 的所有结束冷却状态。

约束：

- 第 `t` 回合后 `hp <= 0`；
- 前 `t-1` 回合后 `hp > 0`；
- 同一个 `cooldownAfter` 只保留字典序最小的技能序列。

## 未知后缀鲁棒值 G

`G(u, r, c)` 表示还有 `u` 个未知 Boss、剩余 `r` 回合、当前冷却为 `c` 时，最多能保证处理的未知 Boss 血量阈值。

边界：

```text
G(0, r, c) = Hmax
G(u, r, c) = 0, when u > 0 and r <= 0
```

递推：

```text
G(u,r,c) = max H in [0,Hmax]
such that for every h in [1,H],
there exists t in [1,r] and c' in Kill(h,c,t),
with G(u-1,r-t,c') >= H.
```

这个定义要求对所有 `h <= H` 都有可行打法，因此不会偷看未知 Boss 的真实血量。

## 已知前缀价值 F

`F(j, r, c)` 表示从已知 Boss 下标 `j` 开始，先打完已知前缀剩余 Boss，再面对未知后缀时的鲁棒血量阈值。

设：

```text
K = knownHPs.size()
N = totalBossCount
```

边界：

```text
F(K,r,c) = G(N-K,r,c)
```

递推：

```text
F(j,r,c) = max F(j+1,r-t,c')
where 1 <= t <= r
and c' in Kill(knownHPs[j], c, t).
```

如果没有可行击杀方案，返回 `-1`。

## 当前候选评分

当前 Boss 下标为 `b`，候选方案 `p` 使用 `t_p` 回合，结束冷却为 `c_p`。

```text
r_p = minRounds - usedTurns - t_p
Score(p) = F(b+1, r_p, c_p)
```

如果 `r_p < 0`，该方案不可行。

最终先比较 `robustScore = Score(p)`。只有 `robustScore` 相同，才按以下顺序稳定 tie-break：

1. `turns` 更小；
2. `cooldownCostAfter` 更小；
3. `readyDamageAfter` 更大；
4. `sequence` 字典序更小。

因此新算法既不是单纯 `turns` 优先，也不是 `readyDamageAfter` 优先。

## 复活后的已知血量持久化

如果一次 attempt 失败：

- `knownHPs` 不清空；
- `knownCount` 不回退；
- 技能冷却、回合数、执行序列重置；
- 下一次 attempt 从 Boss0 重新规划，但可以合法使用已经揭示过的 `knownHPs` 前缀。

这不是偷看，因为这些血量已经在之前 attempt 中被揭示。

## 错例修复

对于 `[11,7,18]`、`minRounds=4`：

- 第一只 Boss 只知道 `11`，后两只 Boss 是未知后缀；
- `[0,3]` 虽然战后冷却较好，但只剩 2 回合处理 2 个未知 Boss，鲁棒阈值低；
- `[1]` 或 `[2]` 一回合击杀第一只 Boss，保留 3 回合处理后缀，鲁棒阈值更高；
- 之后随着 `7` 和 `18` 逐步揭示，最终可以得到 `[1,0,2,0]` 或 `[2,0,1,0]` 这类 4 回合合法解。

## 剩余风险

`Hmax` 是未知 Boss 血量的公开先验，不等于真实未来血量。如果 `Hmax` 过低，策略会低估未知后缀风险；如果 `Hmax` 过高，策略会偏保守。可以在 JSON 中显式提供 `UnknownBossHpMax` 调整该先验。
