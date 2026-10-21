"""Bind the pinned XiaoLe object's free calls to its lifetime adapter.

The delivered vendor archive stays unchanged. JieLi's own bitcode rename tool
changes one undefined symbol in WakeupApi.o; all other members are preserved.
Unknown input versions or missing tools fail the build instead of linking the
unsafe original. Output belongs in the caller's build directory.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

SUPPORTED_SHA256 = "b8472b701f4e37e833636cc92ac0d1685c165d8f5dfde2d1830b7740fba7e009"
MEMBER = "WakeupApi.o"


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(command: list[str], environment: dict[str, str]) -> bytes:
    result = subprocess.run(command, capture_output=True, env=environment, timeout=60)
    if result.returncode:
        raise RuntimeError(result.stderr.decode("utf-8", errors="replace")[-4000:])
    return result.stdout


def prepare(library: Path, output: Path, tool_dir: Path) -> dict:
    library, output = library.resolve(), output.resolve()
    if library == output:
        raise ValueError("XiaoLe adapter output must not overwrite the vendor archive")
    source_hash = digest(library)
    if source_hash != SUPPORTED_SHA256:
        raise ValueError("Unsupported XiaoLe library SHA256 " + source_hash +
                         "; review worker/buffer ownership before updating the adapter pin")
    suffix = ".exe" if os.name == "nt" else ""
    tools = {name: (tool_dir / ("llvm-" + name + suffix)).resolve() for name in ("ar", "nm", "bcrename")}
    tools["clang"] = (tool_dir / ("clang" + suffix)).resolve()
    for tool in tools.values():
        if not tool.is_file(): raise FileNotFoundError("Required JieLi tool: " + str(tool))
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.parent / "tmp"
    temporary.mkdir(exist_ok=True)
    environment = os.environ | {"TEMP": str(temporary), "TMP": str(temporary), "TMPDIR": str(temporary)}
    original = temporary / "WakeupApi.original.o"
    rebound = temporary / MEMBER
    original.write_bytes(run([str(tools["ar"]), "p", str(library), MEMBER], environment))
    if original.read_bytes()[:4] != b"BC\xc0\xde":
        raise ValueError("Pinned WakeupApi.o is not the expected LLVM bitcode")
    run([str(tools["bcrename"]), "-rename-list=free:xiaole_sdk_free", "-o=" + str(rebound), str(original)], environment)
    # Unlike free(), the deferred-release adapter intentionally retains the pointer.
    # Preserve every IR instruction, but remove the now-false nocapture parameter promise.
    ir_path = temporary / "WakeupApi.ll"
    checked_ir = temporary / "WakeupApi.checked.ll"
    ir_flags = [str(tools["clang"]), "-emit-llvm", "-target", "pi32v2", "-mcpu=r3", "-x", "ir"]
    run([*ir_flags, "-S", str(rebound), "-o", str(ir_path)], environment)
    text = ir_path.read_text(encoding="utf-8")
    fixed, count = re.subn(r"(?m)^(declare void @xiaole_sdk_free\(i8\*) nocapture(\).*)$", r"\1\2", text)
    if count != 1:
        raise ValueError("Unexpected XiaoLe free declaration; cannot establish pointer capture contract")
    ir_path.write_text(fixed, encoding="utf-8", newline="\n")
    run([*ir_flags, "-c", str(ir_path), "-o", str(rebound)], environment)
    run([*ir_flags, "-S", str(rebound), "-o", str(checked_ir)], environment)
    normalize = lambda value: re.sub(r"(?m)^; ModuleID = .*\n", "", value).replace("\r\n", "\n")
    if normalize(checked_ir.read_text(encoding="utf-8")) != normalize(fixed):
        raise ValueError("Unexpected IR change beyond XiaoLe allocator name/capture contract")
    before = run([str(tools["nm"]), str(original)], environment).decode("utf-8").splitlines()
    after = run([str(tools["nm"]), str(rebound)], environment).decode("utf-8").splitlines()
    before = sorted(line.strip() for line in before if line.strip())
    after = sorted(line.strip() for line in after if line.strip())
    expected = sorted(line[:-4] + "xiaole_sdk_free" if line.split() == ["U", "free"] else line for line in before)
    if not any(line.split() == ["U", "free"] for line in before) or after != expected:
        raise ValueError("Unexpected symbol changes in XiaoLe bitcode rebind")
    candidate = temporary / "liblx_aec_safe.a"
    shutil.copyfile(library, candidate)
    run([str(tools["ar"]), "rcs", str(candidate), str(rebound)], environment)
    members = run([str(tools["ar"]), "t", str(library)], environment).decode("utf-8").splitlines()
    final_members = run([str(tools["ar"]), "t", str(candidate)], environment).decode("utf-8").splitlines()
    if members != final_members:
        raise ValueError("XiaoLe archive member list changed")
    member_hashes = {}
    for member in members:
        old = run([str(tools["ar"]), "p", str(library), member], environment)
        new = run([str(tools["ar"]), "p", str(candidate), member], environment)
        if member != MEMBER and old != new:
            raise ValueError("Unrelated XiaoLe member changed: " + member)
        if member == MEMBER and new != rebound.read_bytes():
            raise ValueError("Rebound XiaoLe object missing from archive")
        member_hashes[member] = hashlib.sha256(new).hexdigest()
    os.replace(candidate, output)
    record = {"input_sha256": source_hash, "output_sha256": digest(output), "member": MEMBER,
              "binding": {"free": "xiaole_sdk_free"}, "deferred_pointer_capture_allowed": True,
              "member_sha256": member_hashes,
              "helper_sha256": digest(Path(__file__)),
              "tool_sha256": {name: digest(path) for name, path in tools.items()}}
    output.with_suffix(".json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    # These two bitcode files are reproducible intermediates under this output's tmp directory.
    original.unlink()
    rebound.unlink()
    ir_path.unlink()
    checked_ir.unlink()
    return record


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--tool-dir", type=Path, required=True)
    args = parser.parse_args()
    record = prepare(args.library, args.output, args.tool_dir)
    print("XiaoLe scoped free binding:", record["output_sha256"])


if __name__ == "__main__":
    main()
