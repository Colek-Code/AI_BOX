"""Prepare/audit OTA inputs. Never runs isd_download, flashes, erases VM or uploads."""
from pathlib import Path
import argparse
import binascii
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "apps/D_Code/build/out/product_ui_ota"


def owned(path, task=True):
    path = path.resolve()
    boundary = OUT.resolve() if task else (ROOT / "apps/D_Code/build/out").resolve()
    if not path.is_relative_to(boundary) or path == boundary:
        raise ValueError("output must be inside the OTA task directory")
    return path


def replace_field(text, name, value):
    text, count = re.subn(r"(?m)^(" + re.escape(name) + r"\s*=)[^;\r\n]+;", r"\g<1>" + value + ";", text)
    if count != 1:
        raise ValueError("expected one active field: " + name)
    return text


def normalize_config(path, board, bank, keep, boot_version=None):
    path = owned(path, task=False)
    if boot_version is not None and not 0 <= boot_version <= 65535:
        raise ValueError("SDK boot version must fit uint16")
    text = path.read_text(encoding="utf-8")
    if board == 1001:
        # Preserve the existing GC9307 post-build LSB80 setting; this is not
        # an OTA clock change. The SDK template itself remains read-only.
        text = replace_field(text, "LSB_DIV", "1")
    if bank:
        if board != 1001 or not re.search(r"(?m)^BR22_TWS_DB=YES;", text):
            raise ValueError("not the approved GC9307 dual-bank configuration")
        if not re.search(r"(?m)^FLASH_SIZE=(?:16M|0x1000000|16777216);", text):
            raise ValueError("generated Flash size does not describe the 16 MB board")
        text = replace_field(text, "VM_OPT", str(keep))
        if boot_version is not None:
            text = replace_field(text, "BR22_TWS_VERSION", str(boot_version))
    path.write_text(text, encoding="utf-8", newline="\n")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate_ufw(path):
    raw = path.read_bytes()
    if len(raw) < 256 or len(raw) > 16 * 1024 * 1024:
        raise ValueError("invalid UFW length")
    for start in (0, 32):
        if binascii.crc_hqx(raw[start+2:start+32], 0) != struct.unpack_from("<H", raw, start)[0]:
            raise ValueError("UFW header CRC mismatch")
    if (raw[16:28] != b"update_data\0" or raw[48:57] != b"app_core\0" or
        struct.unpack_from("<H", raw, 14)[0] != 1 or struct.unpack_from("<H", raw, 46)[0] != 0 or
        struct.unpack_from("<II", raw, 4) != (32, len(raw)) or
        struct.unpack_from("<II", raw, 36) != (64, len(raw)-64)):
        raise ValueError("not a normal single app_core OTA package")
    if binascii.crc_hqx(raw[32:], 0) != struct.unpack_from("<H", raw, 2)[0]:
        raise ValueError("UFW body CRC mismatch")
    if binascii.crc_hqx(raw[64:], 0) != struct.unpack_from("<H", raw, 34)[0]:
        raise ValueError("UFW app_core CRC mismatch")
    return {"size": len(raw), "sha256": digest(path)}


def validate_vm_candidate(address, length):
    if (address is None) != (length is None):
        raise ValueError("--vm-address and --vm-length must be supplied together")
    if address is None:
        return
    if address % 4096 or length % 4096 or length <= 0:
        raise ValueError("explicit VM candidate requires aligned address and positive length")
    if address < 0 or address + length > 16 * 1024 * 1024:
        raise ValueError("VM candidate leaves Flash")


def vm_config_values(text):
    values = {}
    for name in ("VM_ADR", "VM_LEN", "VM_OPT"):
        matches = re.findall(r"(?m)^" + name + r"\s*=\s*([^;\r\n]+);", text)
        if len(matches) != 1:
            raise ValueError("expected one generated VM field: " + name)
        value = matches[0].strip()
        if name == "VM_ADR" and value.upper() == "AUTO":
            values[name] = value.upper()
            continue
        multiplier = 1
        if value[-1:].upper() in ("K", "M"):
            multiplier = 1024 if value[-1:].upper() == "K" else 1024 * 1024
            value = value[:-1]
        values[name] = int(value, 16 if value.lower().startswith("0x") else 10) * multiplier
    return values


