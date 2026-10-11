"""Exercise the real Bash build entry point without downloading or compiling."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
GIT_BASH = Path('C:/Program Files/Git/bin/bash.exe')
BASH = str(GIT_BASH) if os.name == 'nt' and GIT_BASH.exists() else shutil.which('bash')


@unittest.skipUnless(BASH, 'Bash is required for the build script fixture')
class BuildScriptTests(unittest.TestCase):
    def test_help_needs_no_sdk_or_tools(self):
        env = os.environ.copy()
        env.pop('EMSDK', None)
        env['PATH'] = ''
        result = subprocess.run([BASH, str(ROOT/'build_wasm.sh'), '--help'],
                                env=env, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('configure [CMake options]', result.stdout)
        self.assertIn('etl -j2', result.stdout)

    def test_configure_forwards_cmake_options_with_spaces(self):
        with tempfile.TemporaryDirectory(prefix='wasm build ') as directory:
            root = Path(directory)
            script = root/'build_wasm.sh'
            script.write_text((ROOT/'build_wasm.sh').read_text(encoding='utf-8'), encoding='utf-8')
            sdk = root/'sdk'
            tools = sdk/'upstream/emscripten'
            tools.mkdir(parents=True)
            wrapper = tools/'emcmake'
            wrapper.write_text('#!/bin/bash\nprintf \'%s\\n\' "$@" > "$BUILD_TEST_LOG"\n', encoding='utf-8')
            wrapper.chmod(0o755)
            env = os.environ.copy()
            env['EMSDK'] = ('/c' + sdk.as_posix()[2:]) if os.name == 'nt' else sdk.as_posix()
            env['BUILD_TEST_LOG'] = (root/'arguments.txt').as_posix()
            result = subprocess.run([BASH, str(script), 'configure', '-DTEST_VALUE=with spaces'],
                                    env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            args = (root/'arguments.txt').read_text(encoding='utf-8').splitlines()
            self.assertEqual(args[0], 'cmake')
            self.assertIn('-DTEST_VALUE=with spaces', args)
            self.assertTrue(args[-1].replace('\\', '/').endswith('/' + root.name), args[-1])



if __name__ == '__main__':
    unittest.main()
