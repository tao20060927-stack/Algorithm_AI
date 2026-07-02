### 当前 Boss 战主流程：滚动视野伤害容量评分算法

当前代码的主 Boss 策略已经从完整未知后缀鲁棒 DP 切换为：

```text
rolling_horizon_damage_capacity_boss_planner
滚动视野伤害容量评分算法
```

这个主流程保留“Boss 血量按顺序揭示”的规则，但不再把 `Hmax`、`knownPrefixValue(F)`、`robustUnknownValue(G)` 作为当前候选的主评分路径。旧的 F/G 鲁棒递推代码已经删除，不再作为可选实现保留。

当前评分逻辑：

1. 当前 Boss 只使用已揭示的 `knownHPs[currentBossIndex]`。
2. 先求当前 Boss 的最短击杀回合 `minTurns`。
3. 根据剩余回合冗余量只枚举 `minTurns + localSlack` 内的少量候选。
4. 对每个候选，计算执行后的剩余回合、技能冷却和未知 Boss 数量。
5. 已揭示后缀 Boss 用真实已知血量轻量推进。
6. 未揭示后缀 Boss 不枚举血量，只用未来伤害容量曲线估计可承受阈值。

轻量未知后缀评分为：

```text
D(k) = maxDamageProfileFromCooldown(cooldown, remainingTurns)[k]

lightScore =
min over s = 1..unknownCount:
    floor(D(ceil(s * remainingTurns / unknownCount)) / s)
```

其中 `D(k)` 表示从当前冷却状态出发，未来 `k` 回合内最多能造成的累计伤害。该公式只依赖当前冷却、剩余回合、未知 Boss 数量和公开技能列表，不读取未揭示的 `B[i]`。

当前候选比较顺序：

```text
lightScore 更大
turns 更少
cooldownCostAfter 更小
readyDamageAfter 更大
sequence 字典序更小
```

输出 JSON 中的算法名应为：

```json
"algorithm": "rolling_horizon_damage_capacity_boss_planner"
```

`candidateScores` 中应包含：

```json
"lightScore": 0
```

---

### 旧重型版本状态

旧的 `Hmax + knownPrefixValue(F) + robustUnknownValue(G)` 完整未知后缀鲁棒 DP 已从当前代码中删除。

保留的主流程只使用：

```text
solveCurrentBossByLightCapacity
maxDamageProfileFromCooldown
suffixCapacityThreshold
evaluateSuffixLightCapacity
```

这样 Boss 战评分不再枚举未知 Boss 的候选血量，也不会因为 `UnknownBossHpMax / Hmax` 变大导致长时间卡顿。
