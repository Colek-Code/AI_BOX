#!/usr/bin/env python3
"""Encode ui_pack PNGs to LVGL ARGB8888 (BGRA byte order) device assets."""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import sys
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageDraw, ImageChops, ImageFilter, ImageOps
sys.dont_write_bytecode = True
from product_ui_workspace import prepare_work_directory

VALUES_PER_LINE = 16
ASSETS = Path(__file__).resolve().parents[1]
HOME_GEOMETRY = json.loads((ASSETS / "ASSET_MANIFEST").read_text(encoding="utf-8"))["home_color_icons"]
CANVAS_SIZE = HOME_GEOMETRY["canvas_size"]


@dataclass(frozen=True)
class ColorIconSpec:
    enum_name: str
    rel_path: str
    width: int = CANVAS_SIZE
    height: int = CANVAS_SIZE


# Home color pack only. Control-center uses monochrome CC_* A4 masters.
ICONS: tuple[ColorIconSpec, ...] = (
    ColorIconSpec("MUSIC", "source/ui_pack/home/music.png"),
    ColorIconSpec("CALENDAR", "source/ui_pack/home/calendar.png"),
    ColorIconSpec("CALCULATOR", "source/ui_pack/home/calculator.png"),
    ColorIconSpec("GALLERY", "source/ui_pack/home/gallery.png"),
    ColorIconSpec("ALARM", "source/ui_pack/home/alarm.png"),
    ColorIconSpec("POMODORO", "source/ui_pack/home/pomodoro.png"),
    ColorIconSpec("SCHEDULE", "source/ui_pack/home/schedule.png"),
    ColorIconSpec("SETTINGS", "source/ui_pack/home/settings.png"),
    ColorIconSpec("EMPTY_SLOT", "source/ui_pack/home/empty_slot_frost.png"),
    ColorIconSpec("AI_CHAT", "source/ui_pack/home/ai_chat.png"),
    ColorIconSpec("CAMERA", "source/ui_pack/home/camera.png"),
)


def load_rgba_full(path: Path) -> Image.Image:
    if not path.is_file():
        raise FileNotFoundError(f"missing color icon source: {path}")
    return Image.open(path).convert("RGBA")


@dataclass(frozen=True)
class DerivedIcon:
    path: Path
    svg_sha256: str
    picture: Image.Image


def render_derived_icons(assets_dir: Path, geometry: dict) -> list[DerivedIcon]:
    """Render only declared SVG masters; original PNG cutouts stay untouched."""
    from resvg_py import svg_to_bytes

    root = assets_dir.resolve()
    source_paths = {Path(spec.rel_path).name: root / spec.rel_path for spec in ICONS}
    rendered = []
    for name, metadata in geometry.get("derived_sources", {}).items():
        if name not in source_paths:
            raise ValueError(f"unknown derived color icon: {name}")
        vector = (root / metadata["svg"]).resolve()
        picture_path = source_paths[name].resolve()
        if not vector.is_relative_to(root) or not picture_path.is_relative_to(root):
            raise ValueError(f"derived icon path leaves assets directory: {name}")
        if not vector.is_file():
            raise FileNotFoundError(f"missing derived SVG: {vector}")
        native = Image.open(io.BytesIO(svg_to_bytes(svg_path=str(vector))))
        supersampled = svg_to_bytes(svg_path=str(vector), width=native.width * 4,
                                     height=native.height * 4)
        picture = Image.open(io.BytesIO(supersampled)).convert("RGBA")
        picture = picture.resize(native.size, Image.Resampling.LANCZOS)
        rendered.append(DerivedIcon(picture_path,
                        hashlib.sha256(vector.read_bytes()).hexdigest(), picture))
    return rendered


def check_derived_icons(geometry: dict, rendered: list[DerivedIcon]) -> None:
    for icon in rendered:
        metadata = geometry["derived_sources"][icon.path.name]
        if icon.svg_sha256 != metadata.get("sha256"):
            raise ValueError(f"derived SVG hash mismatch: {icon.path.name}; run --refresh-derived")
        current = load_rgba_full(icon.path)
        if current.size != icon.picture.size or current.tobytes() != icon.picture.tobytes():
            raise ValueError(f"derived PNG is stale vs SVG: {icon.path.name}; run --refresh-derived")


