#!/usr/bin/env python3
"""Build original interface and control-center SVG glyphs as LVGL A4 images."""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import re
import shutil
import subprocess
import tempfile
from dataclasses import dataclass, replace
from pathlib import Path

import PIL
from PIL import Image, ImageOps


VALUES_PER_LINE = 24


@dataclass(frozen=True)
class IconSpec:
    enum_name: str
    source_name: str
    size: int
    optical_size: int
    source_kind: str = "interface"

    @property
    def source_file(self) -> str:
        return f"{self.source_name}.svg"


def app_icon(enum: str, stem: str, size: int, optical: int) -> IconSpec:
    return IconSpec(enum, stem, size, optical, "interface")


def cc_icon(enum: str, stem: str, size: int, optical: int) -> IconSpec:
    return IconSpec(enum, stem, size, optical, "control_center")


# Shared functions use one SVG master at each native raster size.
ICONS = (
    cc_icon("MUSIC", "media", 28, 24),
    app_icon("CALENDAR", "calendar", 28, 24),
    app_icon("CALCULATOR", "calculator", 28, 24),
    app_icon("GALLERY", "gallery", 28, 24),
    cc_icon("ALARM", "alarm", 28, 24),
    app_icon("TIMER", "timer", 28, 24),
    app_icon("SCHEDULE", "schedule", 28, 24),
    cc_icon("SETTINGS", "settings", 28, 24),
    cc_icon("WIFI", "wifi", 24, 21),
    app_icon("BLUETOOTH", "bluetooth", 24, 21),
    cc_icon("ROTATION", "rotation", 22, 20),
    cc_icon("BRIGHTNESS", "brightness", 22, 20),
    cc_icon("VOLUME", "volume", 22, 20),
    cc_icon("CC_WIFI", "wifi", 20, 18),
    cc_icon("CC_FOCUS", "focus", 20, 20),
    cc_icon("CC_MEDIA", "media", 20, 18),
    cc_icon("CC_ALARM", "alarm", 20, 20),
    cc_icon("CC_ROTATION", "rotation", 20, 20),
    cc_icon("CC_SETTINGS", "settings", 20, 20),
    cc_icon("CC_BRIGHTNESS", "brightness", 18, 16),
    cc_icon("CC_VOLUME", "volume", 18, 16),
    cc_icon("CC_PREVIOUS", "previous", 18, 16),
    cc_icon("CC_PLAY", "play", 18, 16),
    cc_icon("CC_PAUSE", "pause", 18, 16),
    cc_icon("CC_NEXT", "next", 18, 16),
    app_icon("AI_CHAT", "ai_chat", 28, 24),
    app_icon("CAMERA", "camera", 28, 24),
)

APP_ICONS = tuple(s for s in ICONS if s.source_kind == "interface")
CC_ICONS = tuple(s for s in ICONS if s.source_kind == "control_center")

EXPECTED_RESVG_PY = "0.3.1"


def require_resvg_py(expected: str) -> None:
    """Release and --check require pinned resvg_py; Chromium is preview-only."""
    try:
        import importlib.metadata as metadata
        import resvg_py  # noqa: F401
    except ImportError as error:
        raise ValueError(
            f"resvg_py {expected} is required for icon build/check "
            "(Chromium is preview-only, not a release fallback)"
        ) from error
    try:
        version = metadata.version("resvg_py")
    except metadata.PackageNotFoundError as error:
        raise ValueError("resvg_py is installed without package metadata") from error
    if version != expected:
        raise ValueError(
            f"resvg_py version mismatch: expected {expected}, got {version}"
        )


