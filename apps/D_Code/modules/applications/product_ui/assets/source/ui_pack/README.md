# 桌面彩色图标

`home/` 保存桌面图标 SVG 及对应 PNG，生成 56 × 56 的圆角图标。
编码器为 `../../pipeline/build_product_ui_color_icons.py`，输出 `product_ui_color_icons.c/h`。
源摘要见 `../../ASSET_MANIFEST` 的 `home_color_icons.source_sha256`。

控制中心使用 `../control_center/` 的单色 A4 图标，统一资源说明见 [资源入口](../../README.md)。
