#!/usr/bin/env python3
"""Run the complete D_Code configuration, startup and layout contract suite."""

from __future__ import annotations

import sys
import argparse
import os
from pathlib import Path
import tempfile

sys.dont_write_bytecode = True

from d_code_checks.config_cases import run_config_cases
from d_code_checks.layout_contract import check_layout_contract
from d_code_checks.startup_contract import check_startup_contract
from d_code_checks.syntax_cases import run_syntax_cases


def main() -> int:
    root = Path(__file__).resolve().parents[3]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--group', choices=('all','architecture'), default='all',
                        help='architecture checks declared includes only; it never runs Make or a compiler')
    parser.add_argument("--work-dir", type=Path,
                        default=root / "apps/D_Code/build/out/config_checks")
    parser.add_argument("--keep-going", action="store_true",
                        help="Run all independent groups and retain a failing exit status if any group fails")
    options = parser.parse_args()
    output = options.work_dir.resolve()
    if output == root or not output.is_relative_to(root):
        parser.error("work directory must be inside this project")
    scratch = (output / "temp").resolve()
    if not scratch.is_relative_to(root):
        parser.error("temporary directory must stay inside this project")
    scratch.mkdir(parents=True, exist_ok=True)
    for name in ("TEMP", "TMP", "TMPDIR"):
        os.environ[name] = str(scratch)
    tempfile.tempdir = str(scratch)
    if options.group == 'architecture':
        from d_code_checks.include_contract import check
        from d_code_checks.support import D_CODE, MAKEFILE
        result = check(D_CODE, MAKEFILE, output/'architecture.json')
        print('Declared architecture: ' + str(result['counts']))
        print(result['boundary'])
        return result['exit_code']
    groups = (("layout and Git index", check_layout_contract),
              ("startup", check_startup_contract),
              ("configuration matrix", run_config_cases),
              ("JieLi syntax", run_syntax_cases))
    failures = []
    for name, check in groups:
        try:
            check()
        except (AssertionError, RuntimeError) as error:
            if not options.keep_going:
                raise
            failures.append(name)
            print(f"D_Code {name}: FAIL: {error}", file=sys.stderr, flush=True)
        else:
            print(f"D_Code {name}: PASS", flush=True)
    if failures:
        print("D_Code configuration menu: FAIL: " + ", ".join(failures), file=sys.stderr)
        return 1
    print(
        "D_Code configuration menu: PASS "
        "(10 valid + 7 invalid + exact-once matrix + A->B->A incremental + "
        "6 root UI + legacy Camera/Gallery + AI/WebSocket JieLi syntax)"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, RuntimeError) as error:
        print(f"D_Code configuration menu: FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
