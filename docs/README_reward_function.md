# Realtime Greedy Reward Function 说明

本文档解释 `RealtimeGreedyStrategy.cpp` 和 `Reward.cpp` 中实时贪心 AI 实际使用的路径价值函数（reward function）。

## 核心公式

当前代码实现的是一个 **加性 surrogate**，不是 ratio：

$$
\boxed{
\begin{aligned}
Score(t) =\;
&\Delta R_{\text{real}}(path_t) \\
&+\; \omega_I \cdot \alpha_t^{\text{smooth}} \cdot I_{\text{proxy}}(t) \\
&+\; \beta \cdot V_{\text{tail}}^{\text{marg}}(t) \\
&-\; \eta_q \cdot q_{\text{eff}} \cdot \operatorname{len}(path_t) \\
&-\; \phi_{\text{margin}}\bigl(R + \Delta R_{\text{real}}(path_t)\bigr)
\end{aligned}
}
$$

对应的 C++ 代码在 `Reward.cpp:734-753`（`PathValueEvaluator::evaluate()`）：

```cpp
return delta
     + parameters_.omegaI * context.state.alphaSmooth * info
     + parameters_.beta * tail
     - parameters_.qEffLengthWeight * qEff * pathLength(path)
     - margin;
```

五项各自对应：**真实资源变化**、**信息价值**、**后续金币机会**、**路径长度代价**、**安全裕量惩罚**。

---

## 一、基本符号

设当前状态为：

$$
S = (p, R, L, M)
$$

- $p$：AI 当前局部坐标；
- $R$：当前资源值（累计金币 − 陷阱扣分）；
- $L$：当前已走步数；
- $M$：AI 的局部记忆地图 `localMap_`（只包含 3×3 视野逐步观察到的格子）。

对任意候选目标 $t$，设从 $p$ 到 $t$ 的路径为：

$$
path_t = (v_0, v_1, \dots, v_k), \quad v_0 = p,\; v_k = t
$$

路径步数为：

$$
\operatorname{len}(path_t) = k
$$

---

## 二、候选目标集合

$$
\mathcal{T} = \{ t \mid t \text{ 已观察、可通行、可达、未访问} \}
$$

对应代码 `MemoryGreedyAgent::candidateTargets()`：

- `isObserved(t) = true` —— 格子已在 3×3 视野中点亮；
- `isWalkableForPlanning(t) = true` —— $t$ 不是墙 `#`，不是 Boss 本体 `B`；
- `isVisited(t) = false` —— AI 没有实际踩过该格；
- `shortestPathOnKnownMap(p, t) ≠ empty` —— 在已知地图上可达。

已访问格子允许作为路径中转节点，但不能作为候选目标。

---

## 三、各项详解

### ① 路径真实资源变化 $\Delta R_{\text{real}}$

$$
\Delta R_{\text{real}}(path_t) = \sum_{i=1}^{k} r(v_i)
$$

从 $i=1$ 开始（跳过 $v_0$，即当前位置），逐个格子结算：

$$
r(v) = \begin{cases}
+50, & v \text{ 是金币且未拾取} \\
-30, & v \text{ 是陷阱且未触发} \\
0,   & \text{其他（已拾取、已触发、普通格、出口 E）}
\end{cases}
$$

代码在 `Reward.cpp:576-586`（`pathResourceDelta()`）：

```cpp
for (size_t i = 1; i < path.size(); ++i) {
    if (tile == "G" && !isCollected(pos)) delta += 50;
    if (tile == "T" && !isTriggered(pos)) delta += -30;
}
```

路径前缀和走完路径后的预计资源：

$$
R' = R + \Delta R_{\text{real}}
$$

$$
R_i = R + \sum_{j=1}^{i} r(v_j),\quad i=1,\dots,len(path)
$$

**硬约束**：若存在任意路径前缀 $R_i < 0$，路径非法，直接返回 $-\infty$。因此 AI 不能选择“先踩陷阱导致资源为负，再吃金币补回”的路径；最终 $R' < 0$ 只是该前缀约束的特例。

出口 `E` 不作为普通 reward 候选参与上述评分；是否走向出口由 `shouldGoExit()` 的出口条件单独控制，避免出口同时被当作探索收益目标重复计分。

---

### ② 信息价值 $I_{\text{proxy}}$

$$
I_{\text{proxy}}(t) = \kappa_u \cdot \min(E(t), A_{\text{eff}}) \cdot \rho_{\text{area-value}}
$$

