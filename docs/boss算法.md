### Boss 战滚动视野伤害容量评分算法说明

### 一、问题背景

Boss 战采用顺序揭示规则。开始战斗时，玩家只能知道当前 Boss 的血量；击败当前 Boss 后，下一只 Boss 的血量才会被揭示。如果一次尝试失败并复活，已经揭示过的 Boss 血量会继续保持已知，之后可以基于这些已知血量重新规划。

在这种规则下，当前 Boss 的打法不能只考虑“当前阶段最快击杀”，也不能只考虑“打完当前 Boss 后技能状态最好”。真正需要处理的是下面这个 trade-off：

```text
当前 Boss 少用回合
vs
当前 Boss 打完后保留更强的后续输出能力
```

如果当前 Boss 打得太慢，后续 Boss 可能没有足够回合数完成；如果当前 Boss 打得太快，可能把关键技能打入冷却，导致后续输出不足。因此，算法需要用一个统一指标同时评价：

```text
1. 当前方案消耗了多少回合；
2. 当前方案结束后的技能冷却状态；
3. 后续 Boss 数量；
4. 剩余回合内的潜在输出能力。
```

### 二、旧算法的问题

旧的局部比较策略可能直接按照如下优先级选择方案：

```text
readyDamageAfter > cooldownCostAfter > turns > sequence
```

这种策略容易产生错误倾向：为了让战后技能状态更好，算法可能让当前 Boss 多打一回合。但是在总回合限制 `minRounds` 较紧时，多消耗的这一回合可能直接导致后续 Boss 无法按时击杀。

例如：

```json
{
  "B": [11, 7, 18],
  "PlayerSkills": [[10, 1], [11, 4], [11, 5], [3, 0], [4, 4]],
  "minRounds": 4
}
```

如果第一只 Boss 选择：

```text
[0, 3]
```

虽然两回合后技能冷却状态更好，但只给剩余两只 Boss 留下 2 回合，后续 18 血 Boss 很可能无法处理。更合理的方案是第一只 Boss 用 1 回合击杀，例如：

```text
[1]
```

这样后续还剩 3 回合，可以形成类似：

```text
[1, 0, 2, 0]
```

的 4 回合通关方案。

### 三、算法名称

本文采用的轻量化算法称为：

```text
滚动视野伤害容量评分算法
Rolling Horizon Damage-Capacity Heuristic
```

该算法不再使用完整的未知后缀鲁棒 DP，也不再枚举未知 Boss 的所有可能血量。它通过估计“从当前冷却状态出发，未来若干回合最多能打出多少累计伤害”，来评价当前方案的后续潜力。

### 四、设计目标

算法需要满足以下目标：

1. 不偷看未揭示 Boss 血量；
2. 保留已经揭示的 Boss 血量信息；
3. 失败复活后可以利用已知 Boss 血量重新规划；
4. 不退化成单纯 `turns` 贪心；
5. 不退化成单纯 `readyDamageAfter` 贪心；
6. 显著减少完整鲁棒 DP 的计算量；
7. 保持输出结果可解释，便于调试和写实验报告。

### 五、知识状态定义

设 Boss 总数量为：

$$
N
$$

当前已经揭示的 Boss 数量为：

$$
K
$$

当前已知 Boss 血量前缀为：

$$
KnownHPs=[B_0,B_1,\dots,B_{K-1}]
$$

当前决策时，算法只能使用：

```text
KnownHPs
totalBossCount
currentBossIndex
usedTurns
currentCooldown
PlayerSkills
minRounds
```

算法不能使用：

$$
B_K,B_{K+1},\dots,B_{N-1}
$$

这些 Boss 的血量只有在真正到达对应 Boss 并揭示后，才能加入 `KnownHPs`。

### 六、技能与冷却规则

每个技能 $i$ 有两个属性：

$$
d_i=\text{damage}_i
$$

$$
cd_i=\text{cooldown}_i
$$

当前技能冷却状态记为：

$$
\mathbf{c}=(c_0,c_1,\dots,c_{m-1})
$$

其中：

```text
c_i = 0 表示技能 i 当前可用；
c_i > 0 表示技能 i 还需要 c_i 回合冷却。
```

每回合的动作集合为：

$$
A(\mathbf{c})=
\begin{cases}
{i\mid c_i=0}, & \text{如果存在可用技能}\
{-1}, & \text{如果没有任何技能可用}
\end{cases}
$$

其中：

