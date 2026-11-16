# Product UI 资源

普通固件直接编译已生成的 C / `.inc` 资源，不在设备上解码 PNG。
[ASSET_MANIFEST](ASSET_MANIFEST) 记录源文件摘要、工具版本、格式及资源编号。

## 文件布局

| 资源 | 源文件 | 编译输入 |
| --- | --- | --- |
| 桌面彩色图标 | `source/ui_pack/home/` | `product_ui_color_icons.c/h`，ARGB8888 |
| 控制中心和应用图标 | `source/control_center/`、`source/interface/` | `product_ui_icons.c`，A4 |
| 壁纸与缩略图 | `source/wallpapers/` | `product_ui_wallpapers.c/h`、`generated/*.inc`，RGB565 |
| 字体 | `fonts/source/`、`fonts/strings.inventory` | `generated/product_ui_font_*.c` |
| 配色 | 清单内的 palette | `generated/product_ui_palette.h` |

`reference/icons/` 用于离线预览，不是固件输入。源 SVG、字体及相应许可证随资源维护。
Noto 字体许可见 [SIL Open Font License](fonts/source/LICENSE)。

## 修改与校验

资源生成需要 Python、Node.js，以及清单指定版本的 Pillow、resvg_py、lv_font_conv。
修改源文件后运行 `pipeline/` 中对应生成器，使用 `--check` 检查结果；新增界面文字同步更新字形清单。

在工程根目录执行全量资源校验：

```powershell
python -B -X utf8 apps/D_Code/modules/applications/product_ui/assets/pipeline/check_product_ui_assets.py --check --work-dir apps/D_Code/build/out/assets-check
```

若 lv_font_conv 未加入 PATH，追加 `--converter <lv_font_conv.js的路径>`。
普通固件编译不需要安装资源生成工具。
