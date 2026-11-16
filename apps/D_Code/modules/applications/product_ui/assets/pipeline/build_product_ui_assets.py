#!/usr/bin/env python3
"""按声明目录确定性生成 RGB565 壁纸、缩略图和固定调色表。"""
from __future__ import annotations

import argparse
import hashlib
import importlib.metadata
import io
import json
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET

sys.dont_write_bytecode = True
import PIL
from PIL import Image
from resvg_py import svg_to_bytes
from product_ui_workspace import prepare_work_directory

BAYER4 = ((0, 8, 2, 10), (12, 4, 14, 6), (3, 11, 1, 9), (15, 7, 13, 5))
PALETTE_KEYS = ("surface_media", "surface_quick", "surface_slider", "surface_pressed",
                "on_surface", "on_surface_variant", "primary", "on_primary",
                "slider_fill", "slider_icon", "control_border")


def owned_path(root: Path, relative: str) -> Path:
    """解析并拒绝越出资源根目录的路径。"""
    path = (root / relative).resolve()
    if not path.is_relative_to(root.resolve()):
        raise ValueError(f"asset path leaves its directory: {relative}")
    return path


def load_manifest(assets: Path) -> dict:
    """读取并校验资源清单及固定版本。"""
    manifest = json.loads((assets / "ASSET_MANIFEST").read_text(encoding="utf-8"))
    if manifest.get("schema_version") != 1:
        raise ValueError("unsupported asset manifest schema")
    if PIL.__version__ != manifest["tools"]["pillow"]:
        raise ValueError("Pillow version differs from ASSET_MANIFEST")
    if importlib.metadata.version("resvg_py") != manifest["tools"]["resvg_py"]:
        raise ValueError("resvg_py version differs from ASSET_MANIFEST")
    config = manifest["rgb565"]
    if (config.get("source_kind") != "svg-catalog-v1" or config.get("format") != "RGB565" or
            config.get("dither") != "bayer4x4-v1" or
            (config.get("width"), config.get("height")) != (320, 240) or
            (config.get("thumbnail_width"), config.get("thumbnail_height")) != (80, 60)):
        raise ValueError("unsupported wallpaper geometry or encoding")
    entries = config.get("wallpapers", [])
    if [entry.get("id") for entry in entries] != [0, 1, 2]:
        raise ValueError("wallpaper IDs must be unique and ordered: 0, 1, 2")
    outputs, slugs = set(), set()
    for entry in entries:
        slug = entry["slug"]
        if not re.fullmatch(r"[a-z][a-z0-9_]{0,31}", slug) or slug in slugs:
            raise ValueError(f"invalid or duplicate wallpaper slug: {slug}")
        slugs.add(slug)
        if not entry.get("name"):
            raise ValueError(f"wallpaper name is missing: {slug}")
        path = owned_path(assets, entry["source"])
        if not path.is_file():
            raise FileNotFoundError(f"missing wallpaper master: {path}")
        if hashlib.sha256(path.read_bytes()).hexdigest() != entry["sha256"]:
            raise ValueError(f"wallpaper source hash mismatch: {path.name}")
        document = ET.parse(path).getroot()
        if document.get("width") != "320" or document.get("height") != "240":
            raise ValueError(f"wallpaper master must declare 320x240: {path.name}")
        for key in ("main", "thumbnail"):
            relative = entry[key]
            owned_path(assets, relative)
            if not relative.startswith("generated/product_ui_wallpaper_") or not relative.endswith(".inc"):
                raise ValueError(f"invalid generated wallpaper path: {relative}")
            if relative in outputs:
                raise ValueError(f"duplicate wallpaper output: {relative}")
            outputs.add(relative)
    if set(config["palette"]) != set(PALETTE_KEYS):
        raise ValueError("wallpaper-independent palette keys are incomplete")
    return config


def linear_channel(value: int) -> float:
    """将 sRGB 单通道转换为线性亮度。"""
    channel = value / 255
    return channel / 12.92 if channel <= .04045 else ((channel + .055) / 1.055) ** 2.4


def luminance(pixel) -> float:
    """按线性通道权重计算相对亮度。"""
    return sum(weight * linear_channel(value) for weight, value in
               zip((.2126, .7152, .0722), pixel))


def contrast(first, second) -> float:
    """计算前景与背景的亮度对比度。"""
    light, dark = sorted((luminance(first), luminance(second)), reverse=True)
    return (light + .05) / (dark + .05)


