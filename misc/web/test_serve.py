"""Exercise preview routes using disposable files and a loopback HTTP server."""
import hashlib
import json
import zipfile
import zlib
import functools
import http.client
import http.server
import os
import tempfile
import threading
import socket
import struct
import unittest
from unittest.mock import patch

import serve
from public_servers import resolve_ticket


class PreviewRoutes(unittest.TestCase):
    def test_explicit_config_is_shared_without_replacing_default(self):
        with tempfile.TemporaryDirectory() as root:
            config = os.path.join(root, 'fixture.json')
            settings = {'server':'127.0.0.1', 'udpPort':27961, 'serverLabel':'Private fixture'}
            with open(config, 'w', encoding='utf-8') as stream:
                json.dump(settings, stream)
            with patch.dict(os.environ, {'ETWASM_SERVER_CONFIG':config}):
                self.assertEqual(serve.online_settings(), settings)
                with open(config, 'w', encoding='utf-8') as stream:
                    stream.write('[]')
                self.assertEqual(serve.online_settings(), {})

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.build = os.path.join(self.temp.name, "build")
        self.assets = os.path.join(self.temp.name, "assets")
        self.custom = os.path.join(self.temp.name, "custom")
        os.makedirs(self.build)
        os.makedirs(self.assets)
        for root, name, data in ((self.build, "etl.html", b"launcher"),
                                 (self.build, "etl.wasm", b"engine"),
                                 (self.assets, "pak0.pk3", b"pack"),
                                 (self.build, "debug.log", b"private log")):
            with open(os.path.join(root, name), "wb") as stream:
                stream.write(data)
        self.paths = patch.multiple(serve, BUILD=self.build, ASSETS=self.assets, CUSTOM_ASSETS=self.custom)
        self.paths.start()
        self.server_probe = patch.object(serve._server_browser, 'get', return_value={'status':'unavailable'})
        self.probe = self.server_probe.start()
        class QuietHandler(serve.Handler):
            def log_message(self, *_):
                pass
        self.server = http.server.ThreadingHTTPServer(
            ("127.0.0.1", 0), functools.partial(QuietHandler, directory=self.build))
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()
        self.paths.stop()
        self.server_probe.stop()
        self.temp.cleanup()

    def request(self, path, method="GET", headers=None):
        connection = http.client.HTTPConnection("127.0.0.1", self.server.server_port)
        connection.request(method, path, headers=headers or {})
        response = connection.getresponse()
        result = response.status, dict(response.getheaders()), response.read()
        connection.close()
        return result

    def test_expected_files_and_query_parameters(self):
        self.assertEqual(self.request("/?map=oasis")[2], b"launcher")
        self.assertEqual(self.request("/assets/pak0.pk3")[2], b"pack")
        status, headers, body = self.request("/etl.wasm", "HEAD")
        self.assertEqual(status, 200)
        self.assertEqual(body, b"")
        self.assertEqual(headers["Content-type"], "application/wasm")
        self.assertEqual(headers["Cross-Origin-Embedder-Policy"], "require-corp")

    def test_server_asset_route_requires_same_origin_and_opt_in(self):
        def request(headers, method='GET'):
            connection=http.client.HTTPConnection('127.0.0.1',self.server.server_port)
            connection.request(method,'/network/assets/etmain/sample.pk3?checksum=-123',headers=headers)
            response=connection.getresponse(); result=response.status,response.read(); connection.close(); return result
        with patch.object(serve,'online_settings',return_value={'relay':'ws://localhost:8082/relay','autoAssets':True}), patch.object(serve,'install_server_pack',return_value={'game':'etmain','name':'sample.pk3'}) as install:
            self.assertEqual(request({})[0],403)
            self.assertEqual(request({'X-ETL-Assets':'1','Sec-Fetch-Site':'cross-site'})[0],403)
            self.assertEqual(request({'X-ETL-Assets':'1','Sec-Fetch-Site':'same-origin'},'HEAD')[0],403)
            install.assert_not_called()
            status,body=request({'X-ETL-Assets':'1','Sec-Fetch-Site':'same-origin'})
            self.assertEqual(status,200); self.assertEqual(json.loads(body)['installed']['checksum'],-123)
            install.assert_called_once_with(self.custom,'etmain','sample.pk3',-123,'','')
        with patch.object(serve,'online_settings',return_value={'relay':'ws://localhost:8082/relay','autoAssets':False}):
            self.assertEqual(request({'X-ETL-Assets':'1','Sec-Fetch-Site':'same-origin'})[0],403)

    def test_paths_cannot_escape_or_list_files(self):
        for path in ("/debug.log", "/assets/", "/build/", "/assets/../build/debug.log",
                     "/assets/%2e%2e%2fbuild%2fdebug.log", "/assets/..%5cbuild%5cdebug.log",
                     "/C:/Windows/win.ini", "/etl.wasm/../debug.log"):
            with self.subTest(path=path):
                self.assertEqual(self.request(path)[0], 404)

    def test_server_pack_progress_stream_and_actionable_error(self):
        def fetch(*args,progress):
            progress({'stage':'downloading','loaded':5,'total':10})
            progress({'stage':'verifying'})
            return {'game':'etmain','name':'sample.pk3'}
        def request():
            connection=http.client.HTTPConnection('127.0.0.1',self.server.server_port)
            connection.request('GET','/network/assets/etmain/sample.pk3?checksum=-123',headers={'X-ETL-Assets':'1','Sec-Fetch-Site':'same-origin','Accept':'application/x-ndjson'})
            response=connection.getresponse(); result=response.status,response.getheader('Content-Type'),[json.loads(line) for line in response.read().splitlines()]; connection.close(); return result
        with patch.object(serve,'online_settings',return_value={'relay':'ws://localhost:8082/relay','autoAssets':True}):
            with patch.object(serve,'install_server_pack',fetch):
                status,mime,events=request()
                self.assertEqual((status,mime),(200,'application/x-ndjson'))
                self.assertEqual(events[0],{'stage':'downloading','loaded':5,'total':10})
                self.assertEqual(events[-1]['installed']['checksum'],-123)
            with patch.object(serve,'install_server_pack',side_effect=ValueError('Downloaded pack checksum wrong /private/path')):
                _,_,events=request()
                self.assertEqual(events[-1]['error'],'checksum')
                self.assertNotIn('/private',str(events))

    def test_browser_abort_stops_streamed_installer(self):
        disconnected=threading.Event(); proceed=threading.Event()
        def install(*args,progress):
            progress({'stage':'downloading','loaded':0,'total':100})
            proceed.wait(3)
            try:
                for index in range(10): progress({'stage':'verifying' if index % 2 else 'downloading','loaded':100,'total':100})
            except serve.DownloadCancelled:
                disconnected.set(); raise
            raise AssertionError('Disconnected browser kept its installer running')
        with patch.object(serve,'online_settings',return_value={'relay':'ws://localhost:8082/relay','autoAssets':True}), patch.object(serve,'install_server_pack',install):
            connection=http.client.HTTPConnection('127.0.0.1',self.server.server_port)
            connection.request('GET','/network/assets/etmain/sample.pk3?checksum=1',headers={'X-ETL-Assets':'1','Sec-Fetch-Site':'same-origin','Accept':'application/x-ndjson'})
            response=connection.getresponse()
            self.assertEqual(json.loads(response.readline())['stage'],'downloading')
            response.fp.raw._sock.shutdown(socket.SHUT_RDWR)
            response.close(); connection.close(); proceed.set()
            self.assertTrue(disconnected.wait(3),'A broken progress connection must stop server work')

    def test_missing_allowed_file_is_404(self):
        self.assertEqual(self.request("/assets/pak2.pk3")[0], 404)

    def test_pwa_routes_and_mime_types(self):
        for name, mime in (("sw.js", "text/javascript"), ("manifest.webmanifest", "application/manifest+json"), ("icon-192.png", "image/png")):
            with open(os.path.join(self.build, name), "wb") as stream:
                stream.write(b"app")
            status, headers, body = self.request('/' + name)
            self.assertEqual((status, body), (200, b"app"))
            self.assertEqual(headers['Content-type'], mime)
            self.assertEqual(headers['Cache-Control'], 'no-cache')

    def test_independent_recovery_page_keeps_pack_storage(self):
        status,headers,body=self.request('/network/recovery')
        self.assertEqual(status,200)
        self.assertIn('text/html',headers['Content-Type'])
        self.assertIn(b'Restore offline play',body)
        self.assertIn(b'ACTIVATE_UPDATE',body)
        self.assertNotIn(b'caches.delete',body)
        self.assertNotIn(b'indexedDB.deleteDatabase',body)

    def test_online_configuration_requires_explicit_relay(self):
        with patch.dict(os.environ, {"ETWASM_RELAY_URL":""}):
            self.assertFalse(json.loads(self.request('/network/config.json')[2])['enabled'])
        with patch.dict(os.environ, {"ETWASM_RELAY_URL":"ws://127.0.0.1:8082/relay", "ETWASM_SERVER_LABEL":"Audit server"}):
            config=json.loads(self.request('/network/config.json')[2])
            self.assertTrue(config['enabled'])
            self.assertEqual(config['engineAddress'],'192.0.2.1:27960')
            self.assertEqual(config['serverLabel'],'Audit server')
        for relay_url in ("ws://user:secret@localhost/relay", "ws://[", "ws://localhost:99999/relay", "ws://localhost/", "ws://localhost/wrong"):
            with self.subTest(url=relay_url), patch.dict(os.environ, {"ETWASM_RELAY_URL":relay_url}):
                self.assertFalse(json.loads(self.request('/network/config.json')[2])['enabled'])
        with patch.dict(os.environ, {"ETWASM_SERVER_LABEL":"   "}):
            self.assertEqual(json.loads(self.request('/network/config.json')[2])['serverLabel'], 'ET: Legacy server')

    def test_server_browser_only_queries_operator_target(self):
        details={'status':'online','hostname':'Audit server','map':'oasis','players':4}
        self.probe.return_value=details
        with patch.object(serve,'online_settings',return_value={'relay':'ws://localhost:8082/relay','server':'configured.example','udpPort':27961}), patch.dict(os.environ,{'ETWASM_RELAY_URL':''}):
            config=json.loads(self.request('/network/config.json?server=other.example&udpPort=1234')[2])
            self.assertFalse(config['enabled'])
            self.probe.assert_not_called()
        with patch.object(serve,'online_settings',return_value={'relay':'ws://localhost:8082/relay','server':'configured.example','udpPort':27961}), patch.dict(os.environ,{'ETWASM_RELAY_URL':'ws://localhost:8082/relay'}):
            config=json.loads(self.request('/network/config.json?server=other.example&udpPort=1234')[2])
            self.assertEqual(config['serverInfo'],details)
            self.probe.assert_called_once_with('configured.example',27961)

    def write_packs(self):
        for name in serve.ASSET_FILES:
            with zipfile.ZipFile(os.path.join(self.assets, name), 'w') as archive:
                archive.writestr('maps/test.bsp', b'checked contents')

    def test_public_catalog_and_signed_selected_configuration(self):
        identity, secret = 'a' * 32, 'ab' * 32
        entry = {'id': identity, 'host': '8.8.8.8', 'port': 27961, 'info': {'status': 'online', 'compatible': True, 'hostname': 'Public Fixture', 'map': 'oasis'}}
        catalog = {'version': 1, 'status': 'ready', 'servers': [entry], 'total': 2, 'checked': 2}
        with patch.dict(os.environ, {'ETWASM_RELAY_URL': 'ws://localhost:8082/relay', 'ETWASM_PUBLIC_SECRET': secret}), patch.object(serve._public_catalog, 'get', return_value=catalog) as get, patch.object(serve._public_catalog, 'select', side_effect=lambda key: entry if key == identity else None):
            value = json.loads(self.request('/network/servers.json')[2])
            self.assertEqual(value, catalog)
            self.assertEqual(get.call_count, 1)
            config = json.loads(self.request('/network/config.json?server=' + identity)[2])
            self.assertEqual(config['serverId'], identity)
            self.assertEqual(config['serverInfo'], entry['info'])
            self.assertEqual(config['serverLabel'], 'Public Fixture')
            self.assertTrue(config['publicServers'])
            self.assertEqual(resolve_ticket(secret, config['relay'].split('/relay/')[1])[0], ('8.8.8.8', 27961))
            rotated = 'cd' * 32
            with patch.dict(os.environ, {'ETWASM_PUBLIC_SECRET': rotated}):
                fresh = json.loads(self.request('/network/config.json?server=' + identity)[2])
                self.assertEqual(fresh['serverId'], identity)
                self.assertEqual(resolve_ticket(rotated, fresh['relay'].split('/relay/')[1])[0], ('8.8.8.8', 27961))
                with self.assertRaises(ValueError):
                    resolve_ticket(rotated, config['relay'].split('/relay/')[1])
            self.probe.assert_not_called()
            for query in ('127.0.0.1:27960', identity + '&server=' + identity, 'other.example&udpPort=1234'):
                self.assertFalse(json.loads(self.request('/network/config.json?server=' + query)[2])['enabled'])
            default = json.loads(self.request('/network/config.json')[2])
            self.assertTrue(default['enabled'], 'The default route remains usable when public browsing is enabled')
            self.assertTrue(default['publicServers'])
            self.assertEqual(default['serverId'], '')
            self.assertEqual(default['relay'], 'ws://localhost:8082/relay')
        with patch.dict(os.environ, {'ETWASM_PUBLIC_SECRET': ''}):
            self.assertEqual(json.loads(self.request('/network/servers.json')[2])['status'], 'disabled')

    def test_manifest_hashes_complete_packs_and_refreshes(self):
        self.write_packs()
        status, headers, body = self.request('/assets/manifest.json')
        self.assertEqual(status, 200)
        self.assertEqual(headers['Content-Type'], 'application/json')
        manifest = json.loads(body)
        self.assertEqual(set(manifest['packs']), serve.ASSET_FILES)
        for name, entry in manifest['packs'].items():
            with open(os.path.join(self.assets, name), 'rb') as stream:
                data = stream.read()
            self.assertEqual(entry['size'], len(data))
            self.assertEqual(entry['crc32'], f'{zlib.crc32(data):08x}')
            self.assertEqual(entry['sha256'], hashlib.sha256(data).hexdigest())
        self.assertEqual(self.request('/assets/manifest.json')[2], body)
        with zipfile.ZipFile(os.path.join(self.assets, 'pak0.pk3'), 'a') as archive:
            archive.writestr('new.txt', 'updated')
        self.assertNotEqual(self.request('/assets/manifest.json')[2], body)

    def test_manifest_rejects_corrupt_zip_contents(self):
        self.write_packs()
        path = os.path.join(self.assets, 'pak0.pk3')
        with open(path, 'rb') as stream:
            data = stream.read()
        with open(path, 'wb') as stream:
            stream.write(data.replace(b'checked contents', b'broken! contents'))
        self.assertEqual(self.request('/assets/manifest.json')[0], 503)

    def test_manifest_requires_all_packs(self):
        self.assertEqual(self.request('/assets/manifest.json')[0], 503)

    def test_directory_cannot_be_served_as_launcher(self):
        os.remove(os.path.join(self.build, "etl.html"))
        os.mkdir(os.path.join(self.build, "etl.html"))
        self.assertEqual(self.request("/")[0], 404)

    def custom_pack(self, name='sample.pk3', entries=None, game='etmain'):
        directory = os.path.join(self.custom, game)
        os.makedirs(directory, exist_ok=True)
        path = os.path.join(directory, name)
        bsp = b'IBSP' + struct.pack('<i', 47) + b'\0' * 136
        with zipfile.ZipFile(path, 'w') as archive:
            for entry, data in (entries or {'maps/sample.bsp': bsp}).items():
                archive.writestr(entry, data)
        return path

    def upload(self, data, name='imported.pk3', origin=True, extra_headers=None):
        connection = http.client.HTTPConnection('127.0.0.1', self.server.server_port)
        headers = {'X-ETL-Import':'1', 'Content-Type':'application/octet-stream'}
        if origin:
            headers['Origin'] = f'http://localhost:{self.server.server_port}'
        headers.update(extra_headers or {})
        connection.request('POST', '/assets/import/' + name, data, headers)
        response = connection.getresponse()
        result = response.status, response.read()
        connection.close()
        return result

    def test_custom_catalog_content_addressing_and_integrity(self):
        path = self.custom_pack()
        value = json.loads(self.request('/assets/custom.json')[2])
        entry = value['packs'][0]
        self.assertEqual(entry['maps'], ['sample'])
        route = f"/assets/custom/etmain/{entry['sha256']}/{entry['name']}"
        with open(path, 'rb') as stream:
            self.assertEqual(self.request(route)[2], stream.read())
        self.assertEqual(self.request(route.replace(entry['sha256'], '0' * 64))[0], 404)
        self.assertEqual(self.request('/assets/custom/etmain/sample.pk3')[0], 404)
        self.custom_pack(entries={'maps/changed.bsp':b'IBSP'+struct.pack('<i',47)+b'\0'*136})
        self.assertEqual(self.request(route)[0],404, 'Old versions cannot be fetched using a stale digest')

    def test_catalog_revalidates_only_changed_packs_and_prunes_removed_files(self):
        import custom_assets
        first=self.custom_pack(); second=self.custom_pack(name='second.pk3')
        with patch.object(custom_assets,'inspect_pack',wraps=custom_assets.inspect_pack) as inspect:
            initial=json.loads(self.request('/assets/custom.json')[2]);self.assertEqual(inspect.call_count,2)
            self.custom_pack(name='third.pk3')
            self.assertEqual(len(json.loads(self.request('/assets/custom.json')[2])['packs']),3)
            self.assertEqual(inspect.call_count,3,'New packs must not trigger full revalidation of installed packs')
            with open(second,'wb') as stream: stream.write(b'corrupt archive')
            current=json.loads(self.request('/assets/custom.json')[2]);self.assertEqual(len(current['packs']),2);self.assertEqual(len(current['errors']),1)
            self.assertEqual(inspect.call_count,4)
            self.request('/assets/custom.json');self.assertEqual(inspect.call_count,4,'Unchanged rejected packs must not stall every refresh')
            os.remove(first);self.request('/assets/custom.json')
            self.assertFalse(any(key[1]==first for key in serve._custom_cache),'Deleted packs must leave the validation cache')
            self.custom_pack(name='second.pk3')
            self.assertEqual(len(json.loads(self.request('/assets/custom.json')[2])['packs']),2)
            self.assertEqual(inspect.call_count,5,'Replacing a rejected pack must validate and restore it')

    def test_catalog_rejects_files_changed_during_validation(self):
        import custom_assets
        self.custom_pack()
        inspect=custom_assets.inspect_pack
        def changing(path,game):
            entry=inspect(path,game)
            self.custom_pack(entries={'maps/replaced.bsp':b'IBSP'+struct.pack('<i',47)+b'\0'*136})
            return entry
        with patch.object(custom_assets,'inspect_pack',changing):
            value=json.loads(self.request('/assets/custom.json')[2])
            self.assertFalse(value['packs']);self.assertIn('changed during validation',value['errors'][0]['reason'])
        value=json.loads(self.request('/assets/custom.json')[2]);self.assertEqual(value['packs'][0]['maps'],['replaced'])

    def test_progress_stream_throttles_repeated_validation_checks(self):
        def install(*args,progress):
            for _ in range(65536): progress({'stage':'verifying'})
            return {'game':'etmain','name':'sample.pk3'}
        with patch.object(serve,'online_settings',return_value={'relay':'ws://localhost:8082/relay','autoAssets':True}), patch.object(serve,'install_server_pack',install), patch.object(serve.time,'monotonic',return_value=1):
            connection=http.client.HTTPConnection('127.0.0.1',self.server.server_port)
            connection.request('GET','/network/assets/etmain/sample.pk3?checksum=1',headers={'X-ETL-Assets':'1','Sec-Fetch-Site':'same-origin','Accept':'application/x-ndjson'})
            response=connection.getresponse();events=[json.loads(line) for line in response.read().splitlines()];connection.close()
            self.assertEqual(len(events),2,'Validation heartbeats must not overflow the browser progress budget')
            self.assertEqual(events[-1]['installed']['checksum'],1)

    def test_unsafe_custom_assets_are_skipped(self):
        for entries in ({'../escape.cfg':b'x'}, {'maps/bad.bsp':b'broken'},
                        {'cgame.mp.x86.dll':b'code'}, {'autoexec.cfg':b'commands'}, {'browser-connect.cfg':b'commands'},
                        {'a/b.txt':b'x', 'A/B.TXT':b'other'},
                        {'maps/bad.bsp':b'IBSP'+struct.pack('<i',47)+struct.pack('<ii',144,1)+b'\0'*128}):
            with self.subTest(entries=entries):
                self.custom_pack(entries=entries)
                value = json.loads(self.request('/assets/custom.json')[2])
                self.assertFalse(value['packs'])
                self.assertEqual(len(value['errors']),1)

    def test_map_import_rejects_cross_origin_and_keeps_existing_files(self):
        path = self.custom_pack()
        with open(path,'rb') as stream:
            data = stream.read()
        self.assertEqual(self.upload(data,origin=False)[0],403)
        self.assertEqual(self.upload(data,name='pak0.pk3')[0],400)
        self.assertEqual(self.upload(data,name='../escape.pk3')[0],400)
        self.assertEqual(self.upload(data)[0],200)
        self.assertEqual(self.upload(data)[0],409)
        self.assertEqual(self.upload(b'invalid archive bytes' * 4,name='bad.pk3')[0],400)
        self.assertFalse(os.path.exists(os.path.join(self.custom,'etmain','bad.pk3')))
        self.assertEqual(set(os.listdir(os.path.join(self.custom,'etmain'))),{'sample.pk3','imported.pk3'})

    def test_map_import_capability_and_forwarded_request_rejection(self):
        path = self.custom_pack()
        with open(path, 'rb') as stream:
            data = stream.read()
        for headers in ({'Host':'play.example.org'}, {'X-Forwarded-For':'203.0.113.8'},
                        {'Forwarded':'for=203.0.113.8'}, {'X-Forwarded-Host':'play.example.org'},
                        {'X-Forwarded-Proto':'https'}, {'Via':'1.1 proxy'}):
            with self.subTest(headers=headers):
                self.assertEqual(self.upload(data, extra_headers=headers)[0], 403,
                                 'A proxy cannot grant a remote upload by forwarding a forged local Origin')
                self.assertIs(json.loads(self.request('/assets/custom.json', headers=headers)[2]).get('localImport'), False)
        for origin in (f'http://user@localhost:{self.server.server_port}',
                       f'http://@localhost:{self.server.server_port}',
                       'http://localhost:0',
                       f'http://localhost:{self.server.server_port}?query',
                       f'http://localhost:{self.server.server_port}#fragment'):
            self.assertEqual(self.upload(data, extra_headers={'Origin':origin})[0], 403)
        self.assertFalse(os.path.exists(os.path.join(self.custom, 'etmain', 'imported.pk3')))
        self.assertIs(json.loads(self.request('/assets/custom.json')[2]).get('localImport'), True)

    def test_import_has_a_pack_count_limit(self):
        path = self.custom_pack()
        with open(path,'rb') as stream:
            data = stream.read()
        for number in range(63):
            with open(os.path.join(self.custom,'etmain',f'pack{number}.pk3'),'wb') as stream:
                stream.write(data)
        self.assertEqual(self.upload(data)[0],400)
        self.assertFalse(os.path.exists(os.path.join(self.custom,'etmain','imported.pk3')))

    def test_custom_catalog_rejects_entry_crc_failures(self):
        path = self.custom_pack(entries={'textures/asset.tga':b'original bytes'})
        with open(path,'rb') as stream:
            data = stream.read()
        with open(path,'wb') as stream:
            stream.write(data.replace(b'original bytes',b'corrupt! bytes'))
        value = json.loads(self.request('/assets/custom.json')[2])
        self.assertFalse(value['packs'])
        self.assertEqual(len(value['errors']),1)

    def test_exact_legacy_pack_keeps_checksum_without_loading_modules(self):
        path = self.custom_pack(name='legacy_v2.86.0.pk3', game='legacy', entries={
            'default.cfg':b'normal Legacy defaults', 'cgame.mp.wasm32.so':b'ignored module',
            'ui_mp_x64.dll':b'ignored native module', 'ui/version_generated.h':b'version'})
        with zipfile.ZipFile(path,'a') as archive:
            archive.writestr('ui/version_generated.h', b'version')
        with open(path,'rb') as stream:
            original = stream.read()
        value = json.loads(self.request('/assets/custom.json')[2])
        self.assertFalse(value['errors'])
        entry = value['packs'][0]
        self.assertEqual(entry['sha256'], hashlib.sha256(original).hexdigest())
        self.assertEqual(set(entry['ignoredModules']), {'cgame.mp.wasm32.so','ui_mp_x64.dll'})
        self.assertEqual(self.request(f"/assets/custom/legacy/{entry['sha256']}/{entry['name']}")[2],original)
        self.assertEqual(os.listdir(os.path.dirname(path)),['legacy_v2.86.0.pk3'],'Modules remain inside the unopened PK3, never extracted')
        with zipfile.ZipFile(path,'a') as archive:
            archive.writestr('ui/version_generated.h', b'conflicting version')
        self.assertTrue(json.loads(self.request('/assets/custom.json')[2])['errors'])

    def test_legacy_exception_does_not_allow_other_modules_or_player_settings(self):
        for name in ('autoexec.cfg','etconfig.cfg','browser-connect.cfg','arbitrary.dll','vm/cgame.qvm','other.js'):
            with self.subTest(name=name):
                self.custom_pack(name='legacy_v2.86.pk3',game='legacy',entries={name:b'blocked'})
                value = json.loads(self.request('/assets/custom.json')[2])
                self.assertFalse(value['packs'])
        self.custom_pack(entries={'default.cfg':b'blocked map startup settings'})
        self.assertFalse(json.loads(self.request('/assets/custom.json')[2])['packs'])


if __name__ == "__main__":
    unittest.main()
