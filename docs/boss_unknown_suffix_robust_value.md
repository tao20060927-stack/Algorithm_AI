# Boss 战未知后缀鲁棒价值算法说明

## 旧算法的问题

旧版 `betterBossPlan` 把 `readyDamageAfter` 和 `cooldownCostAfter` 放在 `turns` 前面做硬优先级比较。这样会出现一个错误倾向：当前 Boss 多打一回合来保留更好的战后冷却状态，但后续 Boss 的总回合预算被压缩，最终无法满足 `minRounds`。

## 知识状态

Boss 血量按顺序揭示：

- 开始时只知道当前 Boss 的血量；
- 击败当前 Boss 后，才揭示下一只 Boss；
- 失败复活后，已经揭示过的 `knownHPs` 前缀保持已知；
- 未揭示的 Boss 血量不能进入评分函数。

规划器只允许读取 `knownHPs` 前缀、`totalBossCount`、当前 cooldown、剩余回合、技能列表和公开 Hmax 先验。不得读取 `knownHPs.size()` 之后的真实 `B[i]`。

## Hmax

未知 Boss 使用公开先验上界：

```text
if UnknownBossHpMax exists:
    publicBase = UnknownBossHpMax
else if minRounds exists:
    publicTurns = ceil(minRounds / totalBossCount) + 3
    publicBase = MaxDamage(publicTurns, all-cooldown-zero)
else:
    publicBase = MaxDamage(10, all-cooldown-zero)

Hmax = max(publicBase, max(knownHPs))
```

`MaxDamage(k, all-cooldown-zero)` 表示从全技能可用状态出发，最多 `k` 回合内按当前冷却规则能打出的最大总伤害。这个默认值只使用公开的技能、Boss 数量和回合限制，不读取未揭示 Boss 血量。

这个规则替代旧的 `2 * maxDamage`，因为长回合循环输出模型下，Boss 血量可能远大于两倍单次最大伤害。

## G/F 递推

`G(u,r,c)` 表示还有 `u` 个未知 Boss、剩余 `r` 回合、当前冷却为 `c` 时，最多能保证处理的未知 Boss 血量阈值。

```text
G(0, r, c) = Hmax
G(u, r, c) = 0, when u > 0 and r <= 0
```

```text
G(u,r,c) = max H in [0,Hmax]
such that for every h in [1,H],
there exists t in [1,r] and c' in Kill(h,c,t),
with G(u-1,r-t,c') >= H.
```

`F(j,r,c)` 表示从已知 Boss 下标 `j` 开始，先打完已知前缀剩余 Boss，再面对未知后缀时的鲁棒血量阈值。

```text
F(K,r,c) = G(N-K,r,c)
F(j,r,c) = max F(j+1,r-t,c')
where c' in Kill(knownHPs[j], c, t).
```

## 当前候选评分

当前 Boss 下标为 `b`，候选方案 `p` 使用 `t_p` 回合，结束冷却为 `c_p`：

```text
r_p = minRounds - usedTurns - t_p
Score(p) = F(b+1, r_p, c_p)
```

最终先比较 `robustScore = Score(p)`。只有 `robustScore` 相同，才按 `turns` 更小、`cooldownCostAfter` 更小、`readyDamageAfter` 更大、`sequence` 字典序更小的顺序稳定 tie-break。

## 复活后的已知血量持久化

如果一次 attempt 失败，`knownHPs` 不清空，`knownCount` 不回退；技能冷却、回合数、执行序列重置；下一次 attempt 从 Boss0 重新规划，但可以合法使用已经揭示过的 `knownHPs` 前缀。

## 高血量循环输出样例

对于：

```json
{
  "B": [135,156,107,102],
  "PlayerSkills": [[5,0],[22,4],[40,8]],
  "minRounds": 45
}
```

旧默认 `2 * maxDamage = 80`，第一次只知道 `[135]` 后得到 `Hmax = 135`，会低估未知后缀中可能出现的高血量 Boss。新默认使用：

```text
publicTurns = ceil(45 / 4) + 3 = 15
Hmax = max(MaxDamage(15, all-cooldown-zero), max(knownHPs))
```

因此 Hmax 来自技能循环输出能力，而不是未揭示的真实 `156`。

## 剩余风险

`Hmax` 是公开先验，不等于真实未来血量。如果 `Hmax` 过低，策略会低估未知后缀风险；如果 `Hmax` 过高，策略会偏保守。可以在 JSON 中显式提供 `UnknownBossHpMax` 调整该先验。
