#!/usr/bin/env python3
import argparse
import sys
import time

from pysat.formula import WCNF
from pysat.examples.rc2 import RC2
from pysat.solvers import Solver
from pysat.card import CardEnc


def main() -> int:
    parser = argparse.ArgumentParser(description="Solve WCNF using PySAT RC2")
    parser.add_argument("wcnf", help="Path to WCNF file")
    parser.add_argument("--solver", default="g3", help="Underlying SAT solver for RC2 (default: g3)")
    parser.add_argument("--enum-all", action="store_true", help="Enumerate all minimum-cost diagnoses")
    parser.add_argument("--enum-limit", type=int, default=0, help="Max number of diagnoses to enumerate (0=unlimited)")
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

    if model is None:
        print("s UNKNOWN")
        return 1

    print("s OPTIMUM FOUND")
    print(f"o {cost}")
    model_line = "v " + " ".join(str(l) for l in model) + " 0"
    print(model_line)
    print(f"c wall_time_sec {t1 - t0:.6f}")

    if not args.enum_all or cost == 0:
        print(f"c num_diagnoses 1")
        return 0

    soft_vars = []
    soft_var_set = set()
    for clause in formula.soft:
        for lit in clause:
            v = abs(lit)
            if v not in soft_var_set:
                soft_vars.append(v)
                soft_var_set.add(v)

    diag_count = 1

    if args.enum_limit > 0 and diag_count >= args.enum_limit:
        print(f"c num_diagnoses {diag_count}")
        return 0

    model_set = set(model)
    diag_lits = sorted([v for v in soft_vars if v in model_set])
    k = len(diag_lits)

    if k == 0:
        print(f"c num_diagnoses {diag_count}")
        return 0

    with Solver(name=args.solver, bootstrap_with=formula.hard) as sat:
        card_lits = CardEnc.equals(lits=soft_vars, bound=k, top_id=formula.nv)
        for clause in card_lits.clauses:
            sat.add_clause(clause)

        sat.add_clause([-v for v in diag_lits])

        while sat.solve():
            next_model = sat.get_model()
            next_model_set = set(next_model)
            next_diag = sorted([v for v in soft_vars if v in next_model_set])

            diag_count += 1
            model_line = "v " + " ".join(str(l) for l in next_model) + " 0"
            print(model_line)

            if args.enum_limit > 0 and diag_count >= args.enum_limit:
                break

            sat.add_clause([-v for v in next_diag])

    print(f"c num_diagnoses {diag_count}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
