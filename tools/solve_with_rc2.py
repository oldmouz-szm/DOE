#!/usr/bin/env python3
import argparse
import sys
import time

from pysat.formula import WCNF
from pysat.examples.rc2 import RC2


def main() -> int:
    parser = argparse.ArgumentParser(description="Solve WCNF using PySAT RC2")
    parser.add_argument("wcnf", help="Path to WCNF file")
    parser.add_argument("--solver", default="g3", help="Underlying SAT solver for RC2 (default: g3)")
    args = parser.parse_args()

    t0 = time.perf_counter()
    formula = WCNF(from_file=args.wcnf)

    try:
        with RC2(formula, solver=args.solver) as rc2:
            model = rc2.compute()
            cost = rc2.cost
    except Exception as exc:
        print(f"c RC2 failed: {exc}")
        return 2

    t1 = time.perf_counter()

    # MaxSAT-style output lines compatible with the existing C++ parser.
    if model is None:
        print("s UNKNOWN")
        return 1

    print("s OPTIMUM FOUND")
    print(f"o {cost}")
    # Output model in DIMACS-like style for debugging/parity with SAT tools.
    model_line = "v " + " ".join(str(l) for l in model) + " 0"
    print(model_line)
    print(f"c wall_time_sec {t1 - t0:.6f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
