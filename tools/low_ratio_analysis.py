#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""批量挖掘低分迷宫，并基于现有 debug 工具生成归因报告。"""

from __future__ import annotations

import argparse
import csv
import json
import math
import subprocess
import sys
import time
from collections import Counter
from datetime import datetime
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DATASET = REPO_ROOT / "data" / "蔡扬的" / "train"
DEFAULT_REPORT_ROOT = REPO_ROOT / "reports" / "low_ratio_analysis"


# 功能：解析命令行参数。
# 输入：
#   - 无显式输入，参数来自命令行；dataset 必须指向包含迷宫 JSON 的文件夹。
# 输出：
#   - 返回 argparse.Namespace，供主流程决定采样数量、低分案例数量和超时时间。
# 关键逻辑：
#   - 默认数据集直接指向用户指定的“蔡扬的/train”，避免手动复制到 tools/train_tmp。
def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Mine low-ratio greedy cases and generate attribution reports.")
    parser.add_argument("--dataset", type=Path, default=DEFAULT_DATASET, help="folder that contains maze json files")
    parser.add_argument("--sample-count", type=int, default=300, help="number of maze files to evaluate")
    parser.add_argument("--low-case-count", type=int, default=30, help="number of low-ratio cases to trace")
    parser.add_argument("--timeout", type=float, default=60.0, help="timeout seconds for each executable call")
    parser.add_argument("--report-root", type=Path, default=DEFAULT_REPORT_ROOT, help="root folder for generated reports")
    parser.add_argument("--eval-exe", type=Path, default=REPO_ROOT / "build" / "ai_player_eval.exe")
    parser.add_argument("--debug-exe", type=Path, default=REPO_ROOT / "build" / "deep_debug.exe")
    return parser.parse_args()


# 功能：用 UTF-8-SIG 读取 JSON 文件。
# 输入：
#   - path：目标 JSON 路径，允许带 BOM 或不带 BOM。
# 输出：
#   - 返回解析后的 Python 对象。
# 关键逻辑：
#   - 数据集和报告都可能包含中文路径，显式使用 utf-8-sig 避免 BOM 影响 JSON 解析。
def read_json(path: Path) -> Any:
    with path.open("r", encoding="utf-8-sig") as handle:
        return json.load(handle)


# 功能：写入 JSON 报告文件。
# 输入：
#   - path：输出路径。
#   - data：可 JSON 序列化的数据。
# 输出：
#   - 在磁盘生成 UTF-8-SIG JSON 文件。
# 关键逻辑：
#   - 报告内容包含中文归因文本，使用 BOM 便于 Windows 工具直接打开。
def write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8-sig", newline="\n") as handle:
        json.dump(data, handle, ensure_ascii=False, indent=2)
        handle.write("\n")