def validate_manifest(assets_dir: Path) -> tuple[dict[str, str], dict[str, str]]:
    # The ABI enum is hand-maintained. Its order must match the generated table.
    header = (assets_dir / "product_ui_icons.h").read_text(encoding="utf-8")
    identifiers = re.findall(r"^\s*PRODUCT_UI_ICON_([A-Z0-9_]+),?\s*$", header, re.M)
    identifiers = [name for name in identifiers if name != "COUNT"]
    if identifiers != [spec.enum_name for spec in ICONS]:
        raise ValueError("product_ui_icons.h enum order does not match ICONS")
    manifest_path = assets_dir / "ASSET_MANIFEST"
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except FileNotFoundError as error:
        raise ValueError(f"missing asset manifest: {manifest_path}") from error
    if manifest.get("schema_version") != 1:
        raise ValueError("unsupported ASSET_MANIFEST schema_version")
    policy = manifest.get("policy", {})
    if (policy.get("missing_resource") != "error" or
            policy.get("fallback_icon") is not None or
            policy.get("forbidden_fallback_icon") != "settings"):
        raise ValueError(
            "asset policy must fail missing resources and must not alias settings")
    expected_pillow = manifest.get("tools", {}).get("pillow")
    if PIL.__version__ != expected_pillow:
        raise ValueError(
            f"Pillow version mismatch: expected {expected_pillow}, "
            f"got {PIL.__version__}")
    expected_resvg = manifest.get("tools", {}).get("resvg_py")
    if expected_resvg != EXPECTED_RESVG_PY:
        raise ValueError(
            f"ASSET_MANIFEST tools.resvg_py must be {EXPECTED_RESVG_PY}, "
            f"got {expected_resvg!r}")
    require_resvg_py(expected_resvg)
    app_hashes = manifest.get("interface_icons", {}).get("source_sha256")
    if not isinstance(app_hashes, dict):
        raise ValueError("interface_icons.source_sha256 is missing")
    required_app = {spec.source_file for spec in APP_ICONS}
    if set(app_hashes) != required_app:
        missing = sorted(required_app - set(app_hashes))
        extra = sorted(set(app_hashes) - required_app)
        raise ValueError(
            f"interface icons manifest mismatch: missing={missing}, extra={extra}")
    cc_config = manifest.get("control_center_icons", {})
    cc_hashes = cc_config.get("source_sha256")
    if not isinstance(cc_hashes, dict):
        raise ValueError("control_center_icons.source_sha256 is missing")
    required_cc = {spec.source_file for spec in CC_ICONS}
    if set(cc_hashes) != required_cc:
        missing = sorted(required_cc - set(cc_hashes))
        extra = sorted(set(cc_hashes) - required_cc)
        raise ValueError(
            f"control_center icons manifest mismatch: missing={missing}, extra={extra}")
    return app_hashes, cc_hashes


def validate_sources(source_dir: Path, source_hashes: dict[str, str]) -> None:
    missing = sorted(name for name in source_hashes
                     if not (source_dir / name).is_file())
    if missing:
        raise FileNotFoundError("missing SVG sources: " + ", ".join(missing))
    for name, expected in sorted(source_hashes.items()):
        path = source_dir / name
        actual = hashlib.sha256(path.read_bytes()).hexdigest()
        if actual != expected:
            raise ValueError(
                f"SVG source hash mismatch for {path}: "
                f"expected {expected}, got {actual}")


def resolve_svg_path(assets_dir: Path, spec: IconSpec) -> Path:
    if spec.source_kind not in ("interface", "control_center"):
        raise ValueError(f"unknown icon source group: {spec.source_kind}")
    return assets_dir / "source" / spec.source_kind / spec.source_file


def render_alpha_chromium(svg_path: Path, size: int) -> Image.Image:
    candidates = (
        shutil.which("msedge"),
        shutil.which("chrome"),
        r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
        r"C:\Program Files\Google\Chrome\Application\chrome.exe",
    )
    browser = next((Path(path) for path in candidates
                    if path and Path(path).exists()), None)
    if browser is None:
        raise RuntimeError("resvg-py or Chromium/Edge is required")

    scale = 4
    extent = size * scale
    svg = svg_path.read_text(encoding="utf-8")
    html = (
        "<html><head><style>html,body{margin:0;background:#fff;overflow:hidden;}"
        f"svg{{display:block;width:{extent}px;height:{extent}px;}}"
        "</style></head><body>" + svg + "</body></html>"
    )
    with tempfile.TemporaryDirectory(prefix="product-ui-icon-") as temp_name:
        temp_dir = Path(temp_name)
        html_path = temp_dir / "icon.html"
        png_path = temp_dir / "icon.png"
        html_path.write_text(html, encoding="utf-8")
        command = (
            str(browser), "--headless=new", "--disable-gpu",
            "--hide-scrollbars", "--force-device-scale-factor=1",
            f"--window-size={extent},{extent}",
            f"--user-data-dir={temp_dir / 'profile'}",
            f"--screenshot={png_path}", html_path.as_uri(),
        )
        subprocess.run(command, check=True, stdout=subprocess.DEVNULL,
                       stderr=subprocess.PIPE)
        image = Image.open(png_path).convert("RGB").crop((0, 0, extent, extent))
        alpha = ImageOps.invert(image.convert("L"))
    return alpha.resize((size, size), Image.Resampling.LANCZOS)