def release_settings(config):
    """Read effective C-preprocessed settings, never scrape commented macros."""
    try:
        keep = int(config["D_BUILD_OTA_KEEP_VM"])
        boot = int(config["D_BUILD_OTA_BOOT_VERSION"], 0)
        version = json.loads(config["D_BUILD_FIRMWARE_VERSION"])
    except (KeyError, TypeError, ValueError) as error:
        raise ValueError("build receipt lacks valid release settings; rebuild current sources") from error
    if keep not in (0, 1) or not 0 <= boot <= 65535:
        raise ValueError("invalid OTA VM policy or SDK boot version")
    if not isinstance(version, str) or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._+-]{0,63}", version):
        raise ValueError("D_FIRMWARE_VERSION must be a filename-safe version, at most 64 characters")
    return {"keep_vm": keep, "boot_version": boot, "firmware_version": version}


def prepare(args):
    # Reject incomplete/invalid layout input before creating any output files.
    validate_vm_candidate(args.vm_address, args.vm_length)
    output = owned(args.output)
    receipt_path = args.build_result.resolve()
    receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
    config = receipt["config"]
    if receipt["exit_code"] or config.get("D_BUILD_BOARD_TARGET") != "1001" or config.get("D_BUILD_FEATURE_OTA") != "1":
        raise ValueError("a passing GC9307 OTA build is required")
    settings = release_settings(config)
    keep = settings["keep_vm"]
    if args.keep_vm is not None and args.keep_vm != keep:
        raise ValueError("KEEP_VM conflicts with the compiled D_OTA_KEEP_VM")
    if args.boot_version is not None and args.boot_version != settings["boot_version"]:
        raise ValueError("--boot-version conflicts with the compiled D_OTA_BOOT_VERSION")
    elf = receipt_path.with_name("sdk.elf")
    if digest(elf) != receipt["elf_sha256"]:
        raise ValueError("ELF does not match build receipt")
    ini = receipt_path.with_name("isd_config.ini")
    if not ini.is_file() or receipt.get("ota_config_sha256") != digest(ini):
        raise ValueError("rebuild with the OTA configuration receipt enabled")
    output.mkdir(parents=True, exist_ok=True)
    if (output / "release_inputs.json").exists():
        raise ValueError("release inputs are frozen; choose a new reviewed release directory")
    for item in output.rglob("*"):
        if item.is_symlink() or item.is_junction():
            raise ValueError("release output contains a filesystem link")
    temporary = output / "tmp"; temporary.mkdir(exist_ok=True)
    environment = os.environ | {key: str(temporary) for key in ("TEMP", "TMP", "TMPDIR")}
    chunks = []
    for section in (".text", ".data", ".dynamic_data", ".ram0_data", ".cache_ram_data"):
        destination = temporary / (section[1:] + ".bin")
        subprocess.run(["C:/JL/pi32/bin/llvm-objcopy.exe", "-O", "binary", "-j", section,
                        str(elf), str(destination)], env=environment, check=True, capture_output=True)
        chunks.append(destination.read_bytes())
    (output / "app.bin").write_bytes(b"".join(chunks))
    for section in ("text", "data", "dynamic_data", "ram0_data", "cache_ram_data"):
        owned(temporary / (section + ".bin")).unlink()
    text = ini.read_text(encoding="utf-8")
    text = replace_field(text, "BR22_TWS_VERSION", str(settings["boot_version"]))
    if args.vm_address is not None:
        text = replace_field(text, "VM_ADR", hex(args.vm_address))
        text = replace_field(text, "VM_LEN", hex(args.vm_length))
    (output / "isd_config.ini").write_text(text, encoding="utf-8", newline="\n")
    effective_vm = vm_config_values((output / "isd_config.ini").read_text(encoding="utf-8"))
    if effective_vm["VM_OPT"] != keep or (args.vm_address is not None and
        (effective_vm["VM_ADR"] != args.vm_address or effective_vm["VM_LEN"] != args.vm_length)):
        raise ValueError("generated VM configuration differs from requested policy/layout")
    vendor = ROOT / "cpu/wl82/tools"
    # isd_download also reads these SDK inputs implicitly from its working directory.
    for name in ("uboot.boot", "cfg_tool.bin", "ota.bin", "wl82loader.bin", "sd_update2.bin", "usb_update2.bin"):
        shutil.copyfile(vendor / name, output / name)
    for name in ("audlogo", "cfg"):
        shutil.copytree(vendor / name, output / name, dirs_exist_ok=True)
    inputs = {p.relative_to(output).as_posix(): digest(p) for p in output.rglob("*")
              if p.is_file() and "tmp" not in p.relative_to(output).parts and p.name not in ("release_inputs.json", "USER_FLASH.ps1")}
    record = {"board": 1001, **settings,
        "elf_sha256": receipt["elf_sha256"], "build_receipt": str(receipt_path),
        "vm_address_candidate": args.vm_address, "vm_length_candidate": args.vm_length,
        "effective_vm_config": effective_vm,
        "layout_and_vm_device_acceptance": "PENDING", "inputs": inputs,
        "source_sha256": receipt["source_sha256"],
        "build_tools_sha256": receipt.get("build_tools_sha256", {}),
        "downloader_sha256": digest(vendor / "isd_download.exe")}
    (output / "release_inputs.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    quote = lambda value: "'" + str(value).replace("'", "''") + "'"
    lines = ["param([switch]$Flash)", "$ErrorActionPreference='Stop'",
        "if (-not $Flash) { throw 'This USER-ONLY command writes the board. Review layout and VM policy first; pass -Flash explicitly.' }",
        "$root=$PSScriptRoot", "$r=Get-Content -LiteralPath (Join-Path $root 'release_inputs.json') -Raw | ConvertFrom-Json",
        "foreach($p in $r.inputs.PSObject.Properties) { if((Get-FileHash -LiteralPath (Join-Path $root $p.Name) -Algorithm SHA256).Hash.ToLower() -ne $p.Value){throw 'Release input changed'} }",
        "$tool=" + quote(vendor / "isd_download.exe"),
        "if((Get-FileHash -LiteralPath $tool -Algorithm SHA256).Hash.ToLower() -ne $r.downloader_sha256){throw 'Downloader changed'}",
        "if(Test-Path -LiteralPath (Join-Path $root 'db_update_files_data.bin')){throw 'Old package exists; do not reuse a completed release directory'}",
        "Push-Location -LiteralPath $root", "try {",
        "  & $tool isd_config.ini -gen2 -tonorflash -dev wl82 -boot 0x1c02000 -div1 -wait 300 -uboot uboot.boot -app app.bin cfg_tool.bin -res audlogo cfg -reboot 500 -update_files normal -extend-bin",
        "  if($LASTEXITCODE -ne 0){throw 'SDK download/package command failed'}", "} finally { Pop-Location }"]
    (output / "USER_FLASH.ps1").write_text("\n".join(lines) + "\n", encoding="utf-8-sig")
    print("Prepared inputs only; USER_FLASH.ps1 was NOT executed. Layout/VM acceptance remains pending.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--normalize-config", type=Path)
    parser.add_argument("--board", type=int, default=1001)
    parser.add_argument("--bank", type=int, choices=(0, 1), default=1)
    parser.add_argument("--keep-vm", type=int, choices=(0, 1))
    parser.add_argument("--build-result", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--boot-version", type=lambda value: int(value, 0),
                        help="Optional consistency check against D_OTA_BOOT_VERSION; not an override")
    parser.add_argument("--vm-address", type=lambda value: int(value, 0))
    parser.add_argument("--vm-length", type=lambda value: int(value, 0))
    parser.add_argument("--verify-ufw", type=Path)
    args = parser.parse_args()
    if args.verify_ufw:
        print(json.dumps(validate_ufw(args.verify_ufw.resolve()), indent=2)); return
    if args.normalize_config:
        if args.keep_vm is None: parser.error("--keep-vm is required")
        normalize_config(args.normalize_config, args.board, args.bank, args.keep_vm, args.boot_version); return
    if not args.build_result or not args.output:
        parser.error("--build-result and --output are required")
    prepare(args)


if __name__ == "__main__":
    main()