代码在 `Reward.cpp` 的 `informationProxy()`。

其中：

$$
\rho_{\text{area-value}} = \operatorname{clip}\left(\frac{\max(v_{\text{area}}, 0)}{50}, \rho_{\text{area-min}}, 1\right)
$$

$$
v_{\text{area}} = 50\rho_G - 30\rho_T
$$

其中 $E(t)$ 表示以候选目标格 $t$ 为 BFS 起点，把局部记忆中已经观察过的格子视为不可逾越障碍后，最多能够展开到的未观察格数量。15x15 掩码只有在 AI 能根据局部记忆完全确定掩码位置时才参与裁剪。

如果候选目标属于 Boss-gated 区域，并且从该目标出发的未知延伸可以触达当前可确认的迷宫边缘，则使用略高于普通上限的 Boss 边缘奖励：

$$
E(t)=A_{\text{boss-edge}}=15
$$

如果该未知延伸没有触达迷宫边缘，则不使用该奖励，仍按普通 $E(t)$ BFS 面积正常计算。

Boss-gated 区域只根据局部记忆地图计算：把所有已观察到的 Boss 本体临时当作墙，从当前位置 BFS 得到“删 Boss 后可达集合”；再从可从当前侧邻接到的 Boss 本体出发 BFS，凡是不在“删 Boss 后可达集合”中的已知可走格，都视为必须踏过 Boss 后才能继续到达的区域。触达迷宫边缘的判断同样只使用局部记忆和当前已启用/部分启用的掩码边缘，不读取隐藏地图。

触达边缘的 BFS 必须是有限判定：如果完整掩码未启用且没有任何出生边缘信息，则不能证明未知延伸触达边缘；如果只有部分边缘信息，最多只在 15x15 记忆容量内搜索，超过该容量仍未触边时按“不触边”处理，禁止在无限未知平面上继续展开。

$$
A_{\text{eff}} =
\begin{cases}
A_{\max}=12, & \text{出口未知或在已知地图上不可达} \\
1.5, & \text{出口已观察且在已知地图上可达}
\end{cases}
$$

- 掩码不是实时真实坐标，也不读取隐藏地图内容；它只是 AI 对“自己大概在 15x15 迷宫内哪里”的假定位置。
- 初始边缘假设由出生点 3x3 视野决定：若上方/下方/左侧/右侧三格被观察为迷宫外部，则把出生点放到对应边缘中央；若两个方向同时越界，则放到对应角；若没有观察到迷宫外部，则先按内部出生处理。
- 掩码不会在一开始无条件完整启用。上/下边缘出生时，只有局部记忆左右范围已经覆盖 15 格，才启用完整 15x15 掩码；左/右边缘出生时，只有局部记忆上下范围已经覆盖 15 格，才启用完整掩码；内部出生时，必须左右和上下范围都达到 15 格才启用完整掩码。
- 部分掩码：即使完整掩码未启用，若出生点在上/下边缘，AI 也已经知道 15x15 的上/下边缘是外围墙，$|C|$ BFS 不能把这两条边界计为未知；若出生点在左/右边缘，则同理知道左/右边缘是外围墙。
- 候选掩码排除：边缘出生时，只有对应方向的记忆跨度达到 14 后，才用已观察到的空格/墙去排除不可能的掩码位置；如果候选只剩一个，就提前视为完整掩码已确定。
- 完整掩码启用后，15x15 掩码外围一圈本身视为迷宫边界墙，BFS 不能进入这圈边界，也不能跨过边界，边界及边界外侧都不计入 $|C|$。
- 出口未知时，$A_{\text{eff}}=12$，开阔未知区域会得到更高面积贡献，用于鼓励继续找出口。
- 出口已知可达后，$A_{\text{eff}}=1.5$，开阔区域面积奖励被强裁剪，不再单纯鼓励探索开放区域，而是直接按当前观察到的金币/陷阱价值密度估算少量新增格子的价值。
- 如果能搜索到当前 $A_{\text{eff}}$ 的向上取整个未知格，就停止 BFS；最终面积贡献仍按 $\min(E(t), A_{\text{eff}})$ 计算。
- 如果未知区域被已观察格封闭，或在掩码已启用后被掩码边界封闭，且数量小于 $A_{\text{eff}}$，就令 $E(t)$ 等于实际搜索到的未知格数量。
- 已观察格作为障碍，避免被已观察区域包围的小未知区域借用远处未知区域面积。
- 对 Boss-gated 区域，只有未知延伸触达迷宫边缘时，$E(t)$ 才按 $A_{\text{boss-edge}}=15$ 处理，用于表达“Boss 后方仍可能通向迷宫边缘/出口方向”的推进价值；否则仍按普通未知区域面积计算。该判断只读取 `localMap_` 中已经观察到的 Boss、已知可走格和当前可确认的掩码边缘，不读取真实出口坐标。
- $\rho_{\text{area-value}}$ 越高，说明当前观察到的金币密度相对陷阱密度更值得继续探索。
- 乘以权重 $\kappa_u$ 后加入信息分。