def refresh_derived_icons(manifest_path: Path, manifest: dict,
                          rendered: list[DerivedIcon]) -> None:
    geometry = manifest["home_color_icons"]
    for icon in rendered:
        staging = icon.path.with_name(icon.path.name + ".next")
        icon.picture.save(staging, format="PNG")
        staging.replace(icon.path)
        geometry["source_sha256"][icon.path.name] = hashlib.sha256(icon.path.read_bytes()).hexdigest()
        geometry["derived_sources"][icon.path.name]["sha256"] = icon.svg_sha256
    write_generated(manifest_path, json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")


def resize_rgba(image: Image.Image, width: int, height: int) -> Image.Image:
    if image.size == (width, height):
        return image
    return image.resize((width, height), Image.Resampling.LANCZOS)


def process_icon(image: Image.Image, spec: ColorIconSpec,
                 geometry: dict | None = None) -> Image.Image:
    """Fit visible artwork, not transparent canvas, into the shared icon plate."""
    geometry = geometry or HOME_GEOMETRY
    canvas = int(geometry["canvas_size"])
    optical = int(geometry["optical_size"])
    radius = int(geometry["corner_radius"])
    threshold = int(geometry["alpha_threshold"])
    if not (0 < optical <= canvas and 0 < threshold <= 255 and
            0 <= radius <= optical // 2 and image.width > 0 and image.height > 0):
        raise ValueError(f"invalid icon geometry: {spec.enum_name}")
    image = image.convert("RGBA")
    alpha = image.getchannel("A")
    if alpha.getbbox() is None:
        raise ValueError(f"empty icon: {spec.enum_name}")
    bounds = geometry.get("content_bounds", {}).get(Path(spec.rel_path).name)
    if bounds is None:
        bounds = alpha.point(lambda value: 255 if value >= threshold else 0).getbbox()
    if bounds is None:
        raise ValueError(f"icon needs an explicit content bound: {spec.enum_name}")
    inset_fraction = float(geometry.get("source_edge_inset", {}).get(Path(spec.rel_path).name, 0))
    if not 0 <= inset_fraction <= 0.05:
        raise ValueError(f"invalid source edge inset: {spec.enum_name}")
    left, top, right, bottom = bounds
    if not (0 <= left < right <= image.width and 0 <= top < bottom <= image.height):
        raise ValueError(f"content bound outside source: {spec.enum_name}")
    artwork = image.crop((left, top, right, bottom))
    inset = round(min(artwork.size) * inset_fraction)
    if inset:
        # Explicitly trim known matte-contaminated cutouts before resampling.
        # RGB colour is never used to distinguish a white glyph from a fringe.
        alpha = ImageOps.expand(artwork.getchannel("A"), border=inset, fill=0)
        alpha = alpha.filter(ImageFilter.MinFilter(inset * 2 + 1))
        alpha = alpha.crop((inset, inset, inset + artwork.width, inset + artwork.height))
        trimmed_bounds = alpha.getbbox()
        if trimmed_bounds is None:
            raise ValueError(f"source edge inset removes all content: {spec.enum_name}")
        artwork.putalpha(alpha)
        artwork = artwork.crop(trimmed_bounds)
    scale = optical / max(artwork.size)
    size = (max(1, round(artwork.width * scale)), max(1, round(artwork.height * scale)))
    artwork = resize_rgba(artwork, *size)
    source_metadata = geometry.get("derived_sources", {}).get(Path(spec.rel_path).name, {})
    contour = source_metadata.get("contour", "rounded_rectangle")
    if contour not in ("source", "rounded_rectangle"):
        raise ValueError(f"invalid icon contour: {spec.enum_name}")
    if contour == "rounded_rectangle":
        # Legacy cutouts share a mask; clean vector masters own their precise outline.
        mask = Image.new("L", (size[0] * 4, size[1] * 4), 0)
        ImageDraw.Draw(mask).rounded_rectangle(
            (0, 0, mask.width - 1, mask.height - 1),
            radius=min(radius, min(size) // 2) * 4, fill=255)
        mask = mask.resize(size, Image.Resampling.LANCZOS)
        artwork.putalpha(ImageChops.multiply(artwork.getchannel("A"), mask))
    output = Image.new("RGBA", (canvas, canvas))
    output.paste(artwork, ((canvas - size[0]) // 2, (canvas - size[1]) // 2))
    return output


def resolve_source(assets_dir: Path, rel_path: str) -> Path:
    """Use ui_pack device-size working copies only."""
    return assets_dir / rel_path


def rgba_to_bgra_bytes(image: Image.Image) -> bytes:
    """LVGL ARGB8888 demo assets store Blue, Green, Red, Alpha per pixel."""
    pixels = list(image.get_flattened_data())
    out = bytearray(len(pixels) * 4)
    offset = 0
    for r, g, b, a in pixels:
        out[offset] = b
        out[offset + 1] = g
        out[offset + 2] = r
        out[offset + 3] = a
        offset += 4
    return bytes(out)


def format_c_array(data: bytes) -> str:
    lines: list[str] = []
    for index in range(0, len(data), VALUES_PER_LINE):
        chunk = data[index : index + VALUES_PER_LINE]
        body = ", ".join(f"0x{byte:02x}" for byte in chunk)
        lines.append(f"    {body},")
    return "\n".join(lines)


def write_generated(path: Path, text: str) -> None:
    """Verify bounded writes before replacing generated text."""
    staging = path.with_name(path.name + ".next")
    lines = text.encode("utf-8").splitlines(keepends=True)
    with staging.open("w+b") as stream:
        for offset in range(0, len(lines), 250):
            block = b"".join(lines[offset:offset + 250])
            position = stream.tell()
            stream.write(block)
            stream.flush()
            stream.seek(position)
            if stream.read(len(block)) != block:
                raise IOError(f"generated asset write verification failed: {path.name}")
    staging.replace(path)


def emit_header(path: Path, geometry: dict | None = None) -> None:
    geometry = geometry or HOME_GEOMETRY
    enums = "\n".join(
        f"    PRODUCT_UI_COLOR_ICON_{spec.enum_name}," for spec in ICONS
    )
    text = f"""/**
 * @file product_ui_color_icons.h
 * @brief Color UI icons from ui_pack (LVGL ARGB8888). Generated — do not edit.
 */

#ifndef __PRODUCT_UI_COLOR_ICONS_H__
#define __PRODUCT_UI_COLOR_ICONS_H__

#include "lvgl.h"

#define PRODUCT_UI_HOME_ICON_CANVAS_SIZE {geometry['canvas_size']}
#define PRODUCT_UI_HOME_ICON_OPTICAL_SIZE {geometry['optical_size']}

typedef enum {{
{enums}
    PRODUCT_UI_COLOR_ICON_COUNT
}} product_ui_color_icon_id_t;

const lv_image_dsc_t *product_ui_color_icon_get(
    product_ui_color_icon_id_t icon_id);

#endif /* __PRODUCT_UI_COLOR_ICONS_H__ */
"""
    write_generated(path, text)


def emit_source(assets_dir: Path, path: Path) -> None:
    geometry = json.loads((assets_dir / "ASSET_MANIFEST").read_text(encoding="utf-8"))["home_color_icons"]
    chunks: list[str] = [
        "/**",
        " * @file product_ui_color_icons.c",
        " * @brief Color UI icons from ui_pack (LVGL ARGB8888 BGRA). Generated.",
        " */",
        '#include "modules/applications/product_ui/assets/product_ui_color_icons.h"',
        "",
    ]
    table_entries: list[str] = []

    for source_spec in ICONS:
        spec = ColorIconSpec(source_spec.enum_name, source_spec.rel_path,
                             geometry["canvas_size"], geometry["canvas_size"])
        src_in = resolve_source(assets_dir, spec.rel_path)
        # Process a copy for encode only — never overwrite HD masters by default.
        image = process_icon(load_rgba_full(src_in), spec, geometry)
        data = rgba_to_bgra_bytes(image)
        transparent = sum(1 for px in image.get_flattened_data()
                          if px[3] == 0)
        total_px = spec.width * spec.height
        print(
            f"  {spec.enum_name:18s} {spec.width}x{spec.height} "
            f"transparent={transparent}/{total_px} "
            f"({100.0 * transparent / total_px:.1f}%)"
        )
        var = f"s_product_ui_color_icon_{spec.enum_name.lower()}"
        data_var = f"{var}_data"
        chunks.append(
            f"static const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST "
            f"uint8_t {data_var}[] = {{"
        )
        chunks.append(format_c_array(data))
        chunks.append("};")
        chunks.append("")
        chunks.append(f"static const lv_image_dsc_t {var} = {{")
        chunks.append("    .header.cf = LV_COLOR_FORMAT_ARGB8888,")
        chunks.append(f"    .header.w = {spec.width},")
        chunks.append(f"    .header.h = {spec.height},")
        chunks.append(f"    .data_size = sizeof({data_var}),")
        chunks.append(f"    .data = {data_var},")
        chunks.append("};")
        chunks.append("")
        table_entries.append(
            f"    [PRODUCT_UI_COLOR_ICON_{spec.enum_name}] = &{var},"
        )

    chunks.append(
        "static const lv_image_dsc_t *const s_product_ui_color_icons"
        "[PRODUCT_UI_COLOR_ICON_COUNT] = {"
    )
    chunks.extend(table_entries)
    chunks.append("};")
    chunks.append("")
    chunks.append(
        "const lv_image_dsc_t *product_ui_color_icon_get("
        "product_ui_color_icon_id_t icon_id)"
    )
    chunks.append("{")
    chunks.append("    if (icon_id >= PRODUCT_UI_COLOR_ICON_COUNT) {")
    chunks.append("        return NULL;")
    chunks.append("    }")
    chunks.append("    return s_product_ui_color_icons[icon_id];")
    chunks.append("}")
    chunks.append("")
    write_generated(path, "\n".join(chunks))


def main() -> None:
    import tempfile

    parser = argparse.ArgumentParser()
    parser.add_argument("--work-dir", type=Path)
    parser.add_argument(
        "--assets-dir",
        type=Path,
        default=Path(__file__).resolve().parents[1],
    )
    action = parser.add_mutually_exclusive_group()
    action.add_argument(
        "--check",
        action="store_true",
        help="Verify SVG/PNG sources and byte-compare regenerated C/H outputs.",
    )
    action.add_argument("--refresh-derived", action="store_true",
                        help="Regenerate declared PNGs from SVG masters, update hashes, then encode C/H.")
    args = parser.parse_args()
    prepare_work_directory(Path(__file__).resolve().parents[7], args.work_dir)
    assets_dir: Path = args.assets_dir
    manifest_path = assets_dir / "ASSET_MANIFEST"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    geometry = manifest["home_color_icons"]
    rendered = render_derived_icons(assets_dir, geometry)
    for spec in ICONS:
        path = assets_dir / spec.rel_path
        regenerating = args.refresh_derived and path.name in geometry.get("derived_sources", {})
        if not path.is_file() and not regenerating:
            raise FileNotFoundError(f"missing {path}")
    if args.refresh_derived:
        refresh_derived_icons(manifest_path, manifest, rendered)
    else:
        check_derived_icons(geometry, rendered)

    header = assets_dir / "product_ui_color_icons.h"
    source = assets_dir / "product_ui_color_icons.c"
    if args.check:
        home = manifest.get("home_color_icons", {})
        source_hashes = home.get("source_sha256")
        if not isinstance(source_hashes, dict):
            raise ValueError("ASSET_MANIFEST missing home_color_icons.source_sha256")
        expected_names = {Path(spec.rel_path).name for spec in ICONS}
        if set(source_hashes) != expected_names:
            raise ValueError(
                "home_color_icons.source_sha256 mismatch vs ICONS: "
                f"missing={sorted(expected_names - set(source_hashes))} "
                f"extra={sorted(set(source_hashes) - expected_names)}"
            )
        for name, expected in sorted(source_hashes.items()):
            path = assets_dir / "source" / "ui_pack" / "home" / name
            actual = hashlib.sha256(path.read_bytes()).hexdigest()
            if actual != expected:
                raise ValueError(
                    f"home color icon hash mismatch {name}: "
                    f"expected {expected}, got {actual}"
                )
        if not header.is_file() or not source.is_file():
            raise FileNotFoundError(
                "missing product_ui_color_icons.c/h — run without --check first"
            )
        with tempfile.TemporaryDirectory(prefix="product-ui-color-check-") as tmp:
            tmp_dir = Path(tmp)
            tmp_header = tmp_dir / "product_ui_color_icons.h"
            tmp_source = tmp_dir / "product_ui_color_icons.c"
            emit_header(tmp_header, geometry)
            emit_source(assets_dir, tmp_source)
            if tmp_header.read_bytes() != header.read_bytes():
                raise ValueError(
                    "product_ui_color_icons.h is stale vs regenerated output")
            if tmp_source.read_bytes() != source.read_bytes():
                raise ValueError(
                    "product_ui_color_icons.c is stale vs regenerated output")
        print(
            f"color icons check OK (byte-identical + source SHA): icons={len(ICONS)} "
            f"header={header.name} source={source.name}"
        )
        return

    emit_header(header, geometry)
    emit_source(assets_dir, source)
    total = 0
    for spec in ICONS:
        total += geometry["canvas_size"] ** 2 * 4
    print(f"wrote product_ui_color_icons.{{c,h}} icons={len(ICONS)} raw={total}B")


if __name__ == "__main__":
    main()
