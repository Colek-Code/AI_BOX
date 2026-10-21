#!/usr/bin/env python3
"""Build deterministic Noto Sans SC LVGL subsets from the pinned local source."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import tempfile
import unicodedata
from pathlib import Path


OUTPUT_PREFIX = "product_ui_font_noto_sans_sc"
PAYLOAD_LAYOUT = "v2"
PAYLOAD_MAX_LINES = 260
PAYLOAD_HARD_MAX_LINES = 300


class AssetInputError(RuntimeError):
    """Raised when a pinned asset input is missing or does not match metadata."""


def load_manifest(assets_dir: Path) -> dict:
    path = assets_dir / "ASSET_MANIFEST"
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as error:
        raise AssetInputError(f"missing asset manifest: {path}") from error
    except json.JSONDecodeError as error:
        raise AssetInputError(f"invalid asset manifest {path}: {error}") from error

    if manifest.get("schema_version") != 1:
        raise AssetInputError("unsupported ASSET_MANIFEST schema_version")
    policy = manifest.get("policy", {})
    if (policy.get("missing_resource") != "error" or
            policy.get("fallback_icon") is not None or
            policy.get("forbidden_fallback_icon") != "settings"):
        raise AssetInputError(
            "asset policy must fail missing resources and must not alias settings")
    return manifest


def font_config(manifest: dict) -> dict:
    config = manifest.get("noto_sans_sc")
    if not isinstance(config, dict):
        raise AssetInputError("manifest is missing noto_sans_sc metadata")
    required = (
        "version", "commit", "source_path", "source_url", "local_path",
        "source_size", "source_git_blob_sha1", "license", "license_url",
        "inventory", "inventory_scan_root", "sizes", "bpp", "ascii_range",
        "network_fetch",
    )
    missing = [name for name in required if name not in config]
    if missing:
        raise AssetInputError("incomplete noto_sans_sc metadata: " + ", ".join(missing))
    if config["network_fetch"] is not False:
        raise AssetInputError("font pipeline must not fetch source files")
    if config["license"] != "OFL-1.1":
        raise AssetInputError("unexpected Noto Sans SC license metadata")
    if config["commit"] not in config["source_url"]:
        raise AssetInputError("font source URL is not pinned to the manifest commit")
    if config["source_path"] not in config["source_url"]:
        raise AssetInputError("font source URL does not match source_path")
    if config["sizes"] != [10, 12, 14, 18]:
        raise AssetInputError("font sizes must be exactly 10, 12, 14, and 18")
    if config["bpp"] != 4 or config["ascii_range"].lower() != "0x20-0x7e":
        raise AssetInputError("font subset format must be A4 with printable ASCII")
    return config


def git_blob_sha1(data: bytes) -> str:
    header = f"blob {len(data)}\0".encode("ascii")
    return hashlib.sha1(header + data).hexdigest()


def validate_source(assets_dir: Path, config: dict) -> Path:
    source = assets_dir / config["local_path"]
    if not source.is_file():
        raise AssetInputError(
            f"missing pinned font source: {source}\n"
            f"expected Noto Sans SC {config['version']} from {config['source_url']}\n"
            "the pipeline does not download fonts; place the exact pinned file there")
    data = source.read_bytes()
    if len(data) != config["source_size"]:
        raise AssetInputError(
            f"font source size mismatch for {source}: "
            f"expected {config['source_size']}, got {len(data)}")
    digest = git_blob_sha1(data)
    if digest != config["source_git_blob_sha1"]:
        raise AssetInputError(
            f"font source Git blob mismatch for {source}: "
            f"expected {config['source_git_blob_sha1']}, got {digest}")
    return source


def load_inventory(assets_dir: Path, config: dict) -> tuple[list[str], str]:
    path = assets_dir / config["inventory"]
    try:
        raw_lines = path.read_text(encoding="utf-8").splitlines()
    except FileNotFoundError as error:
        raise AssetInputError(f"missing font string inventory: {path}") from error

    strings: list[str] = []
    seen: set[str] = set()
    for line_number, raw in enumerate(raw_lines, 1):
        if not raw or raw.startswith("#"):
            continue
        normalized = unicodedata.normalize("NFC", raw)
        if normalized != raw:
            raise AssetInputError(
                f"inventory line {line_number} is not NFC-normalized")
        if any(ord(char) < 0x20 for char in raw):
            raise AssetInputError(
                f"inventory line {line_number} contains a control character")
        if raw in seen:
            raise AssetInputError(
                f"duplicate font inventory string at line {line_number}: {raw!r}")
        seen.add(raw)
        strings.append(raw)

    if not strings:
        raise AssetInputError(f"font string inventory is empty: {path}")
    symbols = "".join(chr(codepoint) for codepoint in range(0x20, 0x7F))
    symbols += "".join(strings)
    symbols = "".join(sorted(set(symbols), key=ord))
    return strings, symbols


def string_literal_non_ascii(text: str) -> set[str]:
    """Return direct non-ASCII characters from C string literals only."""
    result: set[str] = set()
    index = 0
    state = "code"
    while index < len(text):
        char = text[index]
        following = text[index + 1] if index + 1 < len(text) else ""
        if state == "code":
            if char == "/" and following == "/":
                state = "line_comment"
                index += 2
                continue
            if char == "/" and following == "*":
                state = "block_comment"
                index += 2
                continue
            if char == '"':
                state = "string"
            elif char == "'":
                state = "character"
        elif state == "line_comment":
            if char == "\n":
                state = "code"
        elif state == "block_comment":
            if char == "*" and following == "/":
                state = "code"
                index += 2
                continue
        elif state in ("string", "character"):
            delimiter = '"' if state == "string" else "'"
            if char == "\\":
                index += 2
                continue
            if char == delimiter:
                state = "code"
            elif state == "string" and ord(char) >= 0x80:
                result.add(char)
        index += 1
    return result


def audit_inventory_coverage(assets_dir: Path, config: dict,
                             symbols: str) -> None:
    scan_root = (assets_dir / config["inventory_scan_root"]).resolve()
    expected_root = assets_dir.parent.resolve()
    if scan_root != expected_root:
        raise AssetInputError(
            f"inventory_scan_root must resolve to {expected_root}, got {scan_root}")
    covered = set(symbols)
    missing_by_file: dict[Path, list[str]] = {}
    source_files = sorted((*scan_root.rglob("*.c"), *scan_root.rglob("*.h")))
    for path in source_files:
        characters = string_literal_non_ascii(path.read_text(encoding="utf-8"))
        missing = sorted(characters - covered, key=ord)
        if missing:
            missing_by_file[path] = missing
    if missing_by_file:
        details = "; ".join(
            f"{path.relative_to(scan_root)}={''.join(chars)}"
            for path, chars in missing_by_file.items())
        raise AssetInputError(
            "font inventory is missing Product UI string glyphs: " + details)


def resolve_converter(explicit: Path | None,
                      expected_version: str) -> list[str]:
    candidate = str(explicit) if explicit else shutil.which("lv_font_conv")
    if not candidate:
        raise AssetInputError(
            f"missing lv_font_conv {expected_version}; install that exact version "
            "or pass --converter")
    path = Path(candidate)
    command = [str(path)]
    if path.suffix.lower() == ".js":
        node = shutil.which("node")
        if not node or not path.is_file():
            raise AssetInputError(f"cannot run explicit Node converter: {path}")
        command = [node, str(path)]
    elif os.name == "nt" and path.suffix.lower() in (".cmd", ".bat"):
        node = shutil.which("node")
        if path.parent.name.casefold() == ".bin":
            package_dir = path.parent.parent / "lv_font_conv"
        else:
            package_dir = path.parent / "node_modules" / "lv_font_conv"
        script = package_dir / "lv_font_conv.js"
        if not node or not script.is_file():
            raise AssetInputError(
                f"cannot resolve Node entry behind Windows wrapper: {path}")
        command = [node, str(script)]
    result = subprocess.run(
        [*command, "--version"], check=False, capture_output=True)
    output = (result.stdout + result.stderr).decode(errors="replace")
    if result.returncode != 0 or not re.search(
            rf"(?<![0-9.]){re.escape(expected_version)}(?![0-9.])", output):
        raise AssetInputError(
            f"lv_font_conv version mismatch: expected {expected_version}, "
            f"command reported {output.strip() or 'no version'}")
    return command


def output_name(size: int) -> str:
    return f"{OUTPUT_PREFIX}_{size}.c"


def payload_name(size: int, part: int) -> str:
    return f"{OUTPUT_PREFIX}_{size}_{PAYLOAD_LAYOUT}_part_{part:02d}.inc"


def font_enable_macro(size: int) -> str:
    return f"{OUTPUT_PREFIX}_{size}".upper()


def wrapper_text(size: int, payloads: list[str]) -> str:
    macro = font_enable_macro(size)
    header = (
        "/* Generated by build_product_ui_fonts.py. Do not edit. */\n"
        "/* Deterministic glyph data is split into bounded .inc assets. */\n"
        f"#ifndef {macro}\n"
        f"#define {macro} 1\n"
        "#endif\n"
        f"#if {macro}\n"
    )
    includes = "".join(f'#include "{name}"\n' for name in payloads)
    return header + includes + f"#endif /* {macro} */\n"


def strip_outer_font_guard(text: str, size: int) -> str:
    macro = font_enable_macro(size)
    prefix = re.compile(
        rf"(?m)^#ifndef {macro}\n#define {macro} 1\n#endif\n\n"
        rf"#if {macro}\n\n")
    suffix = re.compile(rf"(?m)^#endif /\*#if {macro}\*/\s*$")
    body, prefix_count = prefix.subn("", text, count=1)
    body, suffix_count = suffix.subn("", body, count=1)
    if prefix_count != 1 or suffix_count != 1:
        raise AssetInputError(
            f"generated font guard for size {size} is not in the expected form")
    return body.rstrip() + "\n"


def preprocessor_depth_delta(line: str) -> int:
    directive = line.lstrip()
    if re.match(r"^#\s*(if|ifdef|ifndef)\b", directive):
        return 1
    if re.match(r"^#\s*endif\b", directive):
        return -1
    return 0


def split_payload_lines(lines: list[str]) -> list[list[str]]:
    """Split generated C payload on line budgets without cutting /* */ comments."""
    parts: list[list[str]] = []
    current: list[str] = []
    depth = 0
    in_block_comment = False
    for line in lines:
        current.append(line)
        depth += preprocessor_depth_delta(line)
        if depth < 0:
            raise AssetInputError("generated font has an unmatched #endif")

        # Track C block comments so a multi-line banner is never split mid-/* */.
        i = 0
        while i < len(line):
            if in_block_comment:
                end = line.find("*/", i)
                if end < 0:
                    break
                in_block_comment = False
                i = end + 2
                continue
            start = line.find("/*", i)
            if start < 0:
                break
            end = line.find("*/", start + 2)
            if end < 0:
                in_block_comment = True
                break
            i = end + 2

        if (len(current) >= PAYLOAD_MAX_LINES and depth == 0 and
                not in_block_comment):
            parts.append(current)
            current = []
        if len(current) > PAYLOAD_HARD_MAX_LINES:
            raise AssetInputError(
                "generated font preprocessor block exceeds payload line budget")
    if depth != 0:
        raise AssetInputError("generated font has an unterminated conditional")
    if in_block_comment:
        raise AssetInputError("generated font has an unterminated /* comment")
    if current:
        parts.append(current)
    return parts


def write_payloads(output_dir: Path, size: int, text: str) -> list[str]:
    lines = text.splitlines(keepends=True)
    if not lines:
        raise AssetInputError("generated font payload is empty")
    names: list[str] = []
    for part, payload_lines in enumerate(split_payload_lines(lines)):
        name = payload_name(size, part)
        path = output_dir / name
        payload = "".join(payload_lines)
        while payload.endswith("\n\n"):
            payload = payload[:-1]
        path.write_text(
            payload,
            encoding="utf-8", newline="\n")
        names.append(name)

    expected = set(names)
    for stale in output_dir.glob(f"{OUTPUT_PREFIX}_{size}_*.inc"):
        if stale.name not in expected:
            stale.unlink()
    legacy = output_dir / f"{OUTPUT_PREFIX}_{size}.inc"
    if legacy.exists():
        legacy.unlink()
    return names


def normalize_generated_text(text: str, config: dict,
                             symbols: str, size: int) -> str:
    fallback_sizes = {
        10: 12,
        12: 12,
        14: 14,
        18: 18,
    }
    symbols_digest = hashlib.sha256(symbols.encode("utf-8")).hexdigest()
    canonical = (
        f" * Opts: pinned-commit={config['commit']} "
        f"symbols-sha256={symbols_digest} size={size} bpp={config['bpp']}"
    )
    normalized, count = re.subn(
        r"(?m)^ \* Opts: .*$", canonical, text, count=1)
    if count != 1:
        raise AssetInputError("generated font is missing the Opts header")
    fallback_size = fallback_sizes.get(size)
    if fallback_size is None:
        raise AssetInputError(f"missing LVGL symbol fallback for {size}px font")
    normalized, count = re.subn(
        r"(?m)^    \.fallback = NULL,$",
        f"    .fallback = &lv_font_montserrat_{fallback_size},",
        normalized, count=1)
    if count != 1:
        raise AssetInputError("generated font is missing the fallback field")
    return normalized


def build_into(source: Path, symbols: str, config: dict,
               converter: list[str], output_dir: Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    environment = os.environ.copy()
    environment.update({"LC_ALL": "C", "TZ": "UTC", "SOURCE_DATE_EPOCH": "0"})
    for size in config["sizes"]:
        with tempfile.TemporaryDirectory(
                prefix=f"product-ui-font-{size}-") as temporary_name:
            output = Path(temporary_name) / output_name(size)
            command = [
                *converter,
                "--font", str(source),
                "--symbols", symbols,
                "--size", str(size),
                "--format", "lvgl",
                "--bpp", str(config["bpp"]),
                "--no-compress",
                "--no-kerning",
                "--lv-include", "lvgl.h",
                "--output", str(output),
            ]
            subprocess.run(command, check=True, env=environment)
            if not output.is_file() or output.stat().st_size == 0:
                raise AssetInputError(f"font converter did not create {output}")
            text = output.read_text(encoding="utf-8").replace("\r\n", "\n")
        text = normalize_generated_text(text, config, symbols, size)
        text = strip_outer_font_guard(text, size)
        payloads = write_payloads(output_dir, size, text)
        wrapper = output_dir / output_name(size)
        wrapper.write_text(
            wrapper_text(size, payloads), encoding="utf-8", newline="\n")


def snapshot(directory: Path, sizes: list[int]) -> dict[str, bytes]:
    result: dict[str, bytes] = {}
    for size in sizes:
        wrapper = directory / output_name(size)
        payloads = sorted(
            directory.glob(
                f"{OUTPUT_PREFIX}_{size}_{PAYLOAD_LAYOUT}_part_*.inc"))
        if not wrapper.is_file() or not payloads:
            raise AssetInputError(
                f"missing generated font wrapper or payload for size {size}")
        expected = {path.name for path in payloads}
        stale = sorted(
            path.name for path in directory.glob(f"{OUTPUT_PREFIX}_{size}_*.inc")
            if path.name not in expected
        )
        legacy = directory / f"{OUTPUT_PREFIX}_{size}.inc"
        if legacy.exists():
            stale.append(legacy.name)
        if stale:
            raise AssetInputError(
                "stale font payloads: " + ", ".join(stale))
        for path in (wrapper, *payloads):
            result[path.name] = path.read_bytes()
    return result


def check_build(assets_dir: Path, source: Path, symbols: str,
                config: dict, converter: list[str]) -> None:
    with tempfile.TemporaryDirectory(prefix="product-ui-font-check-a-") as first_name:
        with tempfile.TemporaryDirectory(prefix="product-ui-font-check-b-") as second_name:
            first = Path(first_name)
            second = Path(second_name)
            build_into(source, symbols, config, converter, first)
            build_into(source, symbols, config, converter, second)
            first_snapshot = snapshot(first, config["sizes"])
            second_snapshot = snapshot(second, config["sizes"])
            if first_snapshot != second_snapshot:
                raise AssetInputError(
                    "lv_font_conv produced different bytes in two clean runs")
            actual = snapshot(assets_dir / "generated", config["sizes"])
            if actual != first_snapshot:
                stale = sorted(name for name in first_snapshot
                               if actual.get(name) != first_snapshot[name])
                raise AssetInputError(
                    "generated font assets are stale: " + ", ".join(stale))


def main() -> int:
    import sys
    sys.dont_write_bytecode = True
    from product_ui_workspace import prepare_work_directory
    parser = argparse.ArgumentParser()
    parser.add_argument("--work-dir", type=Path)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--validate-manifest", action="store_true")
    parser.add_argument("--converter", type=Path)
    parser.add_argument(
        "--assets-dir", type=Path,
        default=Path(__file__).resolve().parents[1],
    )
    args = parser.parse_args()
    prepare_work_directory(Path(__file__).resolve().parents[7], args.work_dir)
    if args.check and args.validate_manifest:
        parser.error("--check cannot be combined with --validate-manifest")

    assets_dir = args.assets_dir.resolve()
    try:
        manifest = load_manifest(assets_dir)
        config = font_config(manifest)
        strings, symbols = load_inventory(assets_dir, config)
        audit_inventory_coverage(assets_dir, config, symbols)
        if args.validate_manifest:
            print(
                f"Noto Sans SC {config['version']} manifest valid: "
                f"{len(strings)} strings, {len(symbols)} glyphs, "
                f"sizes={config['sizes']}")
            return 0
        source = validate_source(assets_dir, config)
        converter = resolve_converter(
            args.converter, manifest["tools"]["lv_font_conv"])
        if args.check:
            check_build(assets_dir, source, symbols, config, converter)
            print("Noto Sans SC subsets are deterministic and current")
            return 0
        build_into(source, symbols, config, converter,
                   assets_dir / "generated")
        print(
            f"generated Noto Sans SC {config['version']} subsets for "
            f"sizes {config['sizes']}")
        return 0
    except (AssetInputError, subprocess.CalledProcessError) as error:
        parser.exit(2, f"error: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
