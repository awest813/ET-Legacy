"""Server discovery uses disposable UDP peers, never public game servers."""
import socket
import threading
import unittest
import json
import tempfile
from pathlib import Path
from unittest.mock import patch

import server_browser as browser


def packet(challenge='audit', **values):
    info = dict(challenge=challenge, protocol='84', gamename='et', game='legacy', version='ET Legacy v2.86.0-34-g50cffc8 linux-x86_64 Oct 3 2026', pure='1', wasmModules='1', wasmCgame='000004d2:100', wasmUI='0000162e:200',
                hostname='^1Audit ^7server', mapname='oasis', clients='8', humans='3',
                sv_maxclients='16', needpass='0')
    info.update(values)
    return b'\xff\xff\xff\xffinfoResponse\n' + ''.join('\\'+key+'\\'+value for key, value in info.items()).encode()


class ModuleMetadata(unittest.TestCase):
    def test_build_metadata_validation(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'identity.json'
            with patch.object(browser, 'MODULE_MANIFEST', path):
                self.assertEqual(browser.browser_module_identities(), {})
                valid = {'modules': {'cgame': {'crc':1234, 'size':100}, 'ui': {'crc':5678, 'size':200}}}
                path.write_text(json.dumps(valid), encoding='utf-8')
                self.assertEqual(browser.browser_module_identities(), {'wasmCgame':'000004d2:100', 'wasmUI':'0000162e:200'})
                for field, value in (('crc', -1), ('crc', 0x100000000), ('crc', True),
                                     ('size', 0), ('size', '200'), ('size', None)):
                    invalid = json.loads(json.dumps(valid))
                    invalid['modules']['ui'][field] = value
                    path.write_text(json.dumps(invalid), encoding='utf-8')
                    self.assertEqual(browser.browser_module_identities(), {})
                for text in ('invalid JSON', '[]', '{}', '{"modules":null}'):
                    path.write_text(text, encoding='utf-8')
                    self.assertEqual(browser.browser_module_identities(), {})


class ServerInfo(unittest.TestCase):
    def setUp(self):
        identity = patch.object(browser, 'browser_module_identities', return_value={'wasmCgame':'000004d2:100', 'wasmUI':'0000162e:200'})
        identity.start()
        self.addCleanup(identity.stop)

    def test_pure_requires_both_current_module_identities(self):
        for values in ({'wasmCgame':''}, {'wasmUI':''}, {'wasmCgame':'000004d3:100'},
                       {'wasmUI':'0000162e:201'}, {'wasmUI':'garbage'}):
            self.assertFalse(browser.parse_info(packet(**values), 'audit', 0)['compatible'])
        with patch.object(browser, 'browser_module_identities', return_value={}):
            self.assertFalse(browser.parse_info(packet(), 'audit', 0)['compatible'])
            self.assertTrue(browser.parse_info(packet(pure='0'), 'audit', 0)['compatible'])

    def test_details_and_compatibility(self):
        info = browser.parse_info(packet(), 'audit', 12.5)
        self.assertEqual((info['hostname'], info['players'], info['humans'], info['capacity']), ('Audit server', 8, 3, 16))
        self.assertTrue(info['compatible'])
        self.assertEqual(info['map'], 'oasis')
        for values in ({'protocol':'83'}, {'game':'etpro'}, {'gamename':'other'}, {'game':'le^1gacy'}):
            self.assertFalse(browser.parse_info(packet(**values), 'audit', 0)['compatible'])
        self.assertTrue(browser.parse_info(packet(needpass='1'), 'audit', 0)['password'])

    def test_older_or_unknown_versions_are_not_browser_compatible(self):
        for version in ('ET Legacy v2.84.0 linux-x86_64 May 18 2026', 'ET Legacy v2.86.0', 'ET Legacy v2.86.1', '', 'other v2.86.0', 'ET Legacy v2.8x.0', 'ET Le^1gacy v2.90.0', 'ET Legacy v2.86.0-34-g50cffc8bad'):
            value = browser.parse_info(packet(version=version,wasmModules='0'), 'audit', 0)
            self.assertFalse(value['compatible'])
            self.assertIn('WebAssembly', value['reason'])
        for version in ('ET Legacy v2.86.0-34-g50cffc8', 'ET Legacy v2.86.0-34-g50cffc8 linux-x86_64 Oct 3 2026'):
            self.assertTrue(browser.parse_info(packet(version=version), 'audit', 0)['compatible'])
        self.assertTrue(browser.parse_info(packet(version='ET Legacy v2.86.0',pure='0'), 'audit', 0)['compatible'])
        self.assertFalse(browser.parse_info(packet(version='ET Legacy v2.86.0',pure='unknown'), 'audit', 0)['compatible'])

    def test_engine_version_cannot_stand_in_for_current_pure_mod_pack(self):
        for version in ('ET Legacy 2.86-dirty win-x64 Oct 8 2026', 'ET Legacy 2.86.0 win-x64', 'ET Legacy v2.86-dirty'):
            self.assertTrue(browser.parse_info(packet(version=version), 'audit', 0)['compatible'])
            self.assertFalse(browser.parse_info(packet(version=version,wasmModules='0'), 'audit', 0)['compatible'])
        for declaration in ('', '0', 'yes', 'unknown'):
            self.assertFalse(browser.parse_info(packet(wasmModules=declaration), 'audit', 0)['compatible'])
        self.assertTrue(browser.parse_info(packet(version='ET Legacy v2.86.1',wasmModules='1'), 'audit', 0)['compatible'])
        self.assertFalse(browser.parse_info(packet(version='ET Legacy v2.84.0',wasmModules='1'), 'audit', 0)['compatible'])

    def test_untrusted_fields_are_bounded(self):
        info = browser.parse_info(packet(hostname='^3<markup>\x01\u202e'+'a'*300, mapname='m'*200,
                                        clients='-1', humans='99999', sv_maxclients='unknown'), 'audit', 5)
        self.assertEqual(len(info['hostname']), 160)
        self.assertNotIn('\u202e', info['hostname'])
        self.assertEqual(len(info['map']), 64)
        self.assertIsNone(info['players'])
        self.assertIsNone(info['humans'])
        self.assertIsNone(info['capacity'])

    def test_invalid_or_spoofed_packets_are_rejected(self):
        for data in (packet('wrong'), b'wrong', packet()+b'\\challenge\\audit',
                     packet()+b'\\broken', packet(hostname='a'*9000), packet(protocol='x')):
            with self.subTest(data=data[:40]), self.assertRaises(ValueError):
                browser.parse_info(data, 'audit', 0)

    def test_udp_query_ignores_unmatched_challenge(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            udp.bind(('127.0.0.1',0)); udp.settimeout(2)
            errors=[]
            def reply():
                try:
                    data, peer = udp.recvfrom(1000)
                    self.assertTrue(data.startswith(b'\xff\xff\xff\xffgetinfo '))
                    challenge = data.split(b' ')[1].strip().decode()
                    udp.sendto(packet('wrong'), peer)
                    udp.sendto(packet(challenge), peer)
                except Exception as error:
                    errors.append(error)
            worker=threading.Thread(target=reply); worker.start()
            info=browser.probe_server('127.0.0.1',udp.getsockname()[1])
            worker.join(3)
            self.assertFalse(errors)
            self.assertEqual(info['status'],'online')

    def test_resolution_bounds_duplicates_and_empty_addresses(self):
        addresses=[(socket.AF_INET6,socket.SOCK_DGRAM,0,'',('::1',i,0,0)) for i in range(10)]
        addresses += [(socket.AF_INET,socket.SOCK_DGRAM,0,'',('127.0.0.1',i)) for i in range(10)]
        targets=browser.resolved_targets(addresses+addresses)
        self.assertEqual(len(targets),8)
        self.assertEqual([family for family,_ in targets],[socket.AF_INET6,socket.AF_INET]*4)
        with self.assertRaises(TimeoutError):browser.probe_resolved_server([])
        with patch.object(browser,'_probe_target',side_effect=[OSError('IPv6 unavailable'),{'status':'online'}]) as probe:
            info,family,target=browser.probe_resolved_server(addresses)
            self.assertEqual((info['status'],family,target),('online',socket.AF_INET,('127.0.0.1',0)))
            self.assertEqual(probe.call_count,2)

    def test_hostname_tries_another_address_after_silence(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as silent, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as live:
            silent.bind(('127.0.0.1', 0));live.bind(('127.0.0.1', 0));live.settimeout(2)
            targets=[(socket.AF_INET, socket.SOCK_DGRAM, 0, '', udp.getsockname()) for udp in (silent,live)]
            errors=[]
            def reply():
                try:
                    data,peer=live.recvfrom(1000)
                    live.sendto(packet(data.split(b' ')[1].strip().decode()),peer)
                except Exception as error:errors.append(error)
            worker=threading.Thread(target=reply);worker.start()
            try:
                with patch.object(browser.socket,'getaddrinfo',return_value=targets):
                    info=browser.probe_server('dual.example',27960,timeout=.4)
                self.assertEqual(info['status'],'online')
            finally:worker.join(3)
            self.assertFalse(errors)

    def test_cache_and_configured_target_changes(self):
        cache=browser.ServerBrowser()
        with patch.object(browser,'probe_server',return_value={'status':'online'}) as probe:
            self.assertEqual(cache.get('one.example',27960)['status'],'online')
            cache.get('one.example',27960)
            self.assertEqual(probe.call_count,1)
            cache.get('two.example',27961)
            self.assertEqual(probe.call_args.args,('two.example',27961))
        with patch.object(browser,'probe_server',side_effect=OSError('DNS unavailable')):
            self.assertEqual(cache.get('three.example',27960),{'status':'unavailable'})

    def test_pending_probe_is_shared_and_stale_data_is_not_reported(self):
        cache=browser.ServerBrowser(ttl=0,wait=0.01)
        release=threading.Event()
        def slow(*args):
            release.wait(2)
            return {'status':'online'}
        cache.cached=(('one.example',27960),0,{'status':'online'})
        with patch.object(browser,'probe_server',side_effect=slow) as probe:
            try:
                for host in ('one.example','one.example','two.example'):
                    self.assertEqual(cache.get(host,27960),{'status':'unavailable'})
                self.assertEqual(probe.call_count,1,'Refreshes cannot start a probe storm or enqueue another target')
                done=cache.pending[1]
            finally:
                release.set()
            self.assertTrue(done.wait(2))

    def test_invalid_operator_targets_do_not_query(self):
        with patch.object(browser,'probe_server') as probe:
            cache=browser.ServerBrowser()
            for host,port in (('',27960),('host',True),('host','27960'),('host',0),('host',65536)):
                self.assertEqual(cache.get(host,port),{'status':'unavailable'})
            probe.assert_not_called()


if __name__ == '__main__':
    unittest.main()