该项以 $\omega_I \cdot \alpha_t^{\text{smooth}}$ 为系数进入总评分，信息价值先折算成"资源等价值"再与真实收益相加。

---

### ③ 边际尾部价值 $V_{\text{tail}}^{\text{marg}}$

$$
V_{\text{tail}}^{\text{marg}}(t) = \max_{g \in \mathcal{G} \setminus Coins(path_t)} \frac{50}{\operatorname{dist}(t, g) + 1}
$$

代码在 `Reward.cpp:628-644`（`futureGainMarginal()`）。

- $\mathcal{G}$：当前已知且未拾取的金币集合；
- $Coins(path_t)$：路径 $path_t$ 上已经会拾取的金币（排除，避免 double count）；
- $\operatorname{dist}(t, g)$：在已知地图上从 $t$ 到金币 $g$ 的最短路径步数。

如果没有可达的已知金币，该项为 0。乘以权重 $\beta$ 后进入总评分。

---

### ④ 路径长度代价 $\eta_q \cdot q_{\text{eff}} \cdot \operatorname{len}$

$$
q_{\text{eff}} = \max(q_{\text{ref}},\; q_{\min})
$$

$$
q_{\text{ref}} = \begin{cases}
\dfrac{R + \Delta R(path_{\text{exit}})}{L + \operatorname{len}(path_{\text{exit}}) + \varepsilon}, & \text{出口已观察且在已知地图上可达} \\[12pt]
\dfrac{R}{L + \varepsilon}, & \text{否则}
\end{cases}
$$

代码在 `Reward.cpp:656-665`（`computeQEff()`）。

**含义**：把全局最终评价指标 $R/L$ 转化成局部每一步的代价系数。$q_{\text{eff}}$ 是"当前可兑现的单位步数资源值"——路径每多走一步，就扣掉 $\eta_q \cdot q_{\text{eff}}$ 分。$\eta_q = 1.0$ 对应 `qEffLengthWeight`，默认不改变原有路径长度代价；$q_{\min} = 1.0$ 保证开局 $R=0$ 时步数仍有基础代价。

---

### ⑤ 安全裕量惩罚 $\phi_{\text{margin}}$

$$
\phi_{\text{margin}}(R') = \lambda_m \cdot \left( \frac{\max(0,\; m_{\text{safe}} - R')}{m_{\text{safe}}} \right)^2
$$

代码在 `Reward.cpp:676-681`（`marginPenalty()`）。

- $m_{\text{safe}} = 30$：刚好一枚陷阱的损失量；
- $\lambda_m = 8.0$：惩罚强度；
- 平方形式：远离安全线时几乎无影响，越靠近越陡。

若 $R' \ge 30$，该项为 0。这不是一刀切的固定惩罚，而是平滑 barrier。

---

## 四、动态探索权重 $\alpha_t$

代码在 `Reward.cpp` 的 `updateAlphaSmooth()`，所有参数定义在 `RewardConfig.h`。

### 4.1 观察统计

从已观察的局部记忆格中统计：

$$
N_{\text{obs}} = \text{已观察格子总数}, \quad
N_G = \text{已观察金币数}, \quad
N_T = \text{已观察陷阱数}
$$

### 4.2 密度估计（带平滑先验）

分母统一加 $\lambda = 10.0$ 避免早期样本少时密度估计抖动：

$$
\boxed{\hat\rho_G = \frac{N_G + \mathbf{1.0}}{N_{\text{obs}} + \mathbf{10.0}}} \qquad
\boxed{\hat\rho_T = \frac{N_T + \mathbf{1.0}}{N_{\text{obs}} + \mathbf{10.0}}}
$$

