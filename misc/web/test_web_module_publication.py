"""The pure-server pack must retain the modules published by the browser build."""
import json
from pathlib import Path
import tempfile
import unittest

from build_server_pack import published_modules
from build_web_modules import digest


class PublishedModules(unittest.TestCase):
    def test_stale_archive_or_modified_module_cannot_be_packaged(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            published = root / 'web-modules'
            published.mkdir()
            archives = {}
            for name, filename in (('cgame', 'libcgame.a'), ('ui', 'libui.a'), ('cjson', 'libbundled_cjson.a')):
                path = root / filename
                path.write_bytes(name.encode())
                archives[name] = digest(path)
            modules = {}
            for name in ('cgame', 'ui'):
                path = published / (name + '.mp.wasm32.so')
                path.write_bytes(name.encode())
                modules[name] = {'sha256': digest(path)}
            (published / 'identity.json').write_text(json.dumps({'archives': archives, 'modules': modules}))
            self.assertEqual([path.name for path in published_modules(root)], ['cgame.mp.wasm32.so', 'ui.mp.wasm32.so'])
            (root / 'libcgame.a').write_bytes(b'different build')
            with self.assertRaisesRegex(ValueError, 'archives changed'):
                published_modules(root)
            (root / 'libcgame.a').write_bytes(b'cgame')
            (published / 'ui.mp.wasm32.so').write_bytes(b'changed module')
            with self.assertRaisesRegex(ValueError, 'module changed'):
                published_modules(root)


if __name__ == '__main__':
    unittest.main()