```text
i >= 0 表示使用技能 i；
-1 表示等待一回合。
```

注意：只有在没有任何技能可用时，才允许等待。只要存在可用技能，就必须选择一个技能使用。

技能使用后的状态转移保持原代码规则：

```text
1. 先造成伤害；
2. 所有已有冷却减 1；
3. 再把当前使用的技能设置为自身 cooldown。
```

如果使用技能 $i$，Boss 血量变为：

$$
hp'=hp-d_i
$$

冷却状态变为：

$$
T_i(\mathbf{c})_j=
\begin{cases}
cd_i, & j=i\
\max(c_j-1,0), & j\ne i
\end{cases}
$$

如果等待一回合，则：

$$
hp'=hp
$$

$$
T_{-1}(\mathbf{c})_j=\max(c_j-1,0)
$$

### 七、当前 Boss 候选方案生成

对当前 Boss，算法仍然需要枚举若干个合法击杀候选。

一个候选方案 $p$ 包含：

```text
sequence：技能序列；
turns：击杀当前 Boss 所用回合数；
cooldownAfter：击杀当前 Boss 后的技能冷却状态；
cooldownCostAfter：战后冷却代价；
readyDamageAfter：战后立即可用技能总伤害。
```

候选方案必须满足：

```text
1. Boss 在最后一回合首次死亡；
2. Boss 死亡前不能提前结束；
3. Boss 死亡后不能继续等待刷冷却；
4. 有技能可用时不能主动等待。
```

为了控制计算量，算法不枚举过长的当前 Boss 打法。推荐只枚举：

$$
turns \le minTurns + localSlack
$$

其中 `minTurns` 是当前 Boss 的最短击杀回合数，`localSlack` 根据当前局面的回合冗余量决定：

```cpp
int remainingBosses = totalBossCount - currentBossIndex - 1;
int minFutureTurns = remainingBosses;
int spareAfterMinPlan = minRounds - usedTurns - minTurns - minFutureTurns;

int localSlack = 0;
if (spareAfterMinPlan >= 2) localSlack = 1;
if (spareAfterMinPlan >= 5) localSlack = 2;
```

含义是：

```text
局面越紧，越不允许当前 Boss 为了冷却多打一回合；
局面越宽松，才允许多考虑 1 到 2 回合的候选方案。
```

### 八、伤害容量曲线

滚动视野评分的核心是伤害容量曲线：

$$
D_{\mathbf{c}}(k)
$$

含义是：

```text
从冷却状态 c 出发，未来 k 回合内最多能打出的累计伤害。
```

例如：

```text
D_c(1)：未来 1 回合最大累计伤害；
D_c(2)：未来 2 回合最大累计伤害；
D_c(3)：未来 3 回合最大累计伤害。
```

这个函数只在冷却状态空间上做 DP，不涉及 Boss 血量枚举。

DP 状态为：

```text
cooldown -> 当前最大累计伤害
```

初始状态：

```text
states[startCooldown] = 0
bestDamage[0] = 0
```

每一回合转移：

```text
枚举当前所有可达 cooldown 状态；
枚举该 cooldown 下的合法动作；
执行动作，得到 nextCooldown；
更新累计伤害；
同一个 nextCooldown 只保留最大累计伤害。
```

伪代码如下：

```cpp
states[startCooldown] = 0;
bestDamage[0] = 0;

for turn in 1..maxTurns:
    nextStates.clear();

    for each (cooldown, totalDamage) in states:
        actions = availableBossActions(cooldown);

        for action in actions:
            nextCooldown = applyBossAction(cooldown, action);
            nextDamage = totalDamage + damage(action);

            nextStates[nextCooldown] =
                max(nextStates[nextCooldown], nextDamage);

            bestDamage[turn] = max(bestDamage[turn], nextDamage);

    bestDamage[turn] = max(bestDamage[turn], bestDamage[turn - 1]);
    states = nextStates;
```

最终返回：

```text
bestDamage[0..maxTurns]
```

其中：

```text
bestDamage[k] = 从当前 cooldown 出发，k 回合内最多能打出的累计伤害。
```

### 九、LightScore 评分定义

对当前 Boss 的某个候选方案 $p$，定义：

$$
t_p=\text{方案 }p\text{ 击杀当前 Boss 所用回合数}
$$

$$
\mathbf{c}_p=\text{方案 }p\text{ 击杀当前 Boss 后的冷却状态}
$$

当前 attempt 已经使用回合数为：

