"""Reject incomplete, stale, or internally inconsistent release artifacts."""
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
import zlib

from verify_bundle import CORE, verify


class BundleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.build = Path(self.temp.name)
        entries = []
        for name in sorted(CORE):
            data = name.encode()
            (self.build / name).write_bytes(data)
            entries.append("{url:'%s',sha256:'%s',size:%s}" %
                           (name, hashlib.sha256(data).hexdigest(), len(data)))
        (self.build / 'sw.js').write_text(','.join(entries), encoding='utf-8')
        publication = self.build / 'web-modules'
        publication.mkdir()
        self.manifest = {'archives': {}, 'modules': {}}
        for name, archive in [('cgame', 'libcgame.a'), ('ui', 'libui.a'),
                              ('cjson', 'libbundled_cjson.a')]:
            data = archive.encode()
            (self.build / archive).write_bytes(data)
            self.manifest['archives'][name] = hashlib.sha256(data).hexdigest()
        for name in ('cgame', 'ui'):
            data = name.encode()
            (publication / (name + '.mp.wasm32.so')).write_bytes(data)
            self.manifest['modules'][name] = {
                'sha256': hashlib.sha256(data).hexdigest(),
                'crc': zlib.crc32(data), 'size': len(data)}
        self.save_manifest()

    def save_manifest(self):
        (self.build / 'web-modules/identity.json').write_text(
            json.dumps(self.manifest), encoding='utf-8')

    def test_complete_bundle(self):
        self.assertEqual(verify(self.build), 7)

    def test_changed_browser_file(self):
        (self.build / 'etl.wasm').write_bytes(b'x' * len(b'etl.wasm'))
        with self.assertRaisesRegex(ValueError, 'Browser bundle changed'):
            verify(self.build)

    def test_incorrect_core_size_metadata(self):
        worker = self.build / 'sw.js'
        original = worker.read_text(encoding='utf-8')
        worker.write_text(original.replace('size:8}', 'size:9}'), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'size changed'):
            verify(self.build)

    def test_missing_browser_file(self):
        (self.build / 'etl.data').unlink()
        with self.assertRaises(FileNotFoundError):
            verify(self.build)

    def test_invalid_worker_inventory(self):
        worker = self.build / 'sw.js'
        original = worker.read_text(encoding='utf-8')
        for replacement in ('etl.js', '../etl.html', 'other.html'):
            with self.subTest(replacement=replacement):
                worker.write_text(original.replace("url:'etl.html'",
                                  "url:'%s'" % replacement), encoding='utf-8')
                with self.assertRaisesRegex(ValueError, 'exactly once'):
                    verify(self.build)

    def test_stale_archives_and_modules(self):
        for relative in ('libcgame.a', 'libui.a', 'libbundled_cjson.a',
                         'web-modules/cgame.mp.wasm32.so', 'web-modules/ui.mp.wasm32.so'):
            with self.subTest(relative=relative):
                path = self.build / relative
                original = path.read_bytes()
                path.write_bytes(b'stale')
                with self.assertRaises(ValueError):
                    verify(self.build)
                path.write_bytes(original)

    def test_incorrect_identity_metadata(self):
        for name in ('cgame', 'ui'):
            for field, value in [('crc', 0), ('size', 0), ('crc', True), ('size', True)]:
                with self.subTest(name=name, field=field, value=value):
                    original = self.manifest['modules'][name][field]
                    self.manifest['modules'][name][field] = value
                    self.save_manifest()
                    with self.assertRaisesRegex(ValueError, 'identity changed'):
                        verify(self.build)
                    self.manifest['modules'][name][field] = original
                    self.save_manifest()


if __name__ == '__main__':
    unittest.main()
