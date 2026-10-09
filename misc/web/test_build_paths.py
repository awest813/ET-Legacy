"""A selected release must provide both the served bytes and pure identities."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

WEB = Path(__file__).resolve().parent
ROOT = WEB.parents[1]
PROBE = '''import json,sys
sys.path.insert(0,sys.argv[1])
import serve,server_browser
print(json.dumps({'build':serve.BUILD,'manifest':str(server_browser.MODULE_MANIFEST),
                  'identities':server_browser.browser_module_identities()}))
'''


class ReleasePathTests(unittest.TestCase):
    def probe(self, cwd, selected=None):
        env = os.environ.copy()
        env.pop('ETWASM_BUILD', None)
        if selected is not None:
            env['ETWASM_BUILD'] = str(selected)
        return json.loads(subprocess.check_output(
            [sys.executable, '-c', PROBE, str(WEB)], cwd=cwd, env=env, text=True))

    def test_default_release_is_repository_build(self):
        result = self.probe(ROOT)
        self.assertEqual(Path(result['build']), ROOT / 'build_wasm')
        self.assertEqual(Path(result['manifest']), ROOT / 'build_wasm/web-modules/identity.json')

    def test_selected_release_drives_files_and_compatibility(self):
        with tempfile.TemporaryDirectory() as directory:
            release = Path(directory) / 'downloaded release'
            metadata = release / 'web-modules/identity.json'
            metadata.parent.mkdir(parents=True)
            metadata.write_text(json.dumps({'modules': {
                'cgame': {'crc': 17, 'size': 23}, 'ui': {'crc': 31, 'size': 47}}}), encoding='utf-8')
            for supplied in (release, 'downloaded release'):
                with self.subTest(supplied=supplied):
                    result = self.probe(directory, supplied)
                    self.assertEqual(Path(result['build']), release.resolve())
                    self.assertEqual(Path(result['manifest']), metadata.resolve())
                    self.assertEqual(result['identities'], {
                        'wasmCgame': '00000011:23', 'wasmUI': '0000001f:47'})

    def test_missing_selected_manifest_does_not_use_local_build(self):
        with tempfile.TemporaryDirectory() as directory:
            result = self.probe(directory, directory)
            self.assertEqual(result['identities'], {})

    def test_invalid_selected_manifest_fails_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'web-modules/identity.json'
            path.parent.mkdir()
            path.write_text('not a manifest', encoding='utf-8')
            result = self.probe(directory, directory)
            self.assertEqual(result['identities'], {})


if __name__ == '__main__':
    unittest.main()