$$
T_{\text{used}}
$$

总回合限制为：

$$
T_{\max}
$$

则候选方案执行后的剩余回合为：

$$
R_p=T_{\max}-T_{\text{used}}-t_p
$$

设后续未知 Boss 数量为：

$$
U
$$

从 $\mathbf{c}_p$ 出发，计算伤害容量曲线：

$$
D_{\mathbf{c}_p}(k)
$$

然后定义候选方案的轻量后缀评分为：

$$
LightScore(p)=
\min_{s=1}^{U}
\left\lfloor
\frac{
D_{\mathbf{c}_p}\left(\left\lceil \frac{sR_p}{U} \right\rceil\right)
}{s}
\right\rfloor
$$

其中 $s$ 表示前 $s$ 个未知 Boss。

这个公式的含义是：

```text
把剩余 R_p 回合按进度分配给 U 个未知 Boss；
检查前 1 个、前 2 个、……前 U 个未知 Boss 的平均伤害容量；
取所有阶段里最弱的平均容量，作为该候选方案的后续承受能力。
```

### 十、为什么使用阶段最小值

如果只看总伤害，会忽略输出曲线的前期短板。

例如后续有 3 个未知 Boss，剩余 6 回合。如果 6 回合总伤害很高，但前 2 回合输出很低，那么第一只未知 Boss 可能无法及时击杀，后续总伤害再高也没有意义。

因此算法检查多个阶段：

```text
前 1 个未知 Boss 的阶段容量；
前 2 个未知 Boss 的阶段容量；
...
前 U 个未知 Boss 的阶段容量。
```

然后取最小值：

```text
LightScore = 所有阶段容量中的短板。
```

这样可以避免只看最终总伤害导致的过度乐观。

### 十一、已知后缀与未知后缀

如果失败复活后，多个 Boss 血量已经被揭示，那么后续不应该全部当作未知处理。

因此评分分成两段：

```text
1. 已知后缀：用 knownHPs 中已经揭示的真实血量做轻量滚动模拟；
2. 未知后缀：用 LightScore 伤害容量估计。
```

具体来说，当前 Boss 已经由候选方案 $p$ 解决后，从 `currentBossIndex + 1` 开始检查：

```text
如果该 Boss 已经在 knownHPs 中：
    用真实血量求一个较快击杀方案；
    更新 remainingTurns 和 cooldown；
    继续处理下一个已知 Boss。

如果后续 Boss 未揭示：
    不读取真实血量；
    使用 suffixCapacityThreshold 计算未知后缀的 LightScore。
```

这样既能利用复活后已经获得的信息，又不会偷看未来未揭示 Boss。

### 十二、未知后缀评分函数

未知后缀评分函数可以定义为：

```cpp
int suffixCapacityThreshold(
    const std::vector<int>& cooldown,
    int remainingTurns,
    int unknownCount,
    const std::vector<Skill>& skills
);
```

边界条件：

```cpp
if (unknownCount == 0) return INF;
if (remainingTurns < unknownCount) return -1;
if (remainingTurns <= 0) return -1;
```

核心计算：

```cpp
std::vector<int> damageProfile =
    maxDamageProfileFromCooldown(cooldown, remainingTurns, skills);

int threshold = INF;

for (int s = 1; s <= unknownCount; ++s) {
    int deadline = (s * remainingTurns + unknownCount - 1) / unknownCount;
    int avgCapacity = damageProfile[deadline] / s;
    threshold = std::min(threshold, avgCapacity);
}

return threshold;
```

其中：

```text
deadline = ceil(s * remainingTurns / unknownCount)
```

表示前 $s$ 个未知 Boss 大致应该在多少回合内处理完。

### 十三、当前 Boss 候选选择规则

对所有当前 Boss 候选 $p$，先计算：

```text
LightScore(p)
```

最终选择：

$$
p^*=\arg\max_p LightScore(p)
$$

如果多个候选的 `LightScore` 相同，再使用稳定 tie-break：

```text
1. turns 更少；
2. cooldownCostAfter 更小；
3. readyDamageAfter 更大；
4. sequence 字典序更小。
```

对应比较逻辑：

```cpp
if (candidate.lightScore != best.lightScore) {
    return candidate.lightScore > best.lightScore;
}

if (candidate.turns != best.turns) {
    return candidate.turns < best.turns;
}

if (candidate.cooldownCostAfter != best.cooldownCostAfter) {
    return candidate.cooldownCostAfter < best.cooldownCostAfter;
}

if (candidate.readyDamageAfter != best.readyDamageAfter) {
    return candidate.readyDamageAfter > best.readyDamageAfter;
}

return candidate.sequence < best.sequence;
```