# 功能：写入 CSV 表格。
# 输入：
#   - path：输出路径。
#   - rows：行字典列表。
#   - fieldnames：列顺序。
# 输出：
#   - 在磁盘生成 UTF-8-SIG CSV 文件。
# 关键逻辑：
#   - 候选表会被 Excel 打开查看，UTF-8-SIG 可以减少中文列内容乱码。
def write_csv(path: Path, rows: list[dict[str, Any]], fieldnames: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8-sig", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


# 功能：调用外部评估或 debug 可执行文件。
# 输入：
#   - cmd：命令参数列表。
#   - timeout：单次调用最大等待秒数。
# 输出：
#   - 返回 returncode、stdout、stderr、duration 和 timeout 标记。
# 关键逻辑：
#   - 前端卡死类问题需要先区分算法慢和进程异常，这里把每个样本的耗时与超时单独记录。
def run_process(cmd: list[str], timeout: float) -> dict[str, Any]:
    started = time.perf_counter()
    try:
        completed = subprocess.run(
            cmd,
            cwd=REPO_ROOT,
            text=True,
            encoding="utf-8",
            errors="replace",
            capture_output=True,
            timeout=timeout,
            check=False,
        )
        return {
            "returncode": completed.returncode,
            "stdout": completed.stdout.strip(),
            "stderr": completed.stderr.strip(),
            "duration_sec": time.perf_counter() - started,
            "timeout": False,
        }
    except subprocess.TimeoutExpired as ex:
        return {
            "returncode": None,
            "stdout": (ex.stdout or "").strip() if isinstance(ex.stdout, str) else "",
            "stderr": (ex.stderr or "").strip() if isinstance(ex.stderr, str) else "",
            "duration_sec": time.perf_counter() - started,
            "timeout": True,
        }


# 功能：收集待评估的迷宫文件。
# 输入：
#   - dataset：包含 JSON 迷宫的文件夹。
#   - sample_count：最多采样多少个文件；小于等于 0 表示使用全部文件。
# 输出：
#   - 返回按文件名排序后的 Path 列表。
# 关键逻辑：
#   - 采用确定性排序，保证每次报告可复现，便于对比 reward 修改前后的低分案例。
def collect_maze_files(dataset: Path, sample_count: int) -> list[Path]:
    if not dataset.exists():
        raise FileNotFoundError(f"dataset folder does not exist: {dataset}")
    files = sorted(path for path in dataset.glob("*.json") if path.is_file())
    if sample_count > 0:
        files = files[:sample_count]
    if not files:
        raise RuntimeError(f"no json files found in dataset: {dataset}")
    return files


# 功能：运行单个迷宫的 summary 评估。
# 输入：
#   - eval_exe：ai_player_eval.exe 路径。
#   - maze_file：迷宫 JSON 路径。
#   - timeout：单次评估超时。
# 输出：
#   - 返回一行 summary 指标，包含 ok、finished、resource、steps、score_ratio、耗时和错误信息。
# 关键逻辑：
#   - 低分筛选只依赖 summary 指标，避免对全部样本生成大体积 trace。
def evaluate_one(eval_exe: Path, maze_file: Path, timeout: float) -> dict[str, Any]:
    result = run_process([str(eval_exe), str(maze_file)], timeout)
    row: dict[str, Any] = {
        "case_id": maze_file.stem,
        "maze_file": str(maze_file),
        "ok": False,
        "finished": False,
        "resource": 0,
        "steps": 0,
        "score_ratio": float("-inf") if result["timeout"] else 0.0,
        "duration_sec": round(result["duration_sec"], 6),
        "timeout": result["timeout"],
        "error": "",
    }
    if result["timeout"]:
        row["error"] = "timeout"
        return row
    try:
        payload = json.loads(result["stdout"])
    except json.JSONDecodeError:
        row["error"] = result["stderr"] or result["stdout"][:200]
        return row

    row["ok"] = bool(payload.get("ok", False))
    row["finished"] = bool(payload.get("finished", False))
    row["resource"] = int(payload.get("resource", 0))
    row["steps"] = int(payload.get("steps", 0))
    row["score_ratio"] = float(payload.get("score_ratio", 0.0))
    row["error"] = str(payload.get("error", ""))
    return row


# 功能：对候选集判断主导项。
# 输入：
#   - candidate：deep_debug 导出的单个候选目标。
# 输出：
#   - 返回贡献幅度最大的项名。
# 关键逻辑：
#   - 这里不重算 reward，只做 debug 归因，把 deltaR、Iproxy、tailGain、路径长度惩罚和 margin 拆开比较。
def dominant_term(candidate: dict[str, Any] | None) -> str:
    if not candidate:
        return ""
    terms = {
        "deltaR": abs(float(candidate.get("deltaR", 0.0))),
        "Iproxy": abs(float(candidate.get("Iproxy", 0.0))),
        "tailGain": abs(float(candidate.get("tailGain", 0.0))),
        "pathLen": abs(float(candidate.get("pathLen", 0.0)) * float(candidate.get("qEff", 1.0))),
        "margin": abs(float(candidate.get("margin", 0.0))),
    }
    return max(terms.items(), key=lambda item: item[1])[0]


# 功能：把 deep_debug JSON 展开成 step 表和 candidate 表。
# 输入：
#   - case_id：样本编号。
#   - maze_file：迷宫路径。
#   - trace：deep_debug.exe 输出的 JSON 对象。
# 输出：
#   - 返回 steps、candidates 和 stats 三部分数据。
# 关键逻辑：
#   - 每一步保留被选目标、次优目标和 reward 分解字段，便于定位“为什么当时没去金币/出口”。
def expand_trace(case_id: str, maze_file: Path, trace: dict[str, Any]) -> dict[str, Any]:
    path_rows = {int(row.get("step", index)): row for index, row in enumerate(trace.get("path", []))}
    steps: list[dict[str, Any]] = []
    candidates: list[dict[str, Any]] = []
    selected_tiles: Counter[str] = Counter()
    no_candidate_steps = 0
    selected_path_lengths: list[float] = []

    for debug in trace.get("debugSteps", []):
        step_index = int(debug.get("step", len(steps)))
        path_state = path_rows.get(step_index, {})
        sorted_candidates = sorted(debug.get("candidates", []), key=lambda row: float(row.get("score", -math.inf)), reverse=True)
        selected = next((row for row in sorted_candidates if row.get("selected")), sorted_candidates[0] if sorted_candidates else None)
        runner_up = next((row for row in sorted_candidates if row is not selected), None)
        if not sorted_candidates:
            no_candidate_steps += 1
        if selected:
            selected_tiles[str(selected.get("tile", ""))] += 1
            selected_path_lengths.append(float(selected.get("pathLen", 0.0)))

        steps.append(
            {
                "case_id": case_id,
                "step": step_index,
                "row": path_state.get("row", debug.get("real_r", "")),
                "col": path_state.get("col", debug.get("real_c", "")),
                "tile": path_state.get("tile", ""),
                "resource": path_state.get("resource", ""),
                "delta": path_state.get("delta", ""),
                "first_visit": path_state.get("first_visit", ""),
                "decision": debug.get("decision", ""),
                "observedRatio": debug.get("observedRatio", ""),
                "qEff": debug.get("qEff", ""),
                "alpha": debug.get("alpha", ""),
                "candidate_count": len(sorted_candidates),
                "selected_row": selected.get("real_r", "") if selected else "",
                "selected_col": selected.get("real_c", "") if selected else "",
                "selected_tile": selected.get("tile", "") if selected else "",
                "selected_score": selected.get("score", "") if selected else "",
                "selected_deltaR": selected.get("deltaR", "") if selected else "",
                "selected_Iproxy": selected.get("Iproxy", "") if selected else "",
                "selected_tailGain": selected.get("tailGain", "") if selected else "",
                "selected_pathLen": selected.get("pathLen", "") if selected else "",
                "selected_margin": selected.get("margin", "") if selected else "",
                "selected_projectedR": selected.get("projectedR", "") if selected else "",
                "dominant_term": dominant_term(selected),
                "runner_up_row": runner_up.get("real_r", "") if runner_up else "",
                "runner_up_col": runner_up.get("real_c", "") if runner_up else "",
                "runner_up_tile": runner_up.get("tile", "") if runner_up else "",
                "runner_up_score": runner_up.get("score", "") if runner_up else "",
                "score_gap": (
                    float(selected.get("score", 0.0)) - float(runner_up.get("score", 0.0))
                    if selected and runner_up
                    else ""
                ),
            }
        )

        for rank, candidate in enumerate(sorted_candidates, start=1):
            candidates.append(
                {
                    "case_id": case_id,
                    "maze_file": str(maze_file),
                    "step": step_index,
                    "rank": rank,
                    "selected": bool(candidate.get("selected", False)),
                    "real_r": candidate.get("real_r", ""),
                    "real_c": candidate.get("real_c", ""),
                    "local_r": candidate.get("local_r", ""),
                    "local_c": candidate.get("local_c", ""),
                    "tile": candidate.get("tile", ""),
                    "score": candidate.get("score", ""),
                    "deltaR": candidate.get("deltaR", ""),
                    "Iproxy": candidate.get("Iproxy", ""),
                    "tailGain": candidate.get("tailGain", ""),
                    "qEff": candidate.get("qEff", ""),
                    "pathLen": candidate.get("pathLen", ""),
                    "margin": candidate.get("margin", ""),
                    "projectedR": candidate.get("projectedR", ""),
                    "unknownCompSum": candidate.get("unknownCompSum", ""),
                    "dominant_term": dominant_term(candidate),
                }
            )

    visited = [(row.get("row"), row.get("col")) for row in path_rows.values()]
    unique_visited = set(visited)
    stats = {
        "debug_step_count": len(steps),
        "candidate_count": len(candidates),
        "no_candidate_steps": no_candidate_steps,
        "selected_blank_count": selected_tiles.get(" ", 0),
        "selected_gold_count": selected_tiles.get("G", 0),
        "selected_trap_count": selected_tiles.get("T", 0),
        "selected_exit_count": selected_tiles.get("E", 0),
        "avg_selected_pathLen": round(sum(selected_path_lengths) / len(selected_path_lengths), 4)
        if selected_path_lengths
        else 0.0,
        "max_selected_pathLen": max(selected_path_lengths) if selected_path_lengths else 0.0,
        "repeat_rate": round(1.0 - len(unique_visited) / len(visited), 4) if visited else 0.0,
    }
    return {"steps": steps, "candidates": candidates, "stats": stats}


# 功能：根据指标和 trace 生成归因标签。
# 输入：
#   - metric：summary 指标。
#   - expanded：展开后的 step/candidate/stats。
# 输出：
#   - 返回中文失败模式列表。
# 关键逻辑：
#   - 归因是自动化初筛，不替代人工判断；规则只使用 path 与候选 reward 证据。
def classify_failure(metric: dict[str, Any], expanded: dict[str, Any]) -> list[str]:
    stats = expanded["stats"]
    steps = expanded["steps"]
    candidates = expanded["candidates"]
    modes: list[str] = []
    if metric.get("timeout"):
        modes.append("运行超时：该样本在限定时间内没有返回，需要单独检查搜索分支或前端调用节流。")
    if not metric.get("finished"):
        modes.append("未到达出口：策略最终路径没有落在 E，优先检查停止条件和出口目标选择。")
    if metric.get("resource", 0) <= 50:
        modes.append("资源收益不足：最终金币收益很低，路径中没有积累足够正收益。")
    if stats["repeat_rate"] >= 0.25:
        modes.append("重复走格偏多：路径存在明显折返，步数被拉长导致 ratio 被稀释。")
    if stats["selected_blank_count"] > stats["selected_gold_count"] * 2 and stats["selected_blank_count"] >= 10:
        modes.append("探索空白偏多：候选表中空白探索长期压过金币或出口候选。")
    if stats["selected_trap_count"] > 0:
        modes.append("陷阱目标被选择：至少一次把 T 作为当前最优目标，需要看 margin/resource 约束是否太松。")
    if stats["avg_selected_pathLen"] >= 4 or stats["max_selected_pathLen"] >= 10:
        modes.append("候选路径代价偏高：被选目标的 pathLen 较大，qEff 路径惩罚可能没有压住绕路。")

    exit_steps = [row for row in steps if row.get("selected_tile") == "E"]
    if exit_steps and metric.get("resource", 0) <= 150:
        first_exit = exit_steps[0]
        modes.append(f"出口时机偏早：第 {first_exit.get('step')} 步已选择出口，但最终资源只有 {metric.get('resource', 0)}。")

    gold_candidates = [row for row in candidates if row.get("tile") == "G"]
    selected_gold = [row for row in gold_candidates if row.get("selected")]
    if gold_candidates and len(selected_gold) < max(1, len(gold_candidates) // 5):
        modes.append("金币候选命中率低：候选表出现过金币，但很少成为被选目标。")

    if not modes:
        modes.append("未触发强规则：需要人工查看候选表中 score_gap、Iproxy、deltaR 的局部变化。")
    return modes


# 功能：挑出最值得人工查看的异常步骤。
# 输入：
#   - steps：展开后的每步决策表。
# 输出：
#   - 返回最多 12 行步骤摘要。
# 关键逻辑：
#   - 优先展示陷阱、出口、长路径、小分差和纯探索步骤，减少人工翻完整 CSV 的成本。
def select_interesting_steps(steps: list[dict[str, Any]]) -> list[dict[str, Any]]:
    scored: list[tuple[int, dict[str, Any]]] = []
    for row in steps:
        score = 0
        if row.get("selected_tile") == "T":
            score += 100
        if row.get("selected_tile") == "E":
            score += 80
        if row.get("selected_tile") == " " and row.get("dominant_term") == "Iproxy":
            score += 40
        try:
            if float(row.get("selected_pathLen") or 0) >= 6:
                score += 30
        except (TypeError, ValueError):
            pass
        try:
            gap = row.get("score_gap")
            if gap != "" and abs(float(gap)) <= 2:
                score += 20
        except (TypeError, ValueError):
            pass
        if score:
            scored.append((score, row))
    return [row for _, row in sorted(scored, key=lambda item: (-item[0], int(item[1].get("step", 0))))[:12]]


# 功能：写单个低分案例报告。
# 输入：
#   - report_path：报告输出路径。
#   - metric：summary 指标。
#   - modes：失败模式列表。
#   - expanded：展开后的 trace 数据。
# 输出：
#   - 生成 Markdown 案例报告。
# 关键逻辑：
#   - 报告只引用少量关键步骤，完整证据仍保留在 trace JSON 和 CSV 表中。
def write_case_report(report_path: Path, metric: dict[str, Any], modes: list[str], expanded: dict[str, Any]) -> None:
    interesting = select_interesting_steps(expanded["steps"])
    lines = [
        f"# {metric['case_id']} 低分案例报告",
        "",
        "## Summary",
        "",
        f"- file: `{metric['maze_file']}`",
        f"- finished: {metric['finished']}",
        f"- resource: {metric['resource']}",
        f"- steps: {metric['steps']}",
        f"- score_ratio: {metric['score_ratio']}",
        f"- duration_sec: {metric['duration_sec']}",
        "",
        "## 自动归因",
        "",
    ]
    lines.extend(f"- {mode}" for mode in modes)
    lines.extend(["", "## Trace 统计", ""])
    for key, value in expanded["stats"].items():
        lines.append(f"- {key}: {value}")
    lines.extend(["", "## 重点步骤", ""])
    if interesting:
        lines.append("| step | pos | tile | decision | selected | score | deltaR | Iproxy | pathLen | margin | qEff | gap |")
        lines.append("|---:|---|---|---|---|---:|---:|---:|---:|---:|---:|---:|")
        for row in interesting:
            pos = f"({row.get('row')},{row.get('col')})"
            target = f"{row.get('selected_tile')}@({row.get('selected_row')},{row.get('selected_col')})"
            lines.append(
                "| "
                + " | ".join(
                    str(value)
                    for value in [
                        row.get("step", ""),
                        pos,
                        row.get("tile", ""),
                        row.get("decision", ""),
                        target,
                        row.get("selected_score", ""),
                        row.get("selected_deltaR", ""),
                        row.get("selected_Iproxy", ""),
                        row.get("selected_pathLen", ""),
                        row.get("selected_margin", ""),
                        row.get("qEff", ""),
                        row.get("score_gap", ""),
                    ]
                )
                + " |"
            )
    else:
        lines.append("未筛出高优先级异常步骤，请直接查看 CSV 候选表。")
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text("\n".join(lines) + "\n", encoding="utf-8-sig", newline="\n")


# 功能：写总报告。
# 输入：
#   - report_path：聚合报告路径。
#   - metrics：全部样本 summary 指标。
#   - selected：被选中的低分样本。
#   - failure_counter：失败模式计数。
# 输出：
#   - 生成 Markdown 聚合报告。
# 关键逻辑：
#   - 汇总 finished_rate、mean_ratio、bottom range 和 timeout，作为后续 reward/策略修改的基线。
def write_aggregate_report(
    report_path: Path,
    dataset: Path,
    metrics: list[dict[str, Any]],
    selected: list[dict[str, Any]],
    failure_counter: Counter[str],
) -> None:
    finite = [float(row["score_ratio"]) for row in metrics if math.isfinite(float(row["score_ratio"]))]
    finished_count = sum(1 for row in metrics if row.get("finished"))
    timeout_count = sum(1 for row in metrics if row.get("timeout"))
    mean_ratio = sum(finite) / len(finite) if finite else 0.0
    selected_finite = [float(row["score_ratio"]) for row in selected if math.isfinite(float(row["score_ratio"]))]
    bottom_range = (
        f"{min(selected_finite):.6g} .. {max(selected_finite):.6g}"
        if selected_finite
        else "no finite ratio"
    )

    lines = [
        "# Low Ratio Analysis Aggregate Report",
        "",
        f"- dataset: `{dataset}`",
        f"- sample_count: {len(metrics)}",
        f"- selected_low_case_count: {len(selected)}",
        f"- algorithm: realtimeGreedyRun / ai_player_eval + deep_debug",
        f"- finished_rate: {finished_count / len(metrics):.4f}",
        f"- mean_ratio: {mean_ratio:.6g}",
        f"- bottom_ratio_range: {bottom_range}",
        f"- timeout_count: {timeout_count}",
        "",
        "## 主要失败模式",
        "",
    ]
    if failure_counter:
        for mode, count in failure_counter.most_common():
            lines.append(f"- {count}: {mode}")
    else:
        lines.append("- 未统计到失败模式。")

    lines.extend(["", "## 低分样本", ""])
    lines.append("| case | finished | resource | steps | ratio | report |")
    lines.append("|---|---:|---:|---:|---:|---|")
    for row in selected:
        case_id = row["case_id"]
        lines.append(
            f"| {case_id} | {row['finished']} | {row['resource']} | {row['steps']} | "
            f"{row['score_ratio']} | `case_reports/{case_id}_report.md` |"
        )
    report_path.write_text("\n".join(lines) + "\n", encoding="utf-8-sig", newline="\n")


# 功能：检查工具路径是否存在。
# 输入：
#   - args：命令行参数。
# 输出：
#   - 无返回值；路径缺失时抛出异常。
# 关键逻辑：
#   - 这是批处理入口，提前失败比跑到一半才发现 exe 缺失更容易定位环境问题。
def validate_paths(args: argparse.Namespace) -> None:
    if not args.eval_exe.exists():
        raise FileNotFoundError(f"eval exe does not exist: {args.eval_exe}")
    if not args.debug_exe.exists():
        raise FileNotFoundError(f"debug exe does not exist: {args.debug_exe}")
    if args.sample_count < 0:
        raise ValueError("--sample-count must be >= 0")
    if args.low_case_count <= 0:
        raise ValueError("--low-case-count must be > 0")


# 功能：主流程入口。
# 输入：
#   - 命令行参数指定数据集、样本数、低分样本数和报告目录。
# 输出：
#   - 在 reports/low_ratio_analysis/<timestamp>/ 下生成完整分析产物。
# 关键逻辑：
#   - 先用轻量 summary 评估全样本，再只对低分样本跑 deep_debug，控制总体耗时和文件体积。
def main() -> int:
    args = parse_args()
    validate_paths(args)

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    out_dir = args.report_root / timestamp
    trace_dir = out_dir / "traces"
    case_report_dir = out_dir / "case_reports"
    out_dir.mkdir(parents=True, exist_ok=True)
    trace_dir.mkdir(parents=True, exist_ok=True)

    files = collect_maze_files(args.dataset, args.sample_count)
    print(f"[1/3] evaluating {len(files)} maze files from {args.dataset}", flush=True)
    metrics = [evaluate_one(args.eval_exe, maze_file, args.timeout) for maze_file in files]
    metrics_for_csv = [
        {**row, "score_ratio": "" if row["score_ratio"] == float("-inf") else row["score_ratio"]}
        for row in metrics
    ]
    summary_fields = [
        "case_id",
        "maze_file",
        "ok",
        "finished",
        "resource",
        "steps",
        "score_ratio",
        "duration_sec",
        "timeout",
        "error",
    ]
    write_csv(out_dir / "summary_metrics.csv", metrics_for_csv, summary_fields)
    write_json(out_dir / "summary_metrics.json", metrics_for_csv)

    selected = sorted(
        metrics,
        key=lambda row: (
            float(row["score_ratio"]) if math.isfinite(float(row["score_ratio"])) else -1e18,
            bool(row.get("finished")),
            -int(row.get("steps", 0)),
        ),
    )[: args.low_case_count]
    selected_for_json = [
        {**row, "score_ratio": "" if row["score_ratio"] == float("-inf") else row["score_ratio"]}
        for row in selected
    ]
    write_json(out_dir / "selected_low_cases.json", selected_for_json)

    print(f"[2/3] tracing {len(selected)} low-ratio cases with deep_debug", flush=True)
    failure_counter: Counter[str] = Counter()
    for index, row in enumerate(selected, start=1):
        case_id = row["case_id"]
        maze_file = Path(row["maze_file"])
        trace_path = trace_dir / f"{case_id}_trace.json"
        process = run_process([str(args.debug_exe), str(maze_file), str(trace_path)], args.timeout)
        if process["timeout"] or process["returncode"] != 0:
            row["trace_error"] = "timeout" if process["timeout"] else (process["stderr"] or process["stdout"])
            write_case_report(case_report_dir / f"{case_id}_report.md", row, ["trace 导出失败：" + row["trace_error"]], {"steps": [], "candidates": [], "stats": {}})
            failure_counter["trace 导出失败：" + row["trace_error"]] += 1
            continue
        trace = read_json(trace_path)
        expanded = expand_trace(case_id, maze_file, trace)
        step_fields = list(expanded["steps"][0].keys()) if expanded["steps"] else ["case_id", "step"]
        candidate_fields = list(expanded["candidates"][0].keys()) if expanded["candidates"] else ["case_id", "step"]
        write_csv(trace_dir / f"{case_id}_steps.csv", expanded["steps"], step_fields)
        write_csv(trace_dir / f"{case_id}_candidate_table.csv", expanded["candidates"], candidate_fields)
        modes = classify_failure(row, expanded)
        failure_counter.update(modes)
        write_case_report(case_report_dir / f"{case_id}_report.md", row, modes, expanded)
        print(f"  traced {index}/{len(selected)} {case_id}", flush=True)

    print("[3/3] writing aggregate report", flush=True)
    write_aggregate_report(out_dir / "aggregate_report.md", args.dataset, metrics, selected, failure_counter)
    print(f"done: {out_dir}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
