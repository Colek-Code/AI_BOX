#!/usr/bin/env python3
"""Run every Product UI asset pipeline in deterministic check mode."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


PIPELINES = (
    "build_product_ui_assets.py",
    "build_product_ui_icons.py",
    "build_product_ui_fonts.py",
    "build_product_ui_color_icons.py",
)


def main() -> int:
    import sys
    sys.dont_write_bytecode = True
    from product_ui_workspace import prepare_work_directory
    parser = argparse.ArgumentParser()
    parser.add_argument("--work-dir", type=Path)
    parser.add_argument("--check", action="store_true", required=True)
    parser.add_argument(
        "--assets-dir", type=Path,
        default=Path(__file__).resolve().parents[1],
    )
    parser.add_argument(
        "--converter", type=Path,
        help="Path to lv_font_conv.js or its installed executable wrapper.",
    )
    args = parser.parse_args()
    work_dir = prepare_work_directory(Path(__file__).resolve().parents[7], args.work_dir)

    pipeline_dir = Path(__file__).resolve().parent
    assets_dir = args.assets_dir.resolve()
    for name in PIPELINES:
        command = [
            sys.executable, "-X", "utf8", str(pipeline_dir / name),
            "--check", "--assets-dir", str(assets_dir),
            "--work-dir", str(work_dir),
        ]
        if name == "build_product_ui_fonts.py" and args.converter is not None:
            command.extend(["--converter", str(args.converter.resolve())])
        result = subprocess.run(command, check=False)
        if result.returncode != 0:
            parser.exit(
                result.returncode,
                f"error: Product UI asset check failed in {name}\n")
    print("all Product UI asset pipelines are deterministic and current")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
