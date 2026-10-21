"""Keep asset and verification scratch files inside the selected project."""

from __future__ import annotations

import os
from pathlib import Path
import sys
import tempfile


def resolve_project_output(project_root: Path, requested: Path) -> Path:
    project_root = project_root.resolve()
    directory = requested.resolve()
    if not directory.is_relative_to(project_root) or directory == project_root:
        raise ValueError(f"work directory must be inside the project: {directory}")
    return directory


def prepare_work_directory(project_root: Path, requested: Path | None = None) -> Path:
    directory = resolve_project_output(
        project_root, requested or project_root / "apps/D_Code/build/out/product_ui")
    directory.mkdir(parents=True, exist_ok=True)
    temporary = resolve_project_output(project_root, directory / "temp")
    temporary.mkdir(exist_ok=True)
    # This process and its children only; the caller's environment is unchanged.
    for name in ("TEMP", "TMP", "TMPDIR"):
        os.environ[name] = str(temporary)
    tempfile.tempdir = str(temporary)
    os.environ["PYTHONDONTWRITEBYTECODE"] = "1"
    sys.dont_write_bytecode = True
    return directory