| 参数 | 值 | 含义 |
|---|---|---|
| $\lambda$ | **10.0** | 平滑强度伪计数，值越大早期密度越靠向先验 |
| $\lambda_G$ | **1.0** | 金币先验伪计数 |
| $\lambda_T$ | **1.0** | 陷阱先验伪计数 |

### 4.3 未知区期望净值 $v_{\text{unk}}$

$$
\boxed{v_{\text{unk}} = 50 \cdot \hat\rho_G - 30 \cdot \hat\rho_T}
$$

- 金币密度 $\hat\rho_G$ 每 $+0.01$，净值 $+0.5$；陷阱密度 $\hat\rho_T$ 每 $+0.01$，净值 $-0.3$
- Boss 不再作为风险密度进入 reward；Boss 相关逻辑只由战斗事件和通行规则处理。

### 4.4 未知区域占比

$$
\boxed{\rho_U = \frac{N_{\text{unknown}}}{225}}
$$

$N_{\text{unknown}}$ 由 `MapPoseEstimator::estimatedUnknownCount()` 给出；它只使用 AI 的局部记忆和条件启用的 15x15 掩码，不读取完整真实地图。

### 4.5 原始探索权重 $\alpha_t^{\text{raw}}$

代入全部参数值：

$$
\boxed{
\alpha_t^{\text{raw}} = \mathbf{4.0} \cdot
\frac{1 + \mathbf{1.0} \cdot \rho_U + \mathbf{1.0} \cdot \max(v_{\text{unk}},\; 0)\, /\, 50}
{1 + \mathbf{2.0} \cdot \hat\rho_T}
}
$$

| 参数 | 值 | 位置 | 含义 |
|---|---|---|---|
| $\alpha_0$ | **4.0** | 分子最外层 | 基准探索强度 |
| $w_U$ | **1.0** | 分子 $\rho_U$ 系数 | 未知占比每 $+0.1$，分子 $+0.1$ |
| $w_V$ | **1.0** | 分子 $\max(v_{\text{unk}},0)/50$ 系数 | 未知区期望净值每 $+5$，分子 $+0.1$ |
| $w_R$ | **2.0** | 分母整体系数 | 陷阱密度对探索的抑制强度 |

### 4.6 裁剪和平滑

$$
\boxed{\alpha_t = \operatorname{clip}(\alpha_t^{\text{raw}},\; \mathbf{1.0},\; \mathbf{8.0})}
$$

$$
\boxed{\alpha_t^{\text{smooth}} = \mathbf{0.8} \cdot \alpha_{t-1}^{\text{smooth}} + \mathbf{0.2} \cdot \alpha_t}
$$

| 参数 | 值 | 含义 |
|---|---|---|
| $\alpha_{\min}$ | **1.0** | 下限，即使风险极高也保留最低探索倾向 |
| $\alpha_{\max}$ | **8.0** | 上限，防止虚拟信息项压过真实金币（$8.0 \times 0.4 = 3.2$ 倍） |
| $\theta$ | **0.8** | EMA 平滑系数，新值权重仅 0.2，避免单步抖动 |

### 4.7 数值示例

| 场景 | $N_{\text{obs}}$ | $N_G$ | $N_T$ | $\rho_U$ | $\hat\rho_G$ | $\hat\rho_T$ | $v_{\text{unk}}$ | $\alpha_t^{\text{raw}}$ | $\alpha_t^{\text{smooth}}$ |
|---|---|---|---|---|---|---|---|---|---|
| 开局（9 格） | 9 | 0 | 0 | 0.96 | 0.05 | 0.05 | $+1.05$ | $4.0 \cdot \frac{1.96 + 0.021}{1 + 2.0(0.053)} = 7.2$ | 渐近 7.2 |
| 中期金多（80 格, 5金2陷阱） | 80 | 5 | 2 | 0.64 | 0.067 | 0.033 | $+2.33$ | $4.0 \cdot \frac{1.64 + 0.047}{1 + 2.0(0.033)} = 6.3$ | 渐近 6.3 |
| 后期陷阱多（150 格, 3金8陷阱） | 150 | 3 | 8 | 0.33 | 0.025 | 0.056 | $-0.44$ | $4.0 \cdot \frac{1.33}{1 + 2.0(0.056)} = 4.8$ | 渐近 4.8 |

**直觉**：未知区域越大 → $\rho_U$ 高 → 分子大 → $\alpha$ 高；已观察区陷阱越多 → $\hat\rho_T$ 高 → 分母大 → $\alpha$ 低。EMA 平滑确保变化缓慢。Boss 不再压低探索权重。