def palette_output(config: dict) -> str:
    """校验颜色及对比度并生成固定 C 调色表。"""
    colors = {name: tuple(bytes.fromhex(value)) for name, value in config["palette"].items()}
    if any(len(value) != 3 for value in colors.values()):
        raise ValueError("palette colors must use six hexadecimal digits")
    for background in ("surface_media", "surface_quick", "surface_slider"):
        for foreground in ("on_surface", "on_surface_variant"):
            if contrast(colors[foreground], colors[background]) < 4.5:
                raise ValueError(f"palette text contrast is too low: {foreground}/{background}")
    for foreground, background in (("slider_fill", "surface_slider"), ("slider_icon", "slider_fill"),
                                   ("control_border", "surface_slider"), ("on_primary", "primary"),
                                   ("primary", "surface_quick")):
        if contrast(colors[foreground], colors[background]) < 3:
            raise ValueError(f"palette control contrast is too low: {foreground}/{background}")
    lines = ["/** @file product_ui_palette.h @brief 由 build_product_ui_assets.py 生成的固定调色表；请修改生成器。 */",
             "#ifndef __PRODUCT_UI_PALETTE_H__", "#define __PRODUCT_UI_PALETTE_H__", ""]
    for name in PALETTE_KEYS:
        lines.append(f'#define {"PRODUCT_UI_PALETTE_" + name.upper():<47} 0x{config["palette"][name]}u')
    return "\n".join(lines + ["", "#endif", ""])


def encode_rgb565(picture: Image.Image) -> list[int]:
    """按 4×4 Bayer 抖动量化 RGB565 像素。"""
    values = []
    for index, pixel in enumerate(picture.get_flattened_data()):
        x, y = index % picture.width, index // picture.width
        offset = (BAYER4[y % 4][x % 4] + .5) / 16 - .5
        red, green, blue = [max(0, min(level, round(value * level / 255 + offset)))
                            for value, level in zip(pixel, (31, 63, 31))]
        values.append((red << 11) | (green << 5) | blue)
    return values