注意：`turns`、`cooldownCostAfter`、`readyDamageAfter` 不再作为主比较项。它们只在 `LightScore` 相同时用于稳定排序。

### 十四、算法流程

完整流程如下：

```text
输入：
    Boss 血量数组 B
    玩家技能 PlayerSkills
    总回合限制 minRounds
    当前已揭示 Boss 血量 knownHPs
    当前冷却状态 cooldown
    当前已使用回合 usedTurns

流程：
    1. 如果当前 Boss 未揭示，将其加入 knownHPs。
    2. 对当前 Boss 枚举少量击杀候选。
    3. 对每个候选方案：
        3.1 计算 turns；
        3.2 计算 cooldownAfter；
        3.3 计算 remainingTurnsAfter；
        3.4 先处理已知后缀 Boss；
        3.5 对未知后缀计算伤害容量 LightScore。
    4. 选择 LightScore 最大的候选。
    5. 执行该候选，更新 sequence、usedTurns、cooldown。
    6. 揭示下一只 Boss，继续规划。
    7. 如果失败复活，保留 knownHPs，重置 cooldown、usedTurns 和 sequence，从 Boss0 重新开始。
```

### 十五、示例分析

输入：

```json
{
  "B": [11, 7, 18],
  "PlayerSkills": [[10, 1], [11, 4], [11, 5], [3, 0], [4, 4]],
  "minRounds": 4
}
```

当前只知道 Boss0 血量为 11。

候选 A：

```text
sequence = [1]
turns = 1
cooldownAfter = [0, 4, 0, 0, 0]
```

此时：

```text
remainingTurnsAfter = 4 - 1 = 3
unknownCount = 2
```

从 `[0, 4, 0, 0, 0]` 出发计算伤害容量曲线，假设得到：

```text
D(1) = 11
D(2) = 21
D(3) = 32
```

则：

```text
s = 1:
deadline = ceil(1 * 3 / 2) = 2
capacity = floor(D(2) / 1) = 21

s = 2:
deadline = ceil(2 * 3 / 2) = 3
capacity = floor(D(3) / 2) = 16
```

所以：

```text
LightScore(A) = min(21, 16) = 16
```

候选 B：

```text
sequence = [0, 3]
turns = 2
cooldownAfter = [0, 0, 0, 0, 0]
```

此时：

```text
remainingTurnsAfter = 4 - 2 = 2
unknownCount = 2
```

从 `[0, 0, 0, 0, 0]` 出发计算伤害容量曲线，假设得到：

```text
D(1) = 11
D(2) = 21
```

则：

```text
s = 1:
deadline = ceil(1 * 2 / 2) = 1
capacity = floor(D(1) / 1) = 11

s = 2:
deadline = ceil(2 * 2 / 2) = 2
capacity = floor(D(2) / 2) = 10
```

所以：

```text
LightScore(B) = min(11, 10) = 10
```

比较：

```text
LightScore(A) = 16
LightScore(B) = 10
```

算法选择候选 A，即：

```text
[1]
```

这说明虽然候选 A 的战后冷却状态不如候选 B，但它多保留了 1 回合，使后续两只 Boss 的整体承受能力更强。

### 十六、与完整鲁棒 DP 的区别

完整未知后缀鲁棒 DP 的思想是：

```text
对未知 Boss 血量 h = 1..Hmax 逐个验证；
判断是否对所有 h <= H 都存在可行打法；
通过 F/G 递推得到严格鲁棒阈值。
```

其评分形式类似：

$$
Score(p)=F(b+1,R_p,\mathbf{c}_p)
$$

其中 $F$ 会继续调用未知后缀鲁棒函数 $G$。

滚动视野伤害容量评分算法不再枚举未知血量，也不再做严格的 $F/G$ 递推。它只估计：

```text
从候选战后 cooldown 出发，剩余回合内的最大累计伤害曲线。
```

因此两者关系为：

```text
完整鲁棒 DP：严格但重；
滚动视野伤害容量评分：轻量但近似。
```

### 十七、复杂度分析

设：