---

## 五、停止探索规则

当条件 1 成立，且没有触发条件 2 或条件 3 的禁止提前退出规则，并且条件 4 或条件 5 任一成立时，AI 不再继续探索，直接走向出口：

1. 出口已观察且在已知地图上可达；
2. 若当前 ratio $R/L = 0$，只要存在候选目标 $t$ 使得走完后资源 $R + \Delta R(path_t) \ge 0$，就禁止走向出口；只有所有候选目标都会让资源变负时，才允许继续判断退出；
3. 若当前 ratio $R/L < 1$ 且已拾取金币数 $< 3$，只要仍存在可继续探索的非出口目标，就禁止走向出口；只有当前已知可探索目标都被探索完时，才允许继续判断退出；
4. $\max_t Score(t) \le \tau$（没有候选目标能带来显著的 transformed gain）；或
5. 不存在候选目标 $t$ 满足：

$$
Score(t) > q_{\text{eff}} \cdot
\Bigl(\operatorname{len}(path_t) + \operatorname{len}\bigl(path(t \to \text{exit})\bigr) - \operatorname{len}(path_{\text{exit}})\Bigr)
$$

其中括号内为**绕路代价**：
- $\operatorname{len}(path_t)$：当前位置到 target 的步数；
- $\operatorname{len}(path(t \to \text{exit}))$：target 到出口的最短路径步数；
- $\operatorname{len}(path_{\text{exit}})$：当前位置直接去出口的步数。

含义：去 target 的路线比直接去出口多走了绕路代价步，这些步数的机会成本是 $q_{\text{eff}} \times 绕路代价$。只有当目标 reward 大于这个机会成本时，才值得绕过去。

也就是说，ratio 仍为 0 时不能因为出口已知就空手离场，除非已经没有任何不亏到负资源的探索选择；当 ratio 低于 1 且金币数还少于 3 时，也会优先继续探索非出口目标，直到当前已知可探索目标耗尽；在满足这些硬限制后，只有某个 target 的 reward 能覆盖绕路多走的步数代价，AI 才继续去该 target，否则直接走向出口。

代码在 `MemoryGreedyAgent::shouldGoExit()`。

---

## 六、局部资源口袋 First Target Greedy

默认实时贪心在普通候选上仍使用前述 `Score(t)`。但当 AI 当前局部 known_map 中识别出资源口袋时，会启用一个只决定**本轮第一个金币目标**的 pocket-aware 规则。

该规则不生成完整 pocket 清理路线，也不改变全局探索、出口、UNKNOWN 通行或路径搜索规则。AI 到达第一个目标后，下一轮会重新观察、重新识别 pocket、重新评分。

### 6.1 Pocket 识别

候选 hub 来自：

$$
\mathcal{H} = \{P\} \cup \{P \text{ 的已知可走相邻格}\}
$$

对每个 hub $h$，收集半径 `pocketRadius = 2` 内、在 known_map 上已知可达且未收集的金币：

$$
G_h = \{g \mid g \text{ 是未收集金币},\; d_{\text{known}}(h,g) \le pocketRadius\}
$$

如果某个 hub 满足：

$$
|G_h| \ge 2
$$

则形成 pocket。多个 pocket 同时存在时，优先选择金币数量最多、hub 距离当前点最近的 pocket。

### 6.2 第一个金币目标评分

对 pocket 内每个金币 $g$，先计算从当前位置 $P$ 到 $g$ 的 known_map 最短路径 $path_g$：

$$
Base(g) =
\Delta R_{\text{real}}(path_g)
- \eta_q \cdot q_{\text{eff}} \cdot len(path_g)
$$

该路径同样必须满足主 reward 的资源前缀非负硬约束；如果从 $P$ 到 $g$ 的过程中任意一步资源会变成负数，则该 pocket 候选直接记为非法。

然后计算选择 $g$ 之后，剩余 pocket 金币中是否还保留高 Iproxy 的后续出口：

$$
RemainI(g) =
\max_{u \in Pocket,\;u \ne g}
\frac{I_{\text{proxy}}(u)}
{1 + \mu \cdot d_{\text{known}}(g,u)}
$$

当前参数：

