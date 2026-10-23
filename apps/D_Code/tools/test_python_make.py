"""Exercise Python selection with the shipped Windows make; no firmware or devices."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import venv


ROOT = Path(__file__).resolve().parents[3]
RULES = Path(__file__).with_name('python.mk')


@unittest.skipUnless(os.name == 'nt', 'Uses the shipped Windows make')
class PythonSelection(unittest.TestCase):
    def setUp(self):
        base = Path(os.environ['D_CODE_TEST_OUTPUT'])
        base.mkdir(parents=True, exist_ok=True)
        temporary = tempfile.TemporaryDirectory(prefix='python selection ', dir=base)
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.make = ROOT / 'tools/utils/make.exe'
        self.env = os.environ.copy()
        for key in ('D_PYTHON', 'MAKEFLAGS', 'MFLAGS', 'GNUMAKEFLAGS', 'MAKEFILES'):
            self.env.pop(key, None)
        # The original reported environment: launcher available, no Python directory on PATH.
        self.env['PATH'] = str(ROOT / 'tools/utils') + ';' + os.environ['SystemRoot'] + '/System32;' + os.environ['SystemRoot']
        self.env.update(TEMP=str(self.root), TMP=str(self.root), TMPDIR=str(self.root), OS='Windows_NT')
        self.file = self.root / 'Makefile'
        self.file.write_text(
            '.DEFAULT_GOAL := probe\ninclude ' + RULES.as_posix() + '\n'
            '.PHONY: probe ac791n_D_Code clean help\n'
            'probe ac791n_D_Code:\n\t@$(D_PYTHON) -I -B -c "import sys; print(\'PYTHON_OK=\'+sys.executable)"\n'
            'clean help:\n\t@echo NO_PYTHON_NEEDED\n', encoding='utf-8')

    def run_make(self, *arguments):
        return subprocess.run([str(self.make), '-rR', '-f', str(self.file), *arguments],
                              cwd=self.root, env=self.env, capture_output=True, text=True, timeout=30)

    def test_launcher_only_environment_resolves_full_interpreter(self):
        result = self.run_make('probe')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('PYTHON_OK=', result.stdout)

    def test_default_goal_also_checks_python(self):
        result = self.run_make()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('PYTHON_OK=', result.stdout)

    def test_explicit_interpreter_and_arguments_are_honored(self):
        result = self.run_make('probe', 'D_PYTHON="' + Path(sys.executable).as_posix() + '" -X utf8')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('PYTHON_OK=' + sys.executable, result.stdout)

    def test_invalid_explicit_choice_fails_without_running_target(self):
        result = self.run_make('probe', 'D_PYTHON=missing_python_for_test')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('D_PYTHON cannot start Python 3.10+', result.stderr)
        self.assertNotIn('PYTHON_OK=', result.stdout)

    def test_environment_override_with_spaces_is_supported(self):
        environment = self.root / 'Python with spaces'
        venv.EnvBuilder(with_pip=False).create(environment)
        command = environment / 'Scripts/python.exe'
        self.env['D_PYTHON'] = '"' + command.as_posix() + '"'
        result = self.run_make('probe')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('PYTHON_OK=', result.stdout)

    def test_unsupported_interpreter_is_rejected(self):
        command = self.root / 'old python.cmd'
        command.write_text('@echo off\r\necho Python 3.8\r\n', encoding='ascii')
        result = self.run_make('probe', 'D_PYTHON="' + command.as_posix() + '"')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('D_PYTHON cannot start Python 3.10+', result.stderr)

    def test_clean_and_help_do_not_require_python(self):
        result = self.run_make('clean', 'help', 'D_PYTHON=missing_python_for_test')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('NO_PYTHON_NEEDED', result.stdout)

    def test_mixed_clean_and_build_still_checks_python(self):
        result = self.run_make('clean', 'probe', 'D_PYTHON=missing_python_for_test')
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn('NO_PYTHON_NEEDED', result.stdout)

    def test_existing_vscode_entry_uses_the_shared_selection(self):
        entry = self.root / '.vscode/winmk.bat'
        entry.parent.mkdir()
        shutil.copyfile(ROOT / '.vscode/winmk.bat', entry)
        result = subprocess.run([os.environ['COMSPEC'], '/d', '/c', '.vscode\\winmk.bat ac791n_D_Code'],
                                cwd=self.root, env=self.env, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('PYTHON_OK=', result.stdout)


if __name__ == '__main__':
    unittest.main()