```text
R = 剩余回合数；
C = 可达冷却状态数；
A = 平均合法动作数；
Q = 当前 Boss 候选数量；
Hmax = 未知 Boss 血量上界；
U = 未知 Boss 数量；
TKill = 单 Boss 精确击杀终态枚举成本。
```

完整鲁棒 DP 的后缀评分复杂度大致包含：

$$
O(U \cdot C \cdot H_{\max} \cdot R^2 \cdot \log H_{\max} \cdot TKill)
$$

其中最重的部分是对未知血量区间：

```text
h = 1..Hmax
```

的枚举和验证。

滚动视野伤害容量评分算法的后缀评分主要是冷却状态 DP：

$$
O(Q_{\text{unique}} \cdot R \cdot C \cdot A)
$$

其中 $Q_{\text{unique}}$ 表示不同候选结束冷却状态的数量。

因此，新算法主要减少了以下计算：

```text
1. 不再枚举未知 Boss 血量 h = 1..Hmax；
2. 不再二分鲁棒血量阈值；
3. 不再递归计算 G(u,r,c)；
4. 不再为未知血量反复调用 Kill(h,c,t)。
```

需要注意的是，复杂度下降倍数不是固定常数。它取决于 `Hmax`、`R`、`U`、`C` 和候选数量。但可以确定的是，新算法去掉了对 `Hmax` 的显式依赖，因此在高血量或长回合样例中会明显更轻。

### 十八、调试输出字段

建议每个 phase 输出：

```json
{
  "bossIndex": 0,
  "revealedHp": 11,
  "knownBossHPsBeforeFight": [11, null, null],
  "knownCount": 1,
  "unknownBossCount": 2,
  "turnStart": 0,
  "turns": 1,
  "sequence": [1],
  "cooldownAfter": [0, 4, 0, 0, 0],
  "cooldownCostAfter": 220,
  "readyDamageAfter": 27,
  "remainingTurnsAfter": 3,
  "remainingUnknownBossCount": 2,
  "lightScore": 16,
  "selectionReason": "max light suffix damage capacity under remaining turns"
}
```

同时建议输出候选评分表：

```json
"candidateScores": [
  {
    "sequence": [1],
    "turns": 1,
    "cooldownAfter": [0, 4, 0, 0, 0],
    "remainingTurnsAfter": 3,
    "remainingUnknownBossCount": 2,
    "lightScore": 16
  },
  {
    "sequence": [0, 3],
    "turns": 2,
    "cooldownAfter": [0, 0, 0, 0, 0],
    "remainingTurnsAfter": 2,
    "remainingUnknownBossCount": 2,
    "lightScore": 10
  }
]
```

实际数值应由程序计算，不能硬编码。

### 十九、算法优点

该算法的主要优点是：

```text
1. 保留了 turns 和后续伤害能力之间的 trade-off；
2. 不依赖未知 Boss 真实血量；
3. 能利用复活后已经揭示的 Boss 血量；
4. 计算量明显低于完整未知后缀鲁棒 DP；
5. 输出字段容易解释；
6. 适合课程设计中的游戏 AI 策略实现。
```

### 二十、算法局限

该算法不是严格鲁棒最优算法。它存在以下局限：

```text
1. LightScore 是伤害容量近似，不保证对所有未知血量组合都可行；
2. 如果未来 Boss 血量分布极端不均匀，平均容量估计可能偏乐观；
3. 如果技能存在严重过量伤害浪费，累计伤害曲线可能高估实际击杀能力；
4. localSlack 过小可能漏掉某些需要多打一回合换取关键冷却状态的方案；
5. localSlack 过大又会增加当前 Boss 候选数量。
```

因此，该算法适合作为轻量实用策略，而不是严格证明最优的鲁棒规划算法。

### 二十一、总结

滚动视野伤害容量评分算法的核心思想是：

```text
不直接比较当前 Boss 的 turns 或 readyDamageAfter；
而是把当前方案消耗的回合数和战后 cooldown 状态统一换算成后续伤害容量。
```

最终选择公式为：

$$
p^*=\arg\max_p LightScore(p)
$$

其中：

$$
LightScore(p)=
\min_{s=1}^{U}
\left\lfloor
\frac{
D_{\mathbf{c}_p}\left(\left\lceil \frac{sR_p}{U} \right\rceil\right)
}{s}
\right\rfloor
$$

该算法用轻量的冷却状态 DP 替代完整未知血量鲁棒递推，在不偷看未来 Boss 血量的前提下，有效处理了“当前回合消耗”和“后续输出能力”之间的平衡问题。