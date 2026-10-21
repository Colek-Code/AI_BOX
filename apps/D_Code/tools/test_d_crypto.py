"""Run production crypto capacity/time contracts with injected SDK boundaries."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[3]
D_CODE = ROOT / "apps/D_Code"
FIXTURE = Path(__file__).resolve().parent / "tests/crypto_host"


def run(options: argparse.Namespace) -> int:
    work = options.work_dir.resolve()
    if not work.is_relative_to(ROOT) or work == ROOT:
        raise ValueError("work directory must stay inside this project")
    stubs = work / "sdk_stubs"
    temp = work / "temp"
    temp.mkdir(parents=True, exist_ok=True)
    environment = os.environ.copy()
    for name in ("TEMP", "TMP", "TMPDIR"):
        environment[name] = str(temp)
    for relative in ("mbedtls/md.h", "mbedtls/pk.h", "mbedtls/sha256.h",
                     "mbedtls/entropy.h", "mbedtls/ctr_drbg.h", "system/includes.h",
                     "system/generic/printf.h"):
        target = stubs / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text('#include "d_crypto_sdk_stub.h"\n', encoding="utf-8")
    (stubs / "config").mkdir(parents=True, exist_ok=True)
    (stubs / "config/app_config.h").write_text(
        "#define CONFIG_MBEDTLS_ENABLE 1\n", encoding="utf-8")
    compiler = shutil.which(options.compiler)
    if compiler is None:
        raise RuntimeError("host C compiler not found: " + options.compiler)
    source = D_CODE / "framework/d_crypto.c"
    test = FIXTURE / "d_crypto_host_tests.c"
    flags = [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
             "-finput-charset=UTF-8", "-I" + str(stubs), "-I" + str(FIXTURE),
             "-I" + str(D_CODE / "framework"), "-I" + str(D_CODE)]
    executable = work / "d_crypto_host_tests.exe"
    build = subprocess.run(flags + [str(source), str(test), "-o", str(executable)],
                           env=environment, capture_output=True, text=True,
                           encoding="utf-8", errors="replace")
    (work / "build.log").write_text(build.stdout + build.stderr, encoding="utf-8")
    if build.returncode:
        raise RuntimeError("host compile failed; see " + str(work / "build.log"))
    result = subprocess.run([str(executable)], env=environment, capture_output=True,
                            text=True, encoding="utf-8", errors="replace")
    (work / "test.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    report = {
        "result": "PASS" if result.returncode == 0 else "FAIL",
        "returncode": result.returncode,
        "boundary": "Production d_crypto.c; injected SDK crypto/time/RNG. No device or cryptographic algorithm acceptance.",
        "source_sha256": {
            path.relative_to(ROOT).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in (source, D_CODE / "framework/d_crypto.h", test,
                         FIXTURE / "d_crypto_sdk_stub.h", Path(__file__).resolve())
        },
        "compiler": compiler,
        "compiler_version": subprocess.check_output([compiler, "--version"],
                            env=environment, text=True).splitlines()[0],
        "command": flags + [str(source), str(test), "-o", str(executable)],
    }
    print(result.stdout, end="")
    if result.returncode == 0 and options.negative_control:
        original = source.read_text(encoding="utf-8")
        guard = "out_capacity < key_bytes * 2U + 1U"
        if original.count(guard) != 1:
            raise ValueError("capacity guard changed; update the negative control")
        altered = work / "d_crypto_without_capacity_guard.c"
        altered.write_text(original.replace(guard, "0"), encoding="utf-8")
        negative_exe = work / "d_crypto_negative_control.exe"
        negative_build = subprocess.run(flags + [str(altered), str(test), "-o", str(negative_exe)],
                                       env=environment, capture_output=True, text=True,
                                       encoding="utf-8", errors="replace")
        (work / "negative_build.log").write_text(
            negative_build.stdout + negative_build.stderr, encoding="utf-8")
        if negative_build.returncode:
            raise RuntimeError("negative control did not compile")
        negative = subprocess.run([str(negative_exe)], env=environment, capture_output=True,
                                  text=True, encoding="utf-8", errors="replace")
        (work / "negative_control.log").write_text(
            negative.stdout + negative.stderr, encoding="utf-8")
        report["negative_control"] = {
            "removed": guard, "returncode": negative.returncode,
            "detected": negative.returncode != 0,
            "summary": negative.stdout.splitlines()[-1],
        }
        if negative.returncode == 0:
            raise RuntimeError("test failed to detect missing capacity guard")
        print("PASS: negative control detects the removed output capacity guard")
    (work / "result.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n",
                                     encoding="utf-8")
    return result.returncode


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work-dir", type=Path,
                        default=D_CODE / "build/out/crypto_tests")
    parser.add_argument("--compiler", default="gcc")
    parser.add_argument("--negative-control", action="store_true")
    try:
        raise SystemExit(run(parser.parse_args()))
    except (ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        print(str(error), file=sys.stderr)
        raise SystemExit(1)
