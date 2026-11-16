"""把选定 Lucide 图形的几何定义栅格化为 Product UI A4 图标；固件无需 SVG 解码器。"""

from pathlib import Path
from typing import Callable

from PIL import Image, ImageDraw


SOURCE_COMMIT = "ab61fa277bb3548cf993d5cf73d7b7756f238bb0"
ICON_SIZE = 24
SCALE = 8
STROKE = 2 * SCALE


def point(value: float) -> int:
    """按超采样比例转换一个绘图坐标。"""
    return int(round(value * SCALE))


def points(values: list[tuple[float, float]]) -> list[tuple[int, int]]:
    """按超采样比例转换一组绘图坐标。"""
    return [(point(x), point(y)) for x, y in values]


def draw_line(draw: ImageDraw.ImageDraw, values: list[tuple[float, float]]) -> None:
    """按已声明坐标及线宽绘制超采样折线。"""
    draw.line(points(values), fill=255, width=STROKE, joint="curve")


def draw_circle(draw: ImageDraw.ImageDraw, center: tuple[float, float], radius: float) -> None:
    """按已声明坐标及线宽绘制超采样圆形。"""
    x, y = center
    box = [point(x - radius), point(y - radius), point(x + radius), point(y + radius)]
    draw.ellipse(box, outline=255, width=STROKE)


def draw_settings(draw: ImageDraw.ImageDraw) -> None:
    """在超采样画布绘制设置图标。"""
    for y, knob_x in ((6, 8), (12, 16), (18, 11)):
        draw_line(draw, [(3, y), (21, y)])
        draw_circle(draw, (knob_x, y), 2)


def draw_catalog(draw: ImageDraw.ImageDraw) -> None:
    """在超采样画布绘制目录图标的明确几何。"""
    for left in (4, 13):
        for top in (4, 13):
            draw.rounded_rectangle(
                [point(left), point(top), point(left + 7), point(top + 7)],
                radius=point(1), outline=255, width=STROKE
            )


def draw_wifi(draw: ImageDraw.ImageDraw) -> None:
    """在超采样画布绘制 Wi-Fi 图标。"""
    draw.arc([point(2), point(2), point(22), point(20)], 215, 325, fill=255, width=STROKE)
    draw.arc([point(6), point(7), point(18), point(19)], 215, 325, fill=255, width=STROKE)
    draw.ellipse([point(11), point(17), point(13), point(19)], fill=255)


def draw_image(draw: ImageDraw.ImageDraw) -> None:
    """在超采样画布绘制图片图标。"""
    draw.rounded_rectangle([point(3), point(4), point(21), point(20)],
                           radius=point(2), outline=255, width=STROKE)
    draw_circle(draw, (9, 9), 1.5)
    draw_line(draw, [(4, 17), (9, 12), (13, 16), (16, 13), (20, 17)])


def draw_audio(draw: ImageDraw.ImageDraw) -> None:
    """在超采样画布绘制音频图标的明确几何。"""
    draw.arc([point(4), point(3), point(20), point(19)], 180, 360, fill=255, width=STROKE)
    draw.rounded_rectangle([point(4), point(11), point(8), point(19)],
                           radius=point(1), outline=255, width=STROKE)
    draw.rounded_rectangle([point(16), point(11), point(20), point(19)],
                           radius=point(1), outline=255, width=STROKE)


def draw_file(draw: ImageDraw.ImageDraw) -> None:
    """在超采样画布绘制文件图标。"""
    draw_line(draw, [(6, 3), (14, 3), (19, 8), (19, 21), (6, 21), (6, 3)])
    draw_line(draw, [(14, 3), (14, 8), (19, 8)])
    draw_line(draw, [(9, 13), (16, 13)])
    draw_line(draw, [(9, 17), (16, 17)])


def draw_update(draw: ImageDraw.ImageDraw) -> None:
    """在超采样画布绘制更新图标。"""
    draw.arc([point(3), point(3), point(21), point(21)], 35, 205, fill=255, width=STROKE)
    draw.arc([point(3), point(3), point(21), point(21)], 215, 385, fill=255, width=STROKE)
    draw_line(draw, [(18, 3), (21, 3), (21, 6)])
    draw_line(draw, [(6, 21), (3, 21), (3, 18)])


def draw_home(draw: ImageDraw.ImageDraw) -> None:
    """在超采样画布绘制主屏图标。"""
    draw_line(draw, [(3, 11), (12, 3), (21, 11)])
    draw_line(draw, [(5, 10), (5, 21), (19, 21), (19, 10)])
    draw_line(draw, [(10, 21), (10, 15), (14, 15), (14, 21)])


ICONS: dict[str, Callable[[ImageDraw.ImageDraw], None]] = {
    "settings": draw_settings,
    "catalog": draw_catalog,
    "network": draw_wifi,
    "gallery": draw_image,
    "audio": draw_audio,
    "files": draw_file,
    "update": draw_update,
    "home": draw_home,
}


def pack_a4(alpha: Image.Image) -> bytes:
    """将相邻两个像素的高四位打包为 A4 字节。"""
    pixels = alpha.tobytes()
    packed = bytearray()
    for index in range(0, len(pixels), 2):
        high = pixels[index] >> 4
        low = pixels[index + 1] >> 4
        packed.append((high << 4) | low)
    return bytes(packed)


def render_icon(draw_icon: Callable[[ImageDraw.ImageDraw], None]) -> bytes:
    """按超采样几何绘制图标并量化为 A4 字节。"""
    canvas = Image.new("L", (ICON_SIZE * SCALE, ICON_SIZE * SCALE), 0)
    draw_icon(ImageDraw.Draw(canvas))
    alpha = canvas.resize((ICON_SIZE, ICON_SIZE), Image.Resampling.LANCZOS)
    return pack_a4(alpha)


def format_array(name: str, payload: bytes) -> str:
    """格式化既有 A4 像素及 LVGL 描述符，不改变像素值。"""
    lines = []
    for offset in range(0, len(payload), 16):
        chunk = payload[offset:offset + 16]
        lines.append("    " + ", ".join(f"0x{value:02x}" for value in chunk) + ",")
    body = "\n".join(lines)
    return (
        f"static const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST uint8_t "
        f"s_product_ui_icon_{name}_data[] = {{\n{body}\n}};\n\n"
        f"const lv_image_dsc_t product_ui_icon_{name} = {{\n"
        f"    .header.cf = LV_COLOR_FORMAT_A4,\n"
        f"    .header.w = {ICON_SIZE},\n"
        f"    .header.h = {ICON_SIZE},\n"
        f"    .data_size = sizeof(s_product_ui_icon_{name}_data),\n"
        f"    .data = s_product_ui_icon_{name}_data,\n"
        f"}};\n"
    )


def main() -> None:
    """把选定 Lucide 图形的几何定义栅格化为 Product UI A4 图标；固件无需 SVG 解码器。"""
    output = Path(__file__).resolve().parents[1] / "product_ui_icons.c"
    sections = [
        "/**\n * @file product_ui_icons.c\n * @brief 由 Lucide 图形生成的 A4 图标资源；请修改生成器。\n */\n",
        '#include "modules/applications/product_ui/assets/product_ui_icons.h"\n\n',
        f"/* Lucide source commit: {SOURCE_COMMIT} */\n\n",
    ]
    for name, draw_icon in ICONS.items():
        sections.append(format_array(name, render_icon(draw_icon)))
        sections.append("\n")
    output.write_text("".join(sections), encoding="utf-8")


if __name__ == "__main__":
    main()
