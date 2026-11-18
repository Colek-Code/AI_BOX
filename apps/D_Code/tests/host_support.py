"""为主机回归约束输出归属，绝不调用烧录器。"""
from pathlib import Path
ROOT = Path(__file__).resolve().parents[3]
def prepare_work_directory(root, requested):
    """只允许在本工程 build/out 中创建独立测试目录。"""
    path = requested.resolve()
    base = (root / 'apps/D_Code/build/out').resolve()
    if not path.is_relative_to(base) or path == base:
        raise ValueError('work-dir must be a task directory inside apps/D_Code/build/out')
    path.mkdir(parents=True, exist_ok=True)
    return path