def decoded_rgb(value: int) -> tuple[int, int, int]:
    """把 RGB565 值展开为 RGB 通道。"""
    return ((value >> 11) * 255 // 31, ((value >> 5) & 63) * 255 // 63,
            (value & 31) * 255 // 31)


def image_include(values: list[int]) -> str:
    """将既有像素值格式化为 C include 文本。"""
    lines = ["/* 生成的 RGB565 像素；源母版保持原样。 */"]
    lines += [",".join(f"0x{value:04x}" for value in values[start:start + 256]) + ","
              for start in range(0, len(values), 256)]
    return "\n".join(lines) + "\n"


def catalog_header() -> str:
    """生成壁纸目录的公开头文件合同。"""
    return """/** @file product_ui_wallpapers.h @brief 由 build_product_ui_assets.py 生成的壁纸静态目录。 */
#ifndef PRODUCT_UI_WALLPAPERS_H
#define PRODUCT_UI_WALLPAPERS_H
#include "lvgl.h"
#include <stdint.h>
typedef struct {
    const char *name; /**< 静态显示名称。 */
    const lv_image_dsc_t *image; /**< 借用的静态全尺寸图像。 */
    const lv_image_dsc_t *thumbnail; /**< 借用的静态缩略图。 */
} product_ui_wallpaper_t;
/** @brief 读取壁纸目录数量。 @return 静态目录中的条目数。 */
uint8_t product_ui_wallpaper_count(void);
/**
 * @brief 按目录编号借用壁纸。
 * @param[in] id 小于 product_ui_wallpaper_count() 的目录编号。
 * @return 编号有效时返回静态只读条目，否则返回 NULL；调用方不释放。
 */
const product_ui_wallpaper_t *product_ui_wallpaper_get(uint8_t id);
#endif
"""


def catalog_source(config: dict) -> str:
    """生成静态壁纸像素引用及目录查询实现。"""
    lines = ['/** @file product_ui_wallpapers.c @brief 由 build_product_ui_assets.py 生成的壁纸像素及目录。 */', '#include "config/app_config.h"',
             '#if defined(D_LVGL_ENABLE) && (D_UI_TARGET == D_UI_PRODUCT)',
             '#include "modules/applications/product_ui/assets/product_ui_wallpapers.h"', ""]
    for entry in config["wallpapers"]:
        for key, width, height in (("main", 320, 240), ("thumbnail", 80, 60)):
            name = f's_wallpaper_{entry["slug"]}_{key}'
            lines += [f"static const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST uint16_t {name}_pixels[] = {{",
                      '#include "modules/applications/product_ui/assets/' + entry[key] + '"', "};",
                      f"static const lv_image_dsc_t {name} = {{",
                      "    .header.cf = LV_COLOR_FORMAT_RGB565,",
                      f"    .header.w = {width}, .header.h = {height}, .header.stride = {width * 2},",
                      f"    .data_size = sizeof({name}_pixels), .data = (const uint8_t *){name}_pixels,", "};", ""]
    lines.append("static const product_ui_wallpaper_t s_wallpapers[] = {")
    for entry in config["wallpapers"]:
        lines.append('    {' + json.dumps(entry["name"], ensure_ascii=False) +
                     f', &s_wallpaper_{entry["slug"]}_main, &s_wallpaper_{entry["slug"]}_thumbnail' + '},')
    lines += ["};", "", "/** @brief 读取静态壁纸目录数量。 */\nuint8_t product_ui_wallpaper_count(void)", "{",
              "    return (uint8_t)(sizeof(s_wallpapers) / sizeof(s_wallpapers[0]));", "}", "",
              "/** @brief 按编号借用静态目录条目，越界返回 NULL。 */\nconst product_ui_wallpaper_t *product_ui_wallpaper_get(uint8_t id)", "{",
              "    return id < product_ui_wallpaper_count() ? &s_wallpapers[id] : NULL;", "}", "#endif", ""]
    return "\n".join(lines)


def generate(assets: Path, config: dict) -> tuple[dict[str, str], dict[str, Image.Image]]:
    """根据明确配置生成资源结果。"""
    outputs = {"product_ui_wallpapers.h": catalog_header(), "product_ui_wallpapers.c": catalog_source(config),
               "generated/product_ui_palette.h": palette_output(config)}
    previews = {}
    for entry in config["wallpapers"]:
        picture = Image.open(io.BytesIO(svg_to_bytes(svg_path=str(owned_path(assets, entry["source"]))))).convert("RGBA")
        if picture.size != (320, 240) or picture.getchannel("A").getextrema() != (255, 255):
            raise ValueError(f'wallpaper must be opaque 320x240: {entry["slug"]}')
        picture = picture.convert("RGB")
        values = encode_rgb565(picture)
        minimum = min(contrast((255, 255, 255), decoded_rgb(value)) for value in set(values))
        if minimum < 4.5:
            raise ValueError(f'{entry["slug"]}: RGB565 white-label contrast {minimum:.2f} < 4.5')
        outputs[entry["main"]] = image_include(values)
        thumbnail = picture.resize((80, 60), Image.Resampling.LANCZOS)
        outputs[entry["thumbnail"]] = image_include(encode_rgb565(thumbnail))
        preview = Image.new("RGB", picture.size)
        preview.putdata([decoded_rgb(value) for value in values])
        previews[entry["slug"]] = preview
    return outputs, previews


def write_generated(path: Path, contents: str) -> None:
    """按现有分批校验规则写入生成文本。"""
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = contents.splitlines(keepends=True)
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        for start in range(0, len(lines), 250):
            batch = "".join(lines[start:start + 250])
            offset = stream.tell()
            stream.write(batch)
            stream.flush()
            with path.open("r", encoding="utf-8", newline="") as check:
                check.seek(offset)
                if check.read(len(batch)) != batch:
                    raise OSError(f"generated asset write mismatch: {path}")


def main() -> int:
    """按声明目录确定性生成 RGB565 壁纸、缩略图和固定调色表。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets-dir", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--work-dir", type=Path)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--preview", action="store_true")
    args = parser.parse_args()
    if args.check and args.preview:
        parser.error("--check does not create previews")
    output = prepare_work_directory(Path(__file__).resolve().parents[7], args.work_dir)
    assets = args.assets_dir.resolve()
    config = load_manifest(assets)
    generated, previews = generate(assets, config)
    if args.check:
        repeated, _ = generate(assets, config)
        if generated != repeated:
            raise ValueError("wallpaper generation is not deterministic")
        for relative, contents in generated.items():
            path = owned_path(assets, relative)
            if not path.is_file() or path.read_bytes() != contents.encode("utf-8"):
                raise ValueError(f"wallpaper output is stale: {relative}")
    else:
        for relative, contents in generated.items():
            write_generated(owned_path(assets, relative), contents)
        if args.preview:
            directory = output / "wallpapers"
            directory.mkdir(parents=True, exist_ok=True)
            for slug, picture in previews.items():
                picture.save(directory / (slug + ".png"))
    print(f'Wallpaper catalog {"check" if args.check else "generation"}: PASS (3 main + 3 thumbnails, 489600 pixel bytes)')
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