| 参数 | 值 | 含义 |
|---|---:|---|
| `pocketRadius` | **2** | 识别局部资源口袋的 known_map 半径 |
| $\mu$ / `pocketMu` | **0.2** | 剩余高 Iproxy 金币的距离折扣 |
| $\lambda_{\text{remain}}$ / `pocketLambdaRemain` | **0.2** | 保留后续出口的权重 |

最终 first target 分数为：

$$
\boxed{
Score_{\text{first}}(g)
= Base(g)
+ \lambda_{\text{remain}} \cdot RemainI(g)
}
$$

关键限制：

- 不把 $I_{\text{proxy}}(g)$ 加到当前要吃的金币 $g$ 上；
- $I_{\text{proxy}}(g)$ 只作为调试字段 `ownIproxy` 显示；
- Iproxy 只奖励“选择 $g$ 后，剩下的金币是否保留了高后续潜力”。

这样在默认 15×15 左上角局部结构中，如果左边金币 Iproxy 高、右边金币 Iproxy 低且两者收益和距离接近：

$$
Score_{\text{first}}(G_{\text{right}})
>
Score_{\text{first}}(G_{\text{left}})
$$

因为先吃右边金币会把高 Iproxy 的左边金币留作后续探索出口。该机制只返回右边金币及其路径，不会一次性生成完整 pocket 清理路线。

---

## 七、Closed Singleton Lookahead Gate

Closed Singleton Gate 是一个**选择门控**，不改变主 reward 公式。它只在当前 reward top-1 候选 $A$ 是 $|C|=1$ 的封闭小节点，且当前还存在有效开放/非封闭候选 $B$ 时启用。

启动条件：

- $A$ 是当前候选队列最高分；
- $A$ 的 $|C|=1$，且路径可达；
- 当前存在有效的开放/非封闭候选 $B$；
- $|C|=0$ 且无资源、无 tail、无出口意义的候选视为死节点；
- 可以对 $A$ 做 memory-only continuation 计算。

对 $A$ 的 continuation 定义为：

- 假设当前位置变为 $A$；
- 不把 $A$ 周围 3×3 的新视野加入 memory；
- 不考虑 $A$ 周围 3×3 内的任何候选节点；
- 只在当前已有 memory 迷宫中，对其它已知候选节点重新计算 reward；
- 路径长度、路径资源结算、$q_{\text{eff}}$ 路径代价都按“从 $A$ 出发”重新计算。

记：

$$
c_A=\max_{x\in ValidCandidates(S_A)} Score_A(x)
$$

若没有有效后继候选，则 $c_A=-\infty$。

最终判断：

$$
Allow(A)
\Longleftrightarrow
Score(A) + \gamma_{\text{closed}} c_A
>
Score(B) + m_{\text{closed}}
$$

其中默认：

$$
\gamma_{\text{closed}}=1.0,\quad m_{\text{closed}}=0.0
$$

该 gate 是非对称的：只计算封闭小节点 $A$ 的 $c_A$，不计算开放候选 $B$ 的 $c_B$。原因是 $B$ 的价值来自打开未知区域，当前 AI 无法可靠预测走到 $B$ 后会发现什么；计算 $c_B$ 要么偷看完整地图，要么基于不完整信息胡乱预测。

该 gate 只处理 $|C|=1$ closed-singleton candidate 抢先的问题，不处理高 Iproxy 开放资源被过早吃掉、resource pocket 清理顺序、大开放 frontier 选择、全局路径规划或出口时机问题。

---

## 八、目标保持机制

AI 选中一个目标后不会每步都切换。只有新最佳目标的分数比当前持有目标高出 `switchMargin = 5.0` 时，才会切换目标。

Pocket 是一个例外：`selectBestPath()` 会先在当前帧重新识别资源口袋并计算 `Score_first`。如果当前帧存在 pocket，就使用新的 pocket first target 决策；只有当前帧没有 pocket 时，才继续保持上一轮 pocket 目标。这样可以避免 AI 到达真正的局部 hub 后仍被旧 pocket 目标锁住。

代码在 `MemoryGreedyAgent::selectBestPath()` 的目标保持逻辑。

---

## 九、当前参数值

