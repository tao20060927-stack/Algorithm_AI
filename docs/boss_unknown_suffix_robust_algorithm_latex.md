### 当前 Boss 战主流程：滚动视野伤害容量评分算法

当前代码的主 Boss 策略已经从完整未知后缀鲁棒 DP 切换为：

```text
rolling_horizon_damage_capacity_boss_planner
滚动视野伤害容量评分算法
```

这个主流程保留“Boss 血量按顺序揭示”的规则，但不再把 `Hmax`、`knownPrefixValue(F)`、`robustUnknownValue(G)` 作为当前候选的主评分路径。旧的 F/G 鲁棒递推可以作为历史设计参考保留，但不再参与 `runBossBattleJson()` 的主选择。

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

### 历史设计参考：Boss 战未知后缀鲁棒价值算法说明

### 一、问题背景

当前 Boss 战采用“顺序揭示”规则：

1. 开始 Boss 战时，只知道当前 Boss 的血量。
2. 打完当前 Boss 后，才知道下一只 Boss 的血量。
3. 未揭示 Boss 的真实血量不能参与当前决策。
4. 如果失败后复活，已经揭示过的 Boss 血量保持已知。
5. 复活后从起点重新开始时，可以使用已经揭示过的 Boss 血量重新规划。

旧算法的问题是：每个 Boss 单独求当前阶段方案，然后用一个硬优先级比较不同方案：

$$
readyDamageAfter > cooldownCostAfter > turns > sequence
$$

这种比较会导致算法为了保留战后技能状态而多消耗当前回合。若后续 Boss 血量较高，前面多消耗的回合会让后续无法在 `minRounds` 内完成。

典型错误样例：

```json
{
  "B": [11, 7, 18],
  "PlayerSkills": [[10, 1], [11, 4], [11, 5], [3, 0], [4, 4]],
  "minRounds": 4
}
```

旧算法可能在第一只 Boss 处选择：

```text
[0, 3]
```

也就是两回合打死 11 血 Boss。这个方案战后技能状态较好，但只给剩余两只 Boss 留下 2 回合。如果后续出现 18 血 Boss，一回合最大伤害无法击杀，最终失败。

正确方案之一是：

```text
[1, 0, 2, 0]
```

总回合数为 4。

### 二、算法核心思想

新算法不再写死：

```text
turns 优先
```

也不写死：

```text
readyDamageAfter 优先
```

而是把“当前方案消耗的回合数”和“当前方案结束后的冷却状态”统一换算成一个确定量：

```text
该方案执行后，在剩余回合内，面对已知 Boss 后缀和未知 Boss 后缀时，最多能保证处理多高血量的未知 Boss。
```

这个量称为：

```text
未知后缀鲁棒血量阈值
```

如果一个方案多消耗了回合，它的剩余回合预算会变小。  
如果一个方案保留了更好的技能冷却状态，它的后续击杀能力会变强。  
两者最终都通过同一个递推函数计算，不再由人工优先级强行排序。

最终选择公式为：

$$
p^* =
\arg\max_{p \in P_b}
F\left(
b+1,\ 
T_{\max}-T_{\text{used}}-t_p,\ 
\mathbf{c}_p
\right)
$$

其中：

- $p$：当前 Boss 的某个击杀方案；
- $P_b$：当前 Boss 的所有候选击杀方案集合；
- $b$：当前 Boss 下标；
- $T_{\max}$：总回合限制，即 `minRounds`；
- $T_{\text{used}}$：当前 attempt 已经使用的回合数；
- $t_p$：方案 $p$ 击杀当前 Boss 使用的回合数；
- $\mathbf{c}_p$：方案 $p$ 击杀当前 Boss 后的技能冷却状态；
- $F$：已知 Boss 后缀与未知 Boss 后缀的统一价值函数。

### 三、知识状态定义

定义总 Boss 数量：

$$
N
$$

当前已经揭示的 Boss 数量：

$$
K
$$

当前已揭示 Boss 血量前缀：

$$
KnownHPs = [B_0, B_1, \dots, B_{K-1}]
$$

未知 Boss 数量：

$$
U=N-K
$$

决策函数只能使用：

```text
KnownHPs
N
U
当前 cooldown
当前 usedTurns
技能列表
固定未知血量上界 Hmax
```

