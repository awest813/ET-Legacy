"""Exact server checksum and atomic asset installation checks."""
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile
import struct
import server_assets as assets

class ServerAssets(unittest.TestCase):
    def test_stable_release_catalog_uses_exact_mod_only_archive(self):
        page = '<a data-href="https://www.etlegacy.com/download/file/723">Win64 archive</a><a href="#" data-href="https://www.etlegacy.com/download/file/727">All supported <b>archive</b></a>'
        with tempfile.TemporaryDirectory() as temp:
            def fetch(url, path, *args):
                self.assertEqual(url, 'https://www.etlegacy.com/download/release/2840')
                path.write_text(page, encoding='utf-8')
            with patch.object(assets, 'download', side_effect=fetch):
                self.assertEqual(assets.official_source('legacy', 'legacy_v2.84.0.pk3', Path(temp)), ('https://www.etlegacy.com/download/file/727', True))
                for invalid in (page.replace('www.etlegacy.com/download/file/727', 'other.test/download/file/727'), page + page,
                                page.replace('/download/file/727', '/download/file/727?redirect=other'), page.replace('All supported', 'Wrong')):
                    with patch.object(assets, 'download', side_effect=lambda url,path,*args: path.write_text(invalid)), self.assertRaises(ValueError):
                        assets.official_source('legacy', 'legacy_v2.84.0.pk3', Path(temp))

    def test_snapshot_catalog_remains_exact_version(self):
        with tempfile.TemporaryDirectory() as temp:
            page = '<a href="/workflow-files/dl/build/etlegacy-mod-v2.86.0-34-gabc123.zip">Mod</a>'
            with patch.object(assets, 'download', side_effect=lambda url,path,*args: path.write_text(page)) as download:
                self.assertEqual(assets.official_source('legacy', 'legacy_v2.86.0-34-gabc123.pk3', Path(temp)), ('https://www.etlegacy.com/workflow-files/dl/build/etlegacy-mod-v2.86.0-34-gabc123.zip', True))
                self.assertEqual(download.call_args.args[0], 'https://www.etlegacy.com/workflow-files')

    def test_md4_vectors_and_zip_order(self):
        for value, expected in [(b'', '31d6cfe0d16ae931b73c59d7e0c089c0'), (b'a','bde52cb31de33e46245e05fbdbd6fb24'), (b'abc','a448017aaf21d8525fc10ae87aa6729d'), (b'message digest','d9130a8164549fe818874806e1c7014b')]:
            self.assertEqual(assets.md4(value).hex(), expected)
        with tempfile.TemporaryDirectory() as temp:
            pack = Path(temp) / 'sample.pk3'
            with zipfile.ZipFile(pack, 'w') as archive:
                archive.writestr('textures/a.tga',b'abc')
                archive.writestr('empty',b'')
            words = struct.unpack('<4I',assets.md4(struct.pack('<I',0x352441c2)))
            expected = words[0]^words[1]^words[2]^words[3]
            self.assertEqual(assets.pack_checksum(pack),expected if expected < 2**31 else expected-2**32)

    def test_verified_install_and_wrong_checksum_never_published(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / 'assets'
            source = Path(temp) / 'sample.pk3'
            with zipfile.ZipFile(source,'w') as archive:
                archive.writestr('textures/a.tga',b'abc')
            checksum = assets.pack_checksum(source)
            def download(url,path,*args,**kwargs):
                self.assertEqual(url,'https://example.com/packs/etmain/sample.pk3')
                path.write_bytes(source.read_bytes())
            with patch.object(assets,'download',download):
                with self.assertRaises(ValueError):
                    assets.install(root,'etmain','sample.pk3',checksum ^ 1,'https://example.com/packs')
                self.assertFalse((root/'etmain/sample.pk3').exists())
                (root/'etmain/broken.pk3').write_bytes(b'broken')
                entry = assets.install(root,'etmain','sample.pk3',checksum,'https://example.com/packs')
                self.assertEqual(entry['name'],'sample.pk3')
                self.assertEqual((root/'etmain/sample.pk3').read_bytes(),source.read_bytes())
            with patch.object(assets,'download',side_effect=AssertionError('cached pack downloaded')):
                assets.install(root,'etmain','sample.pk3',checksum)

    def test_untrusted_paths_and_redirects(self):
        with tempfile.TemporaryDirectory() as temp:
            for game,name in [('etmain','../evil.pk3'),('other','test.pk3'),('etmain','pak0.pk3')]:
                with self.assertRaises(ValueError): assets.install(temp,game,name,1)
        handler=assets.HTTPSOnly()
        from urllib.request import Request
        for url in ('http://example.com/a','https://other.com/a','https://user:pass@example.com/a','https://example.com:444/a'):
            with self.assertRaises(ValueError):
                handler.redirect_request(Request('https://example.com/a'),None,302,'',{},url)

    def test_approved_archive_extracts_only_exact_pack(self):
        with tempfile.TemporaryDirectory() as temp:
            temp=Path(temp); pack=temp/'sample.pk3'; archive=temp/'maps.zip'; root=temp/'assets'
            with zipfile.ZipFile(pack,'w') as out: out.writestr('textures/a.tga',b'abc')
            with zipfile.ZipFile(archive,'w') as out:
                out.writestr('folder/sample.pk3',pack.read_bytes())
                out.writestr('../../ignored.exe',b'never extracted')
            with patch.object(assets,'download',side_effect=lambda url,path,**kwargs: path.write_bytes(archive.read_bytes())):
                entry=assets.install(root,'etmain','sample.pk3',assets.pack_checksum(pack),source_url='https://example.com/maps.zip')
            self.assertEqual(entry['name'],'sample.pk3')
            self.assertEqual((root/'etmain/sample.pk3').read_bytes(),pack.read_bytes())
            self.assertFalse((temp/'ignored.exe').exists())

    def test_missing_mirror_pack_falls_back_to_official_catalog(self):
        with tempfile.TemporaryDirectory() as temp:
            temp=Path(temp); pack=temp/'sample.pk3'
            with zipfile.ZipFile(pack,'w') as out: out.writestr('textures/a.tga',b'abc')
            def fetch(url,path,**kwargs):
                if url.startswith('https://mirror.test'):
                    raise assets.urllib.error.HTTPError(url,404,'missing',{},None)
                self.assertEqual(url,'https://www.etlegacy.com/packages/download/1')
                path.write_bytes(pack.read_bytes())
            with patch.object(assets,'download',fetch), patch.object(assets,'official_source',return_value=('https://www.etlegacy.com/packages/download/1',False)):
                assets.install(temp/'assets','etmain','sample.pk3',assets.pack_checksum(pack),'https://mirror.test')
            self.assertTrue((temp/'assets/etmain/sample.pk3').is_file())

    def test_cancelled_transfer_removes_staging_and_releases_lock(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)/'assets'; pack=Path(temp)/'sample.pk3'
            with zipfile.ZipFile(pack,'w') as out: out.writestr('textures/a.tga',b'abc')
            stages=[]
            def report(value):
                stages.append(value['stage'])
                if value['stage']=='downloading': raise assets.DownloadCancelled()
            def fetch(url,path,progress=None):
                path.write_bytes(pack.read_bytes())
                progress(stage='downloading',loaded=3,total=3)
            with patch.object(assets,'download',fetch), self.assertRaises(assets.DownloadCancelled):
                assets.install(root,'etmain','sample.pk3',assets.pack_checksum(pack),source_url='https://example.com/sample.pk3',progress=report)
            self.assertEqual(list((root/'etmain').iterdir()),[])
            self.assertFalse(assets._lock.locked())
            self.assertEqual(stages,['waiting','checking','resolving','downloading'])

    def test_progress_through_verification_and_bounded_queue_wait(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)/'assets'; pack=Path(temp)/'sample.pk3'
            with zipfile.ZipFile(pack,'w') as out: out.writestr('textures/a.tga',b'abc')
            stages=[]
            with patch.object(assets,'download',side_effect=lambda url,path,**kwargs: path.write_bytes(pack.read_bytes())):
                assets.install(root,'etmain','sample.pk3',assets.pack_checksum(pack),source_url='https://example.com/sample.pk3',progress=stages.append)
            from itertools import groupby
            self.assertEqual([stage for stage,_ in groupby(value['stage'] for value in stages)],['waiting','checking','resolving','verifying','publishing'])
            assets._lock.acquire()
            try:
                with patch.object(assets.time,'monotonic',side_effect=[0,0,211]), self.assertRaises(TimeoutError):
                    assets.install(root,'etmain','sample.pk3',1)
                self.assertTrue(assets._lock.locked(),'A waiting client must not release another transfer lock')
            finally: assets._lock.release()

    def test_download_reports_unknown_size_and_rejects_short_body(self):
        from io import BytesIO
        class Response(BytesIO):
            status=200
            headers={}
        class Opener:
            def open(self,*args,**kwargs): return response
        with tempfile.TemporaryDirectory() as temp, patch.object(assets.urllib.request,'build_opener',return_value=Opener()):
            response=Response(b'abc'); values=[]
            assets.download('https://example.com/sample.pk3',Path(temp)/'pack',progress=lambda **value: values.append(value))
            self.assertEqual(values[-1],{'stage':'downloading','loaded':3,'total':0})
            response=Response(b'abc'); response.headers={'Content-Length':'4'}
            with self.assertRaisesRegex(ValueError,'Incomplete'): assets.download('https://example.com/sample.pk3',Path(temp)/'pack')

    def test_errors_are_actionable_without_private_paths(self):
        self.assertEqual(assets.public_error(TimeoutError('/private/path'))[0],'timeout')
        code,message=assets.public_error(ValueError('Downloaded pack checksum wrong /private/path'))
        self.assertEqual(code,'checksum'); self.assertNotIn('/private',message)
        self.assertEqual(assets.public_error(assets.urllib.error.URLError(TimeoutError('source timeout')))[0],'timeout')
        self.assertEqual(assets.public_error(zipfile.BadZipFile("Bad CRC-32 for file 'textures/wall64.tga'"))[0],'invalid')
        self.assertEqual(assets.public_error(ValueError('Expanded pack is too large'))[0],'limit')

    def test_cancel_during_archive_validation(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)/'assets'; pack=Path(temp)/'sample.pk3'; checks=0
            with zipfile.ZipFile(pack,'w',compression=zipfile.ZIP_DEFLATED) as out:
                out.writestr('textures/large.tga',b'a' * (3*1048576))
            def report(value):
                nonlocal checks
                if value['stage']=='verifying':
                    checks+=1
                    if checks==3: raise assets.DownloadCancelled()
            with patch.object(assets,'download',side_effect=lambda url,path,**kwargs: path.write_bytes(pack.read_bytes())), self.assertRaises(assets.DownloadCancelled):
                assets.install(root,'etmain','sample.pk3',assets.pack_checksum(pack),source_url='https://example.com/sample.pk3',progress=report)
            self.assertEqual(list((root/'etmain').iterdir()),[])
            self.assertFalse(assets._lock.locked())

    def test_pack_limit_rechecked_when_concurrent_import_fills_last_slot(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)/'assets'; directory=root/'etmain'; directory.mkdir(parents=True)
            pack=Path(temp)/'sample.pk3'
            with zipfile.ZipFile(pack,'w') as out: out.writestr('textures/a.tga',b'new')
            for number in range(63):
                with zipfile.ZipFile(directory/f'pack{number}.pk3','w') as out: out.writestr('textures/a.tga',b'old')
            def report(value):
                if value['stage']=='publishing':
                    (directory/'imported.pk3').write_bytes((directory/'pack0.pk3').read_bytes())
            with patch.object(assets,'download',side_effect=lambda url,path,**kwargs: path.write_bytes(pack.read_bytes())), self.assertRaisesRegex(ValueError,'64'):
                assets.install(root,'etmain','sample.pk3',assets.pack_checksum(pack),source_url='https://example.com/sample.pk3',progress=report)
            self.assertEqual(len(list(directory.glob('*.pk3'))),64)
            self.assertFalse((directory/'sample.pk3').exists())

if __name__ == '__main__': unittest.main()