| 参数 | 值 | 位置 | 含义 |
|------:|:---|:---|:---|
| $\omega_I$ | **0.175** | $\omega_I \cdot \alpha \cdot I_{\text{proxy}}$ | 信息价值折扣因子 |
| $\alpha_0$ | **4.0** | $\alpha_t^{\text{raw}}$ 分子 | 动态探索权重基准值 |
| $\alpha_{\min}$ | **1.0** | $\operatorname{clip}$ 下界 | 探索权重下限，防止完全不探索 |
| $\alpha_{\max}$ | **8.0** | $\operatorname{clip}$ 上界 | 探索权重上限 |
| $\theta$ | **0.8** | EMA 平滑 | $\alpha$ 平滑系数，越大越稳 |
| $\beta$ | **1.23** | $\beta \cdot V_{\text{tail}}$ | 后续金币机会权重 |
| $\kappa_u$ | **60.0** | $I_{\text{proxy}}$ 展开项 | 未观察区域展开数量贡献权重 |
| $A_{\max}$ | **12** | $\min(E(t), A_{\max})$ | BFS 展开数量裁剪上限，SPSA 可训练 |
| `bossEdgeAreaBonus` | **15** | Boss-gated 且未知延伸触达边缘时的 $E(t)$ | Boss 本体及其后方通向迷宫边缘时的额外推进奖励 |
| `knownExitAreaCap` | **1.5** | 出口已知后的 $\min(E(t), A_{\text{eff}})$ | 出口已知后削弱开阔区域面积奖励 |
| $\rho_{\text{area-min}}$ / `rhoAreaValueMin` | **0.001** | $\rho_{\text{area-value}}$ 的 $\operatorname{clip}$ 下界 | 价值密度下限，SPSA 可训练范围 0.001 到 0.1 |
| $q_{\min}$ | **1.0** | $q_{\text{eff}} = \max(q_{\text{ref}}, q_{\min})$ | 步数代价下限 |
| $\eta_q$ / `qEffLengthWeight` | **0.82** | $\eta_q \cdot q_{\text{eff}} \cdot \operatorname{len}(path)$ | 路径长度机会成本权重 |
| $m_{\text{safe}}$ | **30** | $\phi_{\text{margin}}$ | 一枚陷阱的安全裕量 |
| $\lambda_m$ | **8.0** | $\phi_{\text{margin}}$ 系数 | 低资源 barrier 惩罚强度 |
| `gammaClosedSingleton` | **1.0** | $Score(A)+\gamma_{\text{closed}}c_A$ | Closed Singleton Gate 中 A 后续止损价值权重 |
| `marginClosedSingleton` | **0.0** | $Score(B)+m_{\text{closed}}$ | Closed Singleton Gate 中非封闭候选 B 的比较裕量 |
| `pocketRadius` | **2** | pocket 识别 | 一个 hub 附近多远的金币会被视为同一资源口袋 |
| `pocketMu` | **0.2** | $RemainI$ 距离折扣 | 保留高 Iproxy 金币时，距离越远折扣越大 |
| `pocketLambdaRemain` | **0.2** | $Score_{\text{first}}$ | 控制“把高 Iproxy 金币留到后面”的影响强度 |
| $\tau$ | **5.0** | 停止探索 | 停止阈值 margin |
| $w_U$ | **1.0** | $\alpha_t^{\text{raw}}$ 分子 | 未知占比对 $\alpha$ 的提升 |
| $w_V$ | **1.0** | $\alpha_t^{\text{raw}}$ 分子 | 未知区期望净值对 $\alpha$ 的提升 |
| $w_R$ | **2.0** | $\alpha_t^{\text{raw}}$ 分母 | 陷阱密度对 $\alpha$ 的抑制 |
| $\lambda$ | **10.0** | 密度估计分母 | 平滑伪计数 |
| $\lambda_G$ | **1.0** | $\hat\rho_G$ 分子 | 金币先验 |
| $\lambda_T$ | **1.0** | $\hat\rho_T$ 分子 | 陷阱先验 |
| $\varepsilon$ | **10⁻⁶** | $q_{\text{ref}}$ 分母 | 防除零 |
| switchMargin | **5.0** | 目标保持 | 目标切换阈值 |

---

## 九、设计来源

该 reward function 的理论依据来自分式规划中的 Dinkelbach 转化：把全局 ratio 目标

$$
\max \frac{R}{L}
$$

转成局部加性 surrogate

$$
\Delta R - q_{\text{ref}} \cdot \Delta L
$$

其中 $q_{\text{ref}}$ 是当前对最终比值 $R/L$ 的估计。真实收益、信息价值、后续机会都以资源等价值的形式加在一起，路径长度以 $q_{\text{eff}}$ 为单价扣除，避免了 ratio 形式在分子可负时的数值病态问题。
