"""将资源生成和验证的临时文件限制在所选工程的输出范围内。"""

from __future__ import annotations

import os
from pathlib import Path
import sys
import tempfile


def resolve_project_output(project_root: Path, requested: Path) -> Path:
    """解析输出及现有链接的实际目标，拒绝越出当前工程任务范围。"""
    project_root = project_root.resolve()
    directory = requested.resolve()
    if not directory.is_relative_to(project_root) or directory == project_root:
        raise ValueError(f"work directory must be inside the project: {directory}")
    return directory


def prepare_work_directory(project_root: Path, requested: Path | None = None) -> Path:
    """解析工程输出归属后建立任务目录，并只为本进程及子进程设置临时环境。"""
    directory = resolve_project_output(
        project_root, requested or project_root / "apps/D_Code/build/out/product_ui")
    directory.mkdir(parents=True, exist_ok=True)
    temporary = resolve_project_output(project_root, directory / "temp")
    temporary.mkdir(exist_ok=True)
    # 仅作用于本进程及其子进程，不改变调用者环境。
    for name in ("TEMP", "TMP", "TMPDIR"):
        os.environ[name] = str(temporary)
    tempfile.tempdir = str(temporary)
    os.environ["PYTHONDONTWRITEBYTECODE"] = "1"
    sys.dont_write_bytecode = True
    return directory
