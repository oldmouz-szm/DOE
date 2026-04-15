# DOE MaxSAT (IJCAI 2015) C++ Reproduction

This project implements the Dominator-Oriented Encoding (DOE) described in:

- J. Marques-Silva, M. Janota, A. Ignatiev, A. Morgado, "Efficient Model Based Diagnosis with Maximum Satisfiability", IJCAI 2015.

## What is implemented

- ISCAS `.bench` parser.
- Observation/scenario parser and generator.
- DOE preprocessing loop (bounded by `--max-iters`, default `2`):
  - dominators (Lengauer-Tarjan algorithm),
  - dominated gates as hard components,
  - backbone-node propagation on dominated components,
  - blocked-edge detection,
  - filtered nodes/edges fixpoint.
- MaxSAT encoding (WCNF):
  - hard clauses from SD and observation,
  - soft unit clauses `¬Ab(c)` for non-dominated components.
- Diagnosis enumeration with blocking clauses and cardinality constraints.
- Solver runner for external MaxSAT solvers (e.g., `open-wbo-inc`, `eva500a`).

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Or without cmake:

```bash
g++ -std=c++17 -O3 -DNDEBUG -Wall -Wextra -Wpedantic src/doe_maxsat.cpp -o build/doe_maxsat
```

Binary: `build/doe_maxsat`

## Observation format

```text
SCENARIO s0
IN 1=1 2=0 3=1 6=1 7=1
OUT 22=0 23=1
END
```

Or simplified format:

```text
# inputs
1=0
2=0
3=1
# outputs
22=0
23=0
```

## Commands

Generate scenarios:

```bash
build/doe_maxsat generate \
  --bench iscas85/bench/c17.bench \
  --out obs_c17.txt \
  --count 100 \
  --seed 1 \
  --min-output-errors 1 \
  --max-output-errors 50
```

Encode scenarios to WCNF:

```bash
build/doe_maxsat encode \
  --bench iscas85/bench/c17.bench \
  --obs obs_c17.txt \
  --outdir wcnf_c17 \
  --max-iters 2
```

Run solver (single diagnosis):

```bash
build/doe_maxsat run \
  --bench iscas85/bench/c17.bench \
  --obs obs_c17.txt \
  --solver "/path/to/open-wbo-inc" \
  --out results_c17.csv \
  --max-iters 2
```

Enumerate all minimum-cost diagnoses:

```bash
build/doe_maxsat run \
  --bench iscas85/bench/c6288.bench \
  --obs obs/c6288_obs_1.txt \
  --solver "python3 tools/solve_with_rc2.py" \
  --enum-all \
  --max-iters 10
```

Use RC2 (PySAT) as backend solver:

```bash
pip install python-sat

build/doe_maxsat run \
  --bench iscas85/bench/c17.bench \
  --obs obs_c17.txt \
  --solver "python3 tools/solve_with_rc2.py" \
  --out results_c17_rc2.csv \
  --max-iters 2
```

Batch-run all observation files in one folder (one-line summary per observation):

```bash
python3 tools/batch_run_obs_dir.py \
  --bin build/doe_maxsat \
  --bench iscas85/bench/c17.bench \
  --obs-dir obs/c17_1/obs \
  --solver "python3 tools/solve_with_rc2.py" \
  --max-iters 2 \
  --timeout-sec 30 \
  --out results/c17_1_summary.csv
```

输出列（使用 `|` 分隔且按固定宽度对齐）:

- `obs`: observation file name
- `solved`: whether diagnosis succeeded (`1` / `0`)
- `time`: total time for this observation
- `count`: number of diagnoses found
- `size`: average diagnosis size (integer)
- `hits`: number of diagnosis solutions that contain real fault(s)
- `HITrate`: hit rate in percentage
- `timeout`: whether this observation timed out (`1` / `0`)
- `comp`: total unique components across all diagnoses
- `faults`: total unique real faults across all diagnoses

最后会额外输出一行汇总:

```
SUMMARY:
success_rate: 500/500 (100.00%)
avg_hit_rate: (67.50/500) (13.50%)
avg_time: 0.087761s
total_time: 43.880703s
total_components: 200
total_true_faults: 75
```

说明:

- 脚本默认对每个观测执行 `--enum-all`，枚举该观测的所有最优诊断解（在单观测 `30s` 超时内）。
- `HITrate` 计算: (所有诊断解的所有组件集合中的真实故障数) / (所有诊断解的所有组件集合尺寸)
- 汇总 `avg_hit_rate` 计算: (所有观测的命中率之和) / 观测数

## Paper-like run configuration

Script: `scripts/reproduce_iscas85.sh`

It enforces:

- time limit `600s`
- memory limit `4GB`
- DOE iterations `2`

Usage:

```bash
chmod +x scripts/reproduce_iscas85.sh
export SOLVER_CMD="/path/to/open-wbo-inc"
scripts/reproduce_iscas85.sh
```

Or run with RC2 directly:

```bash
chmod +x scripts/reproduce_iscas85.sh
USE_RC2=1 scripts/reproduce_iscas85.sh
```

## Important note on strict reproduction

To strictly match the original paper results, you still need:

- the exact scenario sets used in the paper (if different from regenerated ones),
- the exact solver versions and build flags (`scrypto`, `eva500a`, `open-wbo-inc`, SAT backends),
- the same hardware/runtime environment.

This repository reproduces the DOE method and paper run limits in C++ and supports running the same style of experiments when those external dependencies and scenario files are provided.
