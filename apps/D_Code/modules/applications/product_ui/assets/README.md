# Product UI Assets

`ASSET_MANIFEST` is the source-of-truth for source revisions, integrity
digests, licenses, tool versions, output formats, and the missing-resource
policy. A missing or mismatched resource is an error. No asset lookup aliases
an unknown resource to the settings icon; firmware code may instead handle a
`NULL` icon descriptor explicitly.

## Deterministic Check

```powershell
python -X utf8 apps\D_Code\modules\applications\product_ui\assets\pipeline\check_product_ui_assets.py --check
```

Each pipeline validates pinned hashes/tool versions, regenerates into temp
dirs where applicable, and fails if checked-in outputs are stale.

## Live sources (git-tracked) vs rebuild-only helpers

| Live source (must be in Git) | Encoder / consumer |
| --- | --- |
| `source/ui_pack/home/*.svg` + derived `*.png` | `build_product_ui_color_icons.py --refresh-derived` → `product_ui_color_icons.c/h` (ARGB home tiles) |
| `source/control_center/*.svg` | `build_product_ui_icons.py` → A4 `product_ui_icons.c` (CC monochrome glyphs) |
| `source/interface/*.svg` | same A4 pipeline (application glyphs; shared functions reuse control-center masters) |
| `source/wallpapers/*.svg` | three original opaque masters, stable IDs and hashes in the manifest |
| `generated/product_ui_wallpaper_*.inc` + `product_ui_wallpapers.c/h` | RGB565 320×240 wallpapers and 80×60 thumbnails |
| `generated/product_ui_palette.h` | wallpaper-independent palette tokens from the manifest |

**Not live sources (do not commit rebuild products):**

- `reference/icons/*.png` — local A4 previews from icon pipeline
- `source/product_ui_wallpaper_placeholder.png` — historical source, no longer consumed
- Blur intermediates / `product_ui_wallpaper_blur.inc` — **removed**: opaque CC
  sheet made runtime blur invisible; pipeline no longer publishes blur RGB565
- Design reference images and internal acceptance records are maintained in the
  development workspace and are not inputs to the public firmware build.

## RGB565 Wallpaper

Firmware consumes generated RGB565 `.inc` files only (no on-device PNG decode).
Generation reads all three declared SVG masters, applies fixed offline dithering,
and checks white-label contrast after RGB565 quantization. `--check` compares
reproduced outputs and never substitutes a procedural placeholder. Previews are
opt-in with `--preview`; all temporary outputs use the project build directory.

```powershell
python -X utf8 apps\D_Code\modules\applications\product_ui\assets\pipeline\build_product_ui_assets.py
python -X utf8 apps\D_Code\modules\applications\product_ui\assets\pipeline\build_product_ui_assets.py --check
```

## Color icons (home only)

Home app tiles from `source/ui_pack/home/` → ARGB8888. Control center no longer
uses `ui_pack/control/` frost/media/quick PNGs (removed).

```powershell
python -X utf8 apps\D_Code\modules\applications\product_ui\assets\pipeline\build_product_ui_color_icons.py --check
```

## Monochrome A4 icons

Original interface + control-center SVG masters → LVGL A4. Control-center design
board previews load the **same SVG masters** (not `reference/icons`).

```powershell
python -X utf8 apps\D_Code\modules\applications\product_ui\assets\pipeline\build_product_ui_icons.py --check
```

## Noto Sans SC A4

`fonts/strings.inventory` is the single UTF-8 string inventory. Fonts use
`lv_font_conv` 1.5.3. The exact pinned Noto Sans SC OTF is tracked so a clean
clone can rebuild the subsets without network access.
