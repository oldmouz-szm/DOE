#!/usr/bin/env python3
import argparse
import csv
import re
import subprocess
import sys
import time
from pathlib import Path
from typing import List, Set


def parse_true_faults(obs_file: Path) -> Set[str]:
    faults: Set[str] = set()
    try:
        with obs_file.open("r", encoding="utf-8") as f:
            for line in f:
                s = line.strip()
                if not s.startswith("#"):
                    continue
                lower = s.lower()
                key = "# faults="
                if lower.startswith(key):
                    raw = s[len(key):].strip()
                    if not raw:
                        return faults
                    for tok in raw.replace(" ", "").split(","):
                        if tok:
                            faults.add(tok)
                    return faults
    except OSError:
        return faults
    return faults


def parse_components(raw: str) -> Set[str]:
    if not raw or raw == "-":
        return set()
    return {x for x in raw.split("|") if x}


def observation_sort_key(path: Path) -> tuple:
    name = path.stem
    m = re.search(r"(\d+)$", name)
    if m:
        return (0, int(m.group(1)), name)
    return (1, 0, name)


def format_table_row(cols: List[str], widths: List[int]) -> str:
    padded = [str(v).ljust(w) for v, w in zip(cols, widths)]
    return " | ".join(padded)


def run_one_observation(
    bin_path: str,
    bench: str,
    obs_file: Path,
    solver_cmd: str,
    max_iters: int,
    timeout_sec: int,
    tmp_dir: Path,
) -> dict:
    out_csv = tmp_dir / f"{obs_file.stem}.csv"
    if out_csv.exists():
        out_csv.unlink()

    cmd: List[str] = [
        bin_path,
        "run",
        "--bench",
        bench,
        "--obs",
        str(obs_file),
        "--solver",
        solver_cmd,
        "--out",
        str(out_csv),
        "--max-iters",
        str(max_iters),
        "--enum-all",
        "1",
        "--print-table",
        "0",
    ]

    t0 = time.perf_counter()
    timed_out = False
    return_code = 0
    stderr_tail = ""
    try:
        proc = subprocess.run(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=timeout_sec,
            check=False,
        )
        return_code = proc.returncode
        if proc.stderr:
            stderr_tail = proc.stderr[-500:]
    except subprocess.TimeoutExpired:
        timed_out = True
    elapsed = time.perf_counter() - t0

    true_faults = parse_true_faults(obs_file)
    rows = []
    if out_csv.exists():
        with out_csv.open("r", encoding="utf-8", newline="") as f:
            reader = csv.DictReader(f)
            rows = list(reader)
    # 如果发生超时，则只保留在规定时间内写入的诊断行（依据 CSV 中的 seconds 字段）
    if timed_out and rows:
        filtered = []
        for r in rows:
            s = r.get("seconds", "")
            try:
                sec = float(s)
            except Exception:
                # 无法解析 seconds，保守地跳过该行
                continue
            # 允许少量浮点误差
            if sec <= float(timeout_sec) + 1e-9:
                filtered.append(r)
        rows = filtered
    diag_count = len(rows)
    # 检查在 CSV 中是否存在已解（可能为超时前写入的部分解）
    partial_solved = bool(rows and any(r.get("solved", "0") == "1" for r in rows))
    # 要求：超时（timed_out）时，不把该观测计入成功率（solved==0），
    # 但仍保留超时前写入的诊断用于其他统计（diag_count/HITrate/...）。
    if timed_out:
        solved = 0
    else:
        solved = 1 if partial_solved else 0

    sizes: List[int] = []
    hit_solution_count = 0
    all_components: Set[str] = set()
    
    for r in rows:
        try:
            size = int(r.get("diag_size", "0"))
        except ValueError:
            size = 0
        sizes.append(size)

        comps = parse_components(r.get("diag_components", ""))
        all_components.update(comps)
        
        hit_faults = len(comps & true_faults)
        if hit_faults > 0:
            hit_solution_count += 1

    # 计算新的命中率：所有诊断解的所有组件集合中的真实故障数 / 所有组件集合尺寸
    total_components = len(all_components)
    total_true_faults_in_components = len(all_components & true_faults)
    hit_rate = (total_true_faults_in_components / total_components) if total_components > 0 else 0.0
    
    avg_diag_size = (sum(sizes) / diag_count) if diag_count > 0 else 0.0

    return {
        "obs": obs_file.name,
        "solved": solved,
        "partial_solved": partial_solved,
        "elapsed_sec": elapsed,
        "diag_count": diag_count,
        "avg_diag_size": avg_diag_size,
        "hit_solution_count": hit_solution_count,
        "avg_hit_rate": hit_rate,  # 现在是新的命中率
        "timed_out": timed_out,
        "return_code": return_code,
        "stderr_tail": stderr_tail,
        "total_components": total_components,  # 所有组件集合尺寸
        "total_true_faults": total_true_faults_in_components,  # 所有组件集合中的真实故障数
        # 伪正常：没有标注真实故障，或没有任何候选组件（没有可统计的诊断）
        "is_pseudo_normal": (len(true_faults) == 0) or (total_components == 0),
    }



