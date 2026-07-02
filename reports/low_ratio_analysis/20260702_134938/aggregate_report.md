# Low Ratio Analysis Aggregate Report

- dataset: `C:\Users\tao20\Desktop\Algorithm\AIPlayerDesktop\data\蔡扬的\train`
- sample_count: 300
- selected_low_case_count: 30
- algorithm: realtimeGreedyRun / ai_player_eval + deep_debug
- finished_rate: 0.9967
- mean_ratio: 1.9781
- bottom_ratio_range: 0.0222222 .. 1.26126
- timeout_count: 0

## 主要失败模式

- 29: 探索空白偏多：候选表中空白探索长期压过金币或出口候选。
- 27: 候选路径代价偏高：被选目标的 pathLen 较大，qEff 路径惩罚可能没有压住绕路。
- 26: 陷阱目标被选择：至少一次把 T 作为当前最优目标，需要看 margin/resource 约束是否太松。
- 25: 重复走格偏多：路径存在明显折返，步数被拉长导致 ratio 被稀释。
- 1: 未到达出口：策略最终路径没有落在 E，优先检查停止条件和出口目标选择。
- 1: 资源收益不足：最终金币收益很低，路径中没有积累足够正收益。

## 低分样本

| case | finished | resource | steps | ratio | report |
|---|---:|---:|---:|---:|---|
| train_0076 | False | 20 | 900 | 0.022222222222222223 | `case_reports/train_0076_report.md` |
| train_0143 | True | 110 | 174 | 0.632183908045977 | `case_reports/train_0143_report.md` |
| train_0185 | True | 140 | 194 | 0.7216494845360825 | `case_reports/train_0185_report.md` |
| train_0142 | True | 140 | 152 | 0.9210526315789473 | `case_reports/train_0142_report.md` |
| train_0073 | True | 160 | 172 | 0.9302325581395349 | `case_reports/train_0073_report.md` |
| train_0122 | True | 110 | 116 | 0.9482758620689655 | `case_reports/train_0122_report.md` |
| train_0208 | True | 180 | 182 | 0.989010989010989 | `case_reports/train_0208_report.md` |
| train_0290 | True | 230 | 232 | 0.9913793103448276 | `case_reports/train_0290_report.md` |
| train_0211 | True | 190 | 186 | 1.021505376344086 | `case_reports/train_0211_report.md` |
| train_0063 | True | 140 | 134 | 1.044776119402985 | `case_reports/train_0063_report.md` |
| train_0016 | True | 140 | 132 | 1.0606060606060606 | `case_reports/train_0016_report.md` |
| train_0152 | True | 190 | 170 | 1.1176470588235294 | `case_reports/train_0152_report.md` |
| train_0205 | True | 160 | 142 | 1.1267605633802817 | `case_reports/train_0205_report.md` |
| train_0272 | True | 230 | 204 | 1.1274509803921569 | `case_reports/train_0272_report.md` |
| train_0294 | True | 170 | 150 | 1.1333333333333333 | `case_reports/train_0294_report.md` |
| train_0283 | True | 190 | 164 | 1.1585365853658536 | `case_reports/train_0283_report.md` |
| train_0037 | True | 280 | 240 | 1.1666666666666667 | `case_reports/train_0037_report.md` |
| train_0006 | True | 230 | 196 | 1.1734693877551021 | `case_reports/train_0006_report.md` |
| train_0165 | True | 210 | 178 | 1.1797752808988764 | `case_reports/train_0165_report.md` |
| train_0115 | True | 170 | 144 | 1.1805555555555556 | `case_reports/train_0115_report.md` |
| train_0297 | True | 170 | 142 | 1.1971830985915493 | `case_reports/train_0297_report.md` |
| train_0260 | True | 280 | 232 | 1.206896551724138 | `case_reports/train_0260_report.md` |
| train_0035 | True | 210 | 174 | 1.206896551724138 | `case_reports/train_0035_report.md` |
| train_0261 | True | 280 | 230 | 1.2173913043478262 | `case_reports/train_0261_report.md` |
| train_0194 | True | 210 | 170 | 1.2352941176470589 | `case_reports/train_0194_report.md` |
| train_0161 | True | 280 | 224 | 1.25 | `case_reports/train_0161_report.md` |
| train_0163 | True | 210 | 168 | 1.25 | `case_reports/train_0163_report.md` |
| train_0181 | True | 200 | 160 | 1.25 | `case_reports/train_0181_report.md` |
| train_0019 | True | 140 | 112 | 1.25 | `case_reports/train_0019_report.md` |
| train_0232 | True | 280 | 222 | 1.2612612612612613 | `case_reports/train_0232_report.md` |