决策函数不能使用：

$$
B_K, B_{K+1}, \dots, B_{N-1}
$$

这些血量只有在真实揭示后才能加入 `KnownHPs`。

### 四、技能与冷却定义

技能集合为：

$$
S=\{0,1,\dots,m-1\}
$$

每个技能 $i$ 有两个属性：

$$
d_i=\text{damage}_i
$$

$$
cd_i=\text{cooldown}_i
$$

当前冷却状态为：

$$
\mathbf{c}=(c_0,c_1,\dots,c_{m-1})
$$

其中：

```text
c_i = 0 表示技能 i 当前可用；
c_i > 0 表示技能 i 还需要 c_i 回合冷却。
```

### 五、动作集合

在冷却状态 $\mathbf{c}$ 下，合法动作集合为：

$$
A(\mathbf{c})=
\begin{cases}
\{i\mid c_i=0\}, & \text{如果存在可用技能}\\
\{-1\}, & \text{如果没有任何技能可用}
\end{cases}
$$

其中：

```text
i >= 0 表示使用技能 i；
-1 表示等待一回合。
```

### 六、技能使用后的状态转移

如果使用技能 $i\ge 0$，则 Boss 血量更新为：

$$
hp' = hp-d_i
$$

冷却状态更新为：

$$
T_i(\mathbf{c})_j=
\begin{cases}
cd_i, & j=i\\
\max(c_j-1,0), & j\ne i
\end{cases}
$$

如果等待一回合：

$$
hp'=hp
$$

$$
T_{-1}(\mathbf{c})_j=\max(c_j-1,0)
$$

这个定义与当前代码中的冷却语义一致：

```text
先造成伤害；
所有已有冷却减 1；
然后把当前使用技能设置为它自己的 cooldown。
```

### 七、单 Boss 击杀终态集合 Kill

定义：

$$
Kill(h,\mathbf{c},t)
$$

表示：

```text
从冷却状态 c 出发，面对血量 h 的 Boss，恰好用 t 回合击杀后，所有可能的结束冷却状态集合。
```

形式化定义：

$$
\mathbf{c}'\in Kill(h,\mathbf{c},t)
$$

当且仅当存在动作序列：

$$
(a_1,a_2,\dots,a_t)
$$

满足：

$$
a_s\in A(\mathbf{c}^{s-1})
$$

初始状态：

$$
hp^0=h
$$

$$
\mathbf{c}^0=\mathbf{c}
$$

每一回合：

$$
hp^s=
\begin{cases}
hp^{s-1}-d_{a_s}, & a_s\ge 0\\
hp^{s-1}, & a_s=-1
\end{cases}
$$

$$
\mathbf{c}^s=
\begin{cases}
T_{a_s}(\mathbf{c}^{s-1}), & a_s\ge 0\\
T_{-1}(\mathbf{c}^{s-1}), & a_s=-1
\end{cases}
$$

击杀条件：

$$
hp^{t}\le 0
$$

并且不能提前击杀：

$$
hp^s>0,\quad 0\le s<t
$$

最终：

$$
\mathbf{c}'=\mathbf{c}^t
$$

这条约束保证：

```text
Boss 一旦死亡，阶段立即结束；
不允许在 Boss 死后继续等待刷新冷却。
```

### 八、未知 Boss 血量上界 Hmax

未知 Boss 的真实血量不能被读取，因此需要一个固定先验上界：

$$
H_{\max}
$$

实现规则：

```text
如果 JSON 提供 UnknownBossHpMax，则使用它；
否则使用 2 * maxSkillDamage；
最终 Hmax 不得小于已揭示 Boss 的最大血量。
```

公式：

$$
D_{\max}=\max_i d_i
$$

$$
H_{\text{base}}=
\begin{cases}
UnknownBossHpMax, & \text{如果输入提供该字段}\\
2D_{\max}, & \text{否则}
\end{cases}
$$

$$
H_{\max}=\max(H_{\text{base}}, \max(KnownHPs))
$$

注意：

```text
max(KnownHPs) 只允许使用已揭示 Boss 血量；
不能使用完整 B 数组中的未揭示部分。
```

### 九、未知后缀鲁棒值 G

定义：

$$
G(u,r,\mathbf{c})
$$

含义：

