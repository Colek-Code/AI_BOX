"""核对 AC7911 供应商 SDK 与按相同接口编译的项目接入库。"""
import hashlib
import json
from pathlib import Path

AC7911_MANIFEST_SHA256 = '9328523f2e159100a3b0e2ccebaf8e4d13e9f6c24a6c93821daa54a0f890daaa'

def verify_sdk(qwen_dir: Path, access_binding: dict | None = None) -> dict:
    """逐项校验发行包摘要。传入接入库绑定时，同时检查它记录的 SDK 版本与清单。"""
    sdk = qwen_dir / "sdk"
    manifest = (qwen_dir / "lib/sdk.binding.json").read_bytes()
    if hashlib.sha256(manifest).hexdigest() != AC7911_MANIFEST_SHA256:
        raise ValueError("Qwen SDK manifest differs from the reviewed AC7911 release")
    record = json.loads(manifest)
    if record.get("platform") != "AC7911" or record.get("version") != "1.3.1":
        raise ValueError("Unexpected Qwen SDK platform or version")
    actual = {p.relative_to(sdk).as_posix() for p in sdk.rglob('*') if p.is_file()}
    if actual != set(record["files"]):
        raise ValueError("Qwen SDK file set differs from the release manifest")
    for relative, required in record["files"].items():
        file = (sdk / relative).resolve()
        if not file.is_relative_to(sdk.resolve()) or not file.is_file():
            raise ValueError("Missing Qwen SDK input: " + relative)
        if hashlib.sha256(file.read_bytes()).hexdigest() != required:
            raise ValueError("Changed Qwen SDK input: " + relative)
    if access_binding is not None and (
        access_binding.get("sdk_version") != record["version"]
        or access_binding.get("sdk_manifest_sha256") != hashlib.sha256(manifest).hexdigest()
    ):
        raise ValueError("Qwen access archive was built against a different SDK")
    return record
