"""Regression checks for the public sync boundary; uses only small Git fixtures."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from d_code_checks.public_distribution import REQUIRED_NOTICES, check_public_distribution


class PublicDistributionTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="public_distribution_")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.git("init", "--quiet")
        (self.root / ".gitignore").write_text("/project_doc/\n", encoding="utf-8")
        for name in REQUIRED_NOTICES:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("fixture notice", encoding="utf-8")
        self.git("add", ".gitignore", *REQUIRED_NOTICES)

    def git(self, *args):
        return subprocess.run(["git", *args], cwd=self.root, capture_output=True, check=True)

    def test_clean_public_index(self):
        check_public_distribution(self.root)

    def test_forced_private_addition_is_rejected(self):
        path = self.root / "project_doc/internal.md"
        path.parent.mkdir()
        path.write_text("fixture", encoding="utf-8")
        self.git("add", "-f", "project_doc/internal.md")
        with self.assertRaisesRegex(AssertionError, "Private project_doc"):
            check_public_distribution(self.root)

    def test_missing_ignore_is_rejected(self):
        (self.root / ".gitignore").write_text("", encoding="utf-8")
        with self.assertRaisesRegex(AssertionError, "must ignore"):
            check_public_distribution(self.root)

    def test_untracked_license_is_rejected(self):
        self.git("rm", "--cached", REQUIRED_NOTICES[0])
        with self.assertRaisesRegex(AssertionError, "missing or untracked"):
            check_public_distribution(self.root)

    def test_removed_license_is_rejected(self):
        (self.root / REQUIRED_NOTICES[0]).unlink()
        with self.assertRaisesRegex(AssertionError, "missing or untracked"):
            check_public_distribution(self.root)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work-dir", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    output = args.work_dir.resolve()
    if not output.is_relative_to(root / "apps/D_Code/build/out"):
        parser.error("Use an owned task output under apps/D_Code/build/out")
    output.mkdir(parents=True, exist_ok=True)
    tempfile.tempdir = str(output)
    for name in ("TEMP", "TMP", "TMPDIR"):
        os.environ[name] = str(output)
    unittest.main(argv=[__file__], verbosity=2)