```text
从冷却状态 c 出发，剩余 r 回合，需要面对 u 个未知 Boss。
G 返回一个整数 H，表示：
无论之后 u 个 Boss 的血量如何，只要每个 Boss 血量都 <= H，
算法都能保证在 r 回合内全部打完。
```

边界条件：

$$
G(0,r,\mathbf{c})=H_{\max},\quad r\ge 0
$$

$$
G(u,r,\mathbf{c})=0,\quad u>0,\ r\le 0
$$

递推公式：

$$
G(u,r,\mathbf{c})
=
\max
\left\{
H\in[0,H_{\max}]
\mid
\forall h\in[1,H],\ 
\exists t\in[1,r],\
\exists \mathbf{c}'\in Kill(h,\mathbf{c},t),
\ G(u-1,r-t,\mathbf{c}')\ge H
\right\}
$$

解释：

如果算法声称可以保证处理血量不超过 $H$ 的未知 Boss 后缀，那么对于任意可能出现的下一只 Boss 血量 $h\le H$，都必须存在一种打法：

1. 在 $t$ 回合内击杀该 Boss；
2. 打完后得到冷却状态 $\mathbf{c}'$；
3. 剩余 $u-1$ 个未知 Boss 仍然能保证处理血量不超过 $H$ 的后缀。

这个函数完全不读取未来真实 Boss 血量。

### 十、已知前缀价值 F

当前已揭示 Boss 前缀为：

$$
B_0,B_1,\dots,B_{K-1}
$$

定义：

$$
F(j,r,\mathbf{c})
$$

含义：

```text
从已知 Boss j 开始，当前冷却为 c，剩余 r 回合。
先必须打完已知 Boss j 到 K-1，
然后还要面对 N-K 个未知 Boss。
F 返回最终未知后缀的鲁棒血量阈值。
```

边界：

$$
F(K,r,\mathbf{c})=G(N-K,r,\mathbf{c})
$$

递推：

$$
F(j,r,\mathbf{c})
=
\max_{t,\mathbf{c}'}
F(j+1,r-t,\mathbf{c}')
$$

约束：

$$
1\le t\le r
$$

$$
\mathbf{c}'\in Kill(B_j,\mathbf{c},t)
$$

如果不存在任何满足约束的 $t,\mathbf{c}'$，则：

$$
F(j,r,\mathbf{c})=-1
$$

### 十一、当前 Boss 候选方案评分

当前正在打第 $b$ 个 Boss。

当前 Boss 的某个候选击杀方案 $p$ 有：

$$
t_p=\text{方案 }p\text{ 击杀当前 Boss 所用回合数}
$$

$$
\mathbf{c}_p=\text{方案 }p\text{ 击杀当前 Boss 后的冷却状态}
$$

当前已经使用回合数为：

$$
T_{\text{used}}
$$

总回合限制为：

$$
T_{\max}
$$

方案执行后的剩余回合为：

$$
r_p=T_{\max}-T_{\text{used}}-t_p
$$

最终评分为：

$$
Score(p)=F(b+1,r_p,\mathbf{c}_p)
$$

如果：

$$
r_p<0
$$

则：

$$
Score(p)=-1
$$

最终选择：

$$
p^*
=
\arg\max_{p\in P_b} Score(p)
$$

其中 $P_b$ 是当前 Boss 的所有候选击杀方案集合。

### 十二、Tie-break 规则

只有当多个方案的 $Score(p)$ 完全相同时，才使用 tie-break。

排序键为：

$$
\left(
Score(p),
-t_p,
-Cost(\mathbf{c}_p),
Ready(\mathbf{c}_p),
-\text{LexRank}(seq_p)
\right)
$$

其中：

$$
Cost(\mathbf{c})
=
\sum_i c_i\cdot d_i\cdot(cd_i+1)
$$

$$
Ready(\mathbf{c})
=
\sum_{i:c_i=0}d_i
$$

具体顺序：

1. $Score(p)$ 更大；
2. $t_p$ 更小；
3. $Cost(\mathbf{c}_p)$ 更小；
4. $Ready(\mathbf{c}_p)$ 更大；
5. 技能序列字典序更小。

注意：

```text
turns 和 readyDamageAfter 不再跨 Score 做主比较；
它们只在 robustScore 相同时用于稳定选择。
```

### 十三、为什么这个算法不是 turns 贪心

旧的 turns 优先策略会直接比较：

$$
t_p
$$

并选择当前 Boss 击杀回合数最少的方案。

本算法不直接最大化或最小化 $t_p$。  
它把 $t_p$ 放入剩余回合：

$$
r_p=T_{\max}-T_{\text{used}}-t_p
$$

再通过：

$$
Score(p)=F(b+1,r_p,\mathbf{c}_p)
$$

计算后续承受能力。

如果某个方案多花 1 回合，但它留下的冷却状态显著提升后续鲁棒阈值，那么它仍然可能胜出。

### 十四、为什么这个算法不是 readyDamage 贪心

旧的 readyDamage 优先策略会直接比较：

$$
Ready(\mathbf{c}_p)
$$

并选择战后立即可用伤害更高的方案。

本算法不直接最大化 $Ready(\mathbf{c}_p)$。  
它把完整冷却向量 $\mathbf{c}_p$ 放入：

$$
F(b+1,r_p,\mathbf{c}_p)
$$

由已知后缀和未知后缀的可击杀能力递推决定该状态是否真的有价值。

如果某个方案 readyDamage 很高，但它消耗了太多回合，导致未知后缀鲁棒值下降，那么它会被拒绝。

### 十五、为什么不偷看未来 Boss

算法只允许评分函数访问：

```text
knownHPs
totalBossCount
unknownBossCount
skills
cooldown
remainingTurns
Hmax
```

其中：

```text
knownHPs = 已揭示 Boss 血量前缀
```

未揭示血量：

$$
B_K, B_{K+1}, \dots, B_{N-1}
$$

不能进入：

```text
G
F
Score
candidate comparison
```

外层模拟器可以持有完整 `B` 数组，但只能在到达或揭示新 Boss 时，把对应血量追加到 `knownHPs` 中。

### 十六、复活后的已知血量持久化

如果一次 attempt 失败：

```text
knownHPs 不清空；
knownCount 不回退；
cooldown 重置；
usedTurns 重置；
sequence 重置；
从起点 S 重新规划。
```

因此，如果第一次尝试已经揭示：

$$
KnownHPs=[11,7,18]
$$

那么复活后的下一次尝试可以合法使用这三个血量做已知前缀规划。

这不是偷看，因为这些血量已经在之前 attempt 中被揭示过。

### 十七、错例分析

输入：

```json
{
  "B": [11, 7, 18],
  "PlayerSkills": [[10, 1], [11, 4], [11, 5], [3, 0], [4, 4]],
  "minRounds": 4
}
```

技能最大伤害为：

$$
D_{\max}=11
$$

如果未提供 `UnknownBossHpMax`，则：

$$
H_{\max}=2D_{\max}=22
$$

第一次只知道：

$$
KnownHPs=[11]
$$

未知 Boss 数量：

$$
U=2
$$

Boss0 的两个典型方案：

方案 A：

```text
[1]
```

$$
t_A=1
$$

$$
r_A=4-0-1=3
$$

$$
Score(A)=G(2,3,\mathbf{c}_A)
$$

方案 B：

```text
[0,3]
```

$$
t_B=2
$$

$$
r_B=4-0-2=2
$$

$$
Score(B)=G(2,2,\mathbf{c}_B)
$$

比较的是：

$$
G(2,3,\mathbf{c}_A)
\quad\text{和}\quad
G(2,2,\mathbf{c}_B)
$$

方案 B 虽然冷却状态更好，但只给两个未知 Boss 留 2 回合。  
这意味着未来每只 Boss 平均只能用 1 回合处理。  
只要未来出现一只血量超过单回合最大伤害的 Boss，风险就很高。

方案 A 虽然让一个 11 伤害技能进入冷却，但它给两个未知 Boss 留 3 回合，允许其中一只 Boss 使用 2 回合处理。

因此该算法会倾向选择 1 回合击杀 Boss0 的方案，例如：

```text
[1]
```

之后可得到合法总序列：

```text
[1,0,2,0]
```

### 十八、调试字段

每个 phase 建议输出：

```json
{
  "bossIndex": 0,
  "revealedHp": 11,
  "knownBossHPsBeforeFight": [11, null, null],
  "knownCount": 1,
  "unknownBossCount": 2,
  "hMax": 22,
  "turnStart": 0,
  "turns": 1,
  "sequence": [1],
  "cooldownAfter": [0,4,0,0,0],
  "cooldownCostAfter": 220,
  "readyDamageAfter": 27,
  "robustScore": 11,
  "selectionReason": "max robust unknown suffix value"
}
```

同时建议输出候选方案表：

```json
"candidateScores": [
  {
    "sequence": [1],
    "turns": 1,
    "cooldownAfter": [0,4,0,0,0],
    "remainingTurnsAfter": 3,
    "robustScore": 11
  },
  {
    "sequence": [0,3],
    "turns": 2,
    "cooldownAfter": [0,0,0,0,0],
    "remainingTurnsAfter": 2,
    "robustScore": 7
  }
]
```

实际数值由程序计算，不应硬编码。

### 十九、测试要求

#### 测试一：错例修复

输入：

```json
{
  "B": [11, 7, 18],
  "PlayerSkills": [[10, 1], [11, 4], [11, 5], [3, 0], [4, 4]],
  "minRounds": 4
}
```

期望：

```text
ok = true
turns = 4
withinMinRounds = true
sequence 为合法 4 回合方案，例如 [1,0,2,0] 或 [2,0,1,0]
```

#### 测试二：未揭示血量不能影响当前决策

构造两个输入：

```json
{
  "B": [11, 7, 18],
  "PlayerSkills": [[10, 1], [11, 4], [11, 5], [3, 0], [4, 4]],
  "minRounds": 4
}
```

```json
{
  "B": [11, 99, 99],
  "PlayerSkills": [[10, 1], [11, 4], [11, 5], [3, 0], [4, 4]],
  "minRounds": 4
}
```

在 `knownCount = 1` 时，Boss0 的候选评分和选择应完全一致。  
如果不一致，说明评分函数偷看了未揭示 Boss 血量。

#### 测试三：复活后利用已揭示血量

当 `knownHPs` 已经是：

```text
[11,7,18]
```

并从起点重新开始时，算法应使用这个已知前缀整体规划，找到 4 回合方案。

#### 测试四：边界条件

验证：

```text
G(0,r,c) = Hmax
G(u,0,c) = 0, u > 0
G(u,r,c) <= Hmax
remainingTurns 增大时，G 不应下降
unknownBossCount 增大时，G 不应上升，除非 remainingTurns 也增大
```

### 二十、剩余风险

该算法仍然依赖未知 Boss 血量上界 $H_{\max}$。

如果 $H_{\max}$ 过低，算法会低估未来风险。  
如果 $H_{\max}$ 过高，算法会过度保守。

因此建议默认：

$$
H_{\max}=2D_{\max}
$$

并允许 JSON 显式提供：

```json
{
  "UnknownBossHpMax": 22
}
```

这不是偷看未来 Boss，而是一个公开先验参数。

### 二十一、总结

该算法的核心是：

```text
不再把当前 Boss 的候选方案压成 turns 或 readyDamage 的硬优先级；
而是计算每个候选方案执行后，对已知后缀和未知后缀的鲁棒承受能力。
```

最终公式：

$$
p^*
=
\arg\max_{p\in P_b}
F\left(
b+1,\ 
T_{\max}-T_{\text{used}}-t_p,\ 
\mathbf{c}_p
\right)
$$

其中：

$$
F(K,r,\mathbf{c})=G(N-K,r,\mathbf{c})
$$

$$
F(j,r,\mathbf{c})
=
\max_{\mathbf{c}'\in Kill(B_j,\mathbf{c},t)}
F(j+1,r-t,\mathbf{c}')
$$

$$
G(u,r,\mathbf{c})
=
\max
\left\{
H
\mid
\forall h\le H,\
\exists t,\mathbf{c}'\in Kill(h,\mathbf{c},t),\
G(u-1,r-t,\mathbf{c}')\ge H
\right\}
$$

这套公式同时满足：

1. 不偷看未揭示 Boss 血量；
2. 能利用已经揭示的 Boss 血量；
3. 能在复活后用已知前缀重新规划；
4. 不写死 turns 和 readyDamage 的优先级；
5. 能修复 `[11,7,18], minRounds=4` 这类错例。
