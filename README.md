# DOE MaxSAT (IJCAI 2015) C++ Reproduction

This project implements the Dominator-Oriented Encoding (DOE) described in:

- J. Marques-Silva, M. Janota, A. Ignatiev, A. Morgado, "Efficient Model Based Diagnosis with Maximum Satisfiability", IJCAI 2015.

## What is implemented

- ISCAS `.bench` parser.
- Observation/scenario parser and generator.
- DOE preprocessing loop (bounded by `--max-iters`, default `2`):
  - dominators,
  - dominated gates as hard components,
  - backbone-node propagation on dominated components,
  - blocked-edge detection,
  - filtered nodes/edges fixpoint.
- MaxSAT encoding (WCNF):
  - hard clauses from SD and observation,
  - soft unit clauses `¬Ab(c)` for non-dominated components.
- Solver runner for external MaxSAT solvers (e.g., `open-wbo-inc`, `eva500a`).

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Binary:

- `build/doe_maxsat`

## Observation format

```text
SCENARIO s0
IN 1=1 2=0 3=1 6=1 7=1
OUT 22=0 23=1
END
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

Run solver directly:

```bash
build/doe_maxsat run \
  --bench iscas85/bench/c17.bench \
  --obs obs_c17.txt \
  --solver "/path/to/open-wbo-inc" \
  --out results_c17.csv \
  --max-iters 2
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

## Paper-like run configuration

Script:

- `scripts/reproduce_iscas85.sh`

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