def normalize_alpha(alpha: Image.Image, canvas_size: int,
                    optical_size: int) -> Image.Image:
    bounds = alpha.getbbox()
    if bounds is None:
        raise ValueError("SVG rendered an empty icon")

    glyph = alpha.crop(bounds)
    scale = min(optical_size / glyph.width, optical_size / glyph.height)
    glyph_size = (
        max(1, round(glyph.width * scale)),
        max(1, round(glyph.height * scale)),
    )
    glyph = glyph.resize(glyph_size, Image.Resampling.LANCZOS)
    canvas = Image.new("L", (canvas_size, canvas_size), 0)
    offset = (
        (canvas_size - glyph.width) // 2,
        (canvas_size - glyph.height) // 2,
    )
    canvas.paste(glyph, offset)
    return canvas


def render_alpha(svg_path: Path, spec: IconSpec) -> Image.Image:
    from resvg_py import svg_to_bytes

    render_size = spec.size * 4
    png = svg_to_bytes(
        svg_path=str(svg_path),
        width=render_size,
        height=render_size,
        shape_rendering="geometric_precision",
    )
    image = Image.open(io.BytesIO(png)).convert("RGBA")
    return normalize_alpha(image.getchannel("A"), spec.size,
                           spec.optical_size)


def render_alpha_preview(svg_path: Path, spec: IconSpec) -> Image.Image:
    """Explicit Chromium/Edge preview path — never used by --check/release."""
    render_size = spec.size * 4
    alpha = render_alpha_chromium(svg_path, render_size)
    return normalize_alpha(alpha, spec.size, spec.optical_size)


