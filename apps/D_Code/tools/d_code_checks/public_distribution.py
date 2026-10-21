"""Public distribution excludes private project records and retains asset licenses."""
from pathlib import Path
import subprocess

CALENDAR_LICENSE_ROOT = "apps/D_Code/modules/applications/product_ui/features/time/calendar_data"
REQUIRED_NOTICES = tuple(f"{CALENDAR_LICENSE_ROOT}/{name}" for name in (
    "LICENSE.isee15.txt", "LICENSE.sxtwl.txt", "NOTICE.md"))


def check_public_distribution(root: Path) -> None:
    root = root.resolve()
    indexed = subprocess.run(["git", "ls-files", "-z", "--", "project_doc", *REQUIRED_NOTICES],
                             cwd=root, capture_output=True, check=True).stdout
    names = {p.decode("utf-8") for p in indexed.split(b"\0") if p}
    forbidden = sorted(p for p in names if p == "project_doc" or p.startswith("project_doc/"))
    if forbidden:
        raise AssertionError(f"Private project_doc files are included in the public index ({len(forbidden)})")
    missing = [p for p in REQUIRED_NOTICES if p not in names or not (root / p).is_file()]
    if missing:
        raise AssertionError(f"Required calendar notices are missing or untracked: {missing}")
    ignored = subprocess.run(["git", "check-ignore", "--no-index", "--quiet",
                              "project_doc/.public-boundary-probe"], cwd=root,
                             capture_output=True)
    if ignored.returncode != 0:
        raise AssertionError("The public repository must ignore /project_doc/")