def main() -> int:
    parser = argparse.ArgumentParser(
        description="批量运行单个观测目录，并输出每个观测的精简诊断统计。"
    )
    parser.add_argument("--bin", default="build/doe_maxsat", help="doe_maxsat 可执行文件路径")
    parser.add_argument("--bench", required=True, help="bench 文件路径")
    parser.add_argument("--obs-dir", required=True, help="观测目录（如 obs/c17_1/obs）")
    parser.add_argument("--solver", required=True, help='求解器命令，如 "python3 tools/solve_with_rc2.py"')
    parser.add_argument("--max-iters", type=int, default=2, help="DOE 最大迭代次数")
    parser.add_argument("--timeout-sec", type=int, default=30, help="每个观测总超时秒数")
    parser.add_argument("--tmp-dir", default=".batch_tmp", help="临时输出目录")
    parser.add_argument("--out", default="", help="可选：将结果同时写入该 CSV 文件")
    args = parser.parse_args()

    obs_dir = Path(args.obs_dir)
    if not obs_dir.is_dir():
        print(f"ERROR: 观测目录不存在: {obs_dir}", file=sys.stderr)
        return 2

    tmp_dir = Path(args.tmp_dir)
    tmp_dir.mkdir(parents=True, exist_ok=True)

    obs_files = sorted(obs_dir.glob("*.txt"), key=observation_sort_key)
    if not obs_files:
        print(f"ERROR: 观测目录下没有 .txt 文件: {obs_dir}", file=sys.stderr)
        return 2

    col_names = [
        "obs",
        "solved",
        "time",
        "count",
        "size",
        "hits",
        "HITrate",
        "timeout",
        "comp",
        "faults",
    ]
    widths = [16, 6, 11, 8, 8, 8, 11, 7, 8, 8]
    header = format_table_row(col_names, widths)
    divider = "-+-" .join("-" * w for w in widths)
    print(header)
    print(divider)

    out_fp = None
    if args.out:
        out_path = Path(args.out)
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_fp = out_path.open("w", encoding="utf-8", newline="")
        out_fp.write(header + "\n")
        out_fp.write(divider + "\n")

    total_elapsed = 0.0
    total_hit_rate = 0.0
    total_solved = 0
    total_obs = 0
    total_all_components = 0
    total_all_true_faults = 0
    total_hit_rate_non_pseudo = 0.0
    count_non_pseudo = 0
    results = []

    for obs_file in obs_files:
        result = run_one_observation(
            bin_path=args.bin,
            bench=args.bench,
            obs_file=obs_file,
            solver_cmd=args.solver,
            max_iters=args.max_iters,
            timeout_sec=args.timeout_sec,
            tmp_dir=tmp_dir,
        )
        results.append(result)
        total_obs += 1
        total_elapsed += result["elapsed_sec"]
        total_hit_rate += result["avg_hit_rate"]
        # 只统计非伪正常观测用于平均命中率
        if not result.get("is_pseudo_normal", False):
            total_hit_rate_non_pseudo += result["avg_hit_rate"]
            count_non_pseudo += 1
        total_solved += result["solved"]
        total_all_components += result.get("total_components", 0)
        total_all_true_faults += result.get("total_true_faults", 0)

        line = format_table_row(
            [
                result["obs"],
                f"{result['solved']}",
                f"{result['elapsed_sec']:.6f}",
                f"{result['diag_count']}",
                f"{int(result['avg_diag_size'])}",  # 输出整数
                f"{result['hit_solution_count']}",
                f"{result['avg_hit_rate']:.2%}",  # 输出百分数
                f"{1 if result['timed_out'] else 0}",
                f"{result['total_components']}",
                f"{result['total_true_faults']}",
            ],
            widths,
        )
        print(line)
        if out_fp is not None:
            out_fp.write(line + "\n")

    # 计算平均命中率：所有命中率之和 / 观测数
    # 平均命中率：只对非伪正常观测计算平均
    avg_hit_rate_all = (total_hit_rate_non_pseudo / count_non_pseudo) if count_non_pseudo > 0 else 0.0
    # 计算平均时间：总时长 / 观测数
    avg_time_all = (total_elapsed / total_obs) if total_obs > 0 else 0.0
    # 计算成功率：成功数 / 观测数
    success_rate = (total_solved / total_obs) if total_obs > 0 else 0.0
    
    # 输出简单格式的汇总
    print("\nSUMMARY:")
    print(f"success_rate: {total_solved}/{total_obs} ({success_rate:.2%})")
    print(f"avg_hit_rate: ({total_hit_rate_non_pseudo:.2f}/{count_non_pseudo}) ({avg_hit_rate_all:.2%})")
    print(f"avg_time: {avg_time_all:.6f}s")
    print(f"total_time: {total_elapsed:.6f}s")
    print(f"total_components: {total_all_components}")
    print(f"total_true_faults: {total_all_true_faults}")
    
    if out_fp is not None:
        out_fp.write("\nSUMMARY:\n")
        out_fp.write(f"success_rate: {total_solved}/{total_obs} ({success_rate:.2%})\n")
        out_fp.write(f"avg_hit_rate: ({total_hit_rate_non_pseudo:.2f}/{count_non_pseudo}) ({avg_hit_rate_all:.2%})\n")
        out_fp.write(f"avg_time: {avg_time_all:.6f}s\n")
        out_fp.write(f"total_time: {total_elapsed:.6f}s\n")
        out_fp.write(f"total_components: {total_all_components}\n")
        out_fp.write(f"total_true_faults: {total_all_true_faults}\n")

    if out_fp is not None:
        out_fp.close()

    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except BrokenPipeError:
        # Allow piping to tools like `head` without stack traces.
        sys.exit(0)
