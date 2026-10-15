from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class LauncherTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.directory = Path(self.tmp.name)
        self.mock = self.directory / 'commands'
        self.mock.mkdir()
        shutil.copy(ROOT / 'run-root.sh', self.directory)
        (self.directory / 'libEncore.so').write_bytes(b'test placeholder; loader validates real ELF')
        self.command('id', 'echo "${TEST_UID:-0}"')
        self.command('uname', 'echo "${TEST_ARCH:-aarch64}"')
        self.command('dumpsys', 'echo "  versionName=${TEST_VERSION:-1.14.11949}"')
        self.command('pidof', 'echo "${TEST_PIDS:-1234}"')
        loader = self.directory / 'encore-loader'
        loader.write_text('#!/bin/sh\nprintf "%s\\n" "$@" > "$TEST_CAPTURE"\nexit "${TEST_LOADER_EXIT:-0}"\n')
        loader.chmod(0o755)
        self.env = dict(os.environ, PATH=str(self.mock)+':'+os.environ['PATH'], TEST_CAPTURE=str(self.directory / 'invocation'))

    def tearDown(self): self.tmp.cleanup()

    def command(self, name, body):
        path = self.mock / name
        path.write_text('#!/bin/sh\n'+body+'\n')
        path.chmod(0o755)

    def run_script(self, mode='--load', **env):
        result = subprocess.run(['sh', str(self.directory/'run-root.sh'), mode], env=dict(self.env, **env), capture_output=True, text=True)
        return result.returncode

    def test_check_does_not_inject(self):
        self.assertEqual(self.run_script('--check'), 0)
        self.assertFalse((self.directory/'invocation').exists())

    def test_guard_failures(self):
        for expected, env in [
            (1, {'TEST_UID':'2000'}), (2, {'TEST_ARCH':'x86_64'}),
            (4, {'TEST_VERSION':'1.15'}), (5, {'TEST_PIDS':'1234 5678'}),
            (5, {'TEST_PIDS':'not-a-pid'}),
        ]:
            with self.subTest(env=env): self.assertEqual(self.run_script(**env), expected)
        self.assertEqual(self.run_script('--unknown'), 2)
        self.assertFalse((self.directory/'invocation').exists())

    def test_missing_binary(self):
        (self.directory/'encore-loader').unlink()
        self.assertEqual(self.run_script(), 3)

    def test_exit_code_and_quoted_path(self):
        library = self.directory / 'with spaces.so'
        library.write_bytes(b'test')
        self.assertEqual(self.run_script(ENCORE_LIBRARY_PATH=str(library), TEST_LOADER_EXIT='7'), 7)
        self.assertEqual((self.directory/'invocation').read_text().splitlines(), ['1234', str(library)])

if __name__ == '__main__': unittest.main()