def pack_a4(alpha: Image.Image) -> bytes:
    pixels = list(alpha.get_flattened_data())
    packed = bytearray()
    for index in range(0, len(pixels), 2):
        high = min(15, (pixels[index] + 8) // 17)
        low = 0
        if index + 1 < len(pixels):
            low = min(15, (pixels[index + 1] + 8) // 17)
        packed.append((high << 4) | low)
    return bytes(packed)


def c_identifier(spec: IconSpec) -> str:
    return spec.enum_name.lower()


def write_data(stream, spec: IconSpec, data: bytes) -> None:
    name = c_identifier(spec)
    stream.write(
        "static const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST "
        f"uint8_t s_product_ui_icon_{name}_data[] = {{\n"
    )
    for start in range(0, len(data), VALUES_PER_LINE):
        row = data[start : start + VALUES_PER_LINE]
        stream.write("    " + ", ".join(f"0x{value:02x}" for value in row))
        stream.write(",\n")
    stream.write("};\n\n")
    stream.write(f"static const lv_image_dsc_t s_product_ui_icon_{name} = {{\n")
    stream.write("    .header.cf = LV_COLOR_FORMAT_A4,\n")
    stream.write(f"    .header.w = {spec.size},\n")
    stream.write(f"    .header.h = {spec.size},\n")
    stream.write(f"    .data_size = sizeof(s_product_ui_icon_{name}_data),\n")
    stream.write(f"    .data = s_product_ui_icon_{name}_data,\n")
    stream.write("};\n\n")


def write_c(output: Path, rendered: list[tuple[IconSpec, Image.Image]],
            variants: dict[int, list[tuple[IconSpec, Image.Image]]]) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    with io.StringIO(newline="\n") as stream:
        stream.write("/**\n")
        stream.write(" * @file product_ui_icons.c\n")
        stream.write(" * @brief Generated Product UI A4 icons from original SVG sources.\n")
        stream.write(" */\n")
        stream.write('#include "modules/applications/product_ui/assets/product_ui_icons.h"\n\n')
        for spec, alpha in rendered:
            write_data(stream, spec, pack_a4(alpha))
        stream.write(
            "static const lv_image_dsc_t *const s_product_ui_icons"
            "[PRODUCT_UI_ICON_COUNT] = {\n"
        )
        for spec, _ in rendered:
            stream.write(
                f"    [PRODUCT_UI_ICON_{spec.enum_name}] = "
                f"&s_product_ui_icon_{c_identifier(spec)},\n"
            )
        stream.write("};\n\n")
        stream.write(
            "const lv_image_dsc_t *product_ui_icon_get"
            "(product_ui_icon_id_t icon_id)\n{\n"
        )
        stream.write(
            "    if ((icon_id < 0) || (icon_id >= PRODUCT_UI_ICON_COUNT)) {\n"
        )
        stream.write("        return NULL;\n    }\n")
        stream.write("    return s_product_ui_icons[icon_id];\n}\n")
        for size, entries in variants.items():
            for spec, alpha in entries:
                write_data(stream, replace(spec, enum_name=f"{spec.enum_name}_{size}"), pack_a4(alpha))
            stream.write(f"static const lv_image_dsc_t *const s_icons_{size}[PRODUCT_UI_ICON_COUNT] = {{\n")
            for spec, _ in entries:
                stream.write(f"    [PRODUCT_UI_ICON_{spec.enum_name}] = &s_product_ui_icon_{c_identifier(spec)}_{size},\n")
            stream.write("};\n\n")
        stream.write("const lv_image_dsc_t *product_ui_icon_get_sized(product_ui_icon_id_t icon_id, uint16_t size)\n{\n")
        stream.write("    if (icon_id < 0 || icon_id >= PRODUCT_UI_ICON_COUNT) return NULL;\n    switch (size) {\n")
        for size in variants:
            stream.write(f"    case {size}: return s_icons_{size}[icon_id];\n")
        stream.write("    default: return NULL;\n    }\n}\n")
        lines = stream.getvalue().splitlines(keepends=True)
    with output.open("w", encoding="ascii", newline="\n") as stream:
        for start in range(0, len(lines), 250):
            batch = "".join(lines[start:start + 250])
            offset = stream.tell()
            stream.write(batch)
            stream.flush()
            with output.open("r", encoding="ascii", newline="") as check:
                check.seek(offset)
                if check.read(len(batch)) != batch:
                    raise OSError(f"generated icon write mismatch: {output}")


def build(assets_dir: Path, preview_dir: Path | None = None) -> None:
    cc_dir = assets_dir / "source" / "control_center"
    missing = sorted(
        str(resolve_svg_path(assets_dir, spec))
        for spec in ICONS
        if not resolve_svg_path(assets_dir, spec).is_file()
    )
    if missing:
        raise FileNotFoundError("missing SVG sources: " + ", ".join(missing))
    if not cc_dir.is_dir():
        raise FileNotFoundError(f"missing control_center SVG dir: {cc_dir}")

    preview_dir = preview_dir or assets_dir / "reference" / "icons"
    preview_dir.mkdir(parents=True, exist_ok=True)
    rendered: list[tuple[IconSpec, Image.Image]] = []
    for spec in ICONS:
        alpha = render_alpha(resolve_svg_path(assets_dir, spec), spec)
        alpha.save(preview_dir / f"{c_identifier(spec)}.png")
        rendered.append((spec, alpha))
    sizes = json.loads((assets_dir / "ASSET_MANIFEST").read_text(encoding="utf-8"))["native_icon_sizes"]
    if not sizes or any(not isinstance(size, int) or size <= 0 or size % 2 for size in sizes):
        raise ValueError("native A4 icon sizes must be positive even integers")
    variants = {}
    for size in sizes:
        entries = []
        for spec in ICONS:
            fitted = replace(spec, size=size, optical_size=max(1, round(size * spec.optical_size / spec.size)))
            entries.append((fitted, render_alpha(resolve_svg_path(assets_dir, spec), fitted)))
        variants[size] = entries
    write_c(assets_dir / "product_ui_icons.c", rendered, variants)


def generated_snapshot(assets_dir: Path) -> dict[str, bytes]:
    result = {
        "product_ui_icons.c": (assets_dir / "product_ui_icons.c").read_bytes(),
    }
    for spec in ICONS:
        name = f"{c_identifier(spec)}.png"
        result[f"reference/icons/{name}"] = (
            assets_dir / "reference" / "icons" / name).read_bytes()
    return result


def check_build(assets_dir: Path) -> None:
    app_dir = assets_dir / "source" / "interface"
    cc_dir = assets_dir / "source" / "control_center"
    if not app_dir.exists():
        raise FileNotFoundError(f"missing interface SVG directory: {app_dir}")
    if not cc_dir.exists():
        raise FileNotFoundError(f"missing control_center SVG directory: {cc_dir}")

    with tempfile.TemporaryDirectory(prefix="product-ui-icons-check-a-") as first_name:
        with tempfile.TemporaryDirectory(
                prefix="product-ui-icons-check-b-") as second_name:
            first_assets = Path(first_name)
            second_assets = Path(second_name)
            for dest in (first_assets, second_assets):
                shutil.copyfile(assets_dir / "ASSET_MANIFEST", dest / "ASSET_MANIFEST")
                shutil.copytree(app_dir, dest / "source" / "interface")
                shutil.copytree(cc_dir, dest / "source" / "control_center")
            build(first_assets)
            build(second_assets)
            first = generated_snapshot(first_assets)
            second = generated_snapshot(second_assets)
            if first != second:
                changed = sorted(name for name in first
                                 if first.get(name) != second.get(name))
                raise ValueError(
                    "A4 icon generation is not deterministic: " +
                    ", ".join(changed))
            published_source = assets_dir / "product_ui_icons.c"
            if published_source.read_bytes() != first["product_ui_icons.c"]:
                raise ValueError(
                    "generated icon assets are stale: " + published_source.name)


def main() -> int:
    import sys
    sys.dont_write_bytecode = True
    from product_ui_workspace import prepare_work_directory
    parser = argparse.ArgumentParser()
    parser.add_argument("--work-dir", type=Path)
    parser.add_argument("--check", action="store_true")
    parser.add_argument(
        "--preview-chromium",
        action="store_true",
        help="Explicit preview only (Chromium/Edge). Not valid for --check.",
    )
    parser.add_argument(
        "--assets-dir", type=Path,
        default=Path(__file__).resolve().parents[1],
    )
    args = parser.parse_args()
    work_dir = prepare_work_directory(Path(__file__).resolve().parents[7], args.work_dir)
    if args.check and args.preview_chromium:
        parser.error("--check cannot use --preview-chromium")
    assets_dir = args.assets_dir.resolve()
    try:
        app_hashes, cc_hashes = validate_manifest(assets_dir)
        app_dir = assets_dir / "source" / "interface"
        cc_dir = assets_dir / "source" / "control_center"
        validate_sources(app_dir, app_hashes)
        validate_sources(cc_dir, cc_hashes)
        if args.preview_chromium:
            preview_dir = assets_dir / "reference" / "icons_preview"
            preview_dir.mkdir(parents=True, exist_ok=True)
            for spec in ICONS:
                alpha = render_alpha_preview(
                    resolve_svg_path(assets_dir, spec), spec)
                alpha.save(preview_dir / f"{c_identifier(spec)}.png")
            print(f"previewed {len(ICONS)} icons via Chromium into {preview_dir}")
            return 0
        if args.check:
            check_build(assets_dir)
            print(
                f"{len(ICONS)} Product UI icons are deterministic and current "
                f"(original SVG masters={len(app_hashes) + len(cc_hashes)})"
            )
            return 0
        build(assets_dir, work_dir / "icon_previews")
        print(
            f"generated {len(ICONS)} A4 icons "
            f"(original SVG masters={len(app_hashes) + len(cc_hashes)})"
        )
        return 0
    except (OSError, ValueError, json.JSONDecodeError,
            subprocess.CalledProcessError) as error:
        parser.exit(2, f"error: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
