"""Exercise real runner argument/configuration wiring without opening sockets."""
import argparse
import contextlib
import io
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock, patch

import run_online


class RunnerTests(unittest.TestCase):
    def run_fixture(self, origin=None, release=None, inherited=None, assets=False):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / 'isolated.json'
            config.write_text(json.dumps({'server': '127.0.0.1', 'udpPort': 27961,
                                          'webPort': 8083, 'relayPort': 8084}), encoding='utf-8')
            argv = ['run_online.py', '--config', str(config)]
            if origin is not None: argv += ['--public-origin', origin]
            if release is not None:
                selected = Path(directory) / release
                selected.mkdir()
                argv += ['--build-dir', str(selected)]
            if assets:
                argv += ['--assets-dir', str(Path(directory) / 'original packs')]
            child = Mock()
            child.poll.return_value = 1
            output = io.StringIO()
            with patch('sys.argv', argv), patch.object(run_online.subprocess, 'Popen', return_value=child) as start, \
                    patch.object(run_online.secrets, 'token_hex', return_value='c' * 64), \
                    patch.object(run_online, 'verify_browser', return_value=7), \
                    patch.dict(os.environ, {'ETWASM_BUILD': inherited} if inherited else {}, clear=True), \
                    contextlib.redirect_stdout(output):
                with self.assertRaisesRegex(SystemExit, 'Launcher or relay stopped'):
                    run_online.main()
            self.assertEqual(start.call_count, 2)
            calls = start.call_args_list
            for call in calls:
                self.assertEqual(call.kwargs['env']['ETWASM_BIND'], '127.0.0.1')
                self.assertEqual(call.kwargs['env']['ETWASM_SERVER_CONFIG'], str(config.resolve()))
                self.assertEqual(call.kwargs['env']['ETWASM_PUBLIC_SECRET'], 'c' * 64)
                expected = selected.resolve() if release is not None else (
                    Path(inherited).resolve() if inherited else
                    Path(run_online.__file__).resolve().parents[2] / 'build_wasm')
                self.assertEqual(call.kwargs['env']['ETWASM_BUILD'], str(expected))
                if assets:
                    self.assertEqual(call.kwargs['env']['ETWASM_ASSETS'],
                                     str((Path(directory) / 'original packs').resolve()))
            self.assertNotIn('c' * 64, output.getvalue())
            return calls, output.getvalue()

    def test_local_origins_and_relay(self):
        calls, output = self.run_fixture()
        self.assertEqual(calls[0].args[0][-4:], ['--origin', 'http://localhost:8083', '--origin', 'http://127.0.0.1:8083'])
        self.assertEqual(calls[1].kwargs['env']['ETWASM_RELAY_URL'], 'ws://127.0.0.1:8084/relay')
        self.assertIn('http://localhost:8083/', output)

    def test_tls_proxy_origin_and_relay(self):
        for supplied, expected in [('https://Play.Example.org:443', 'https://play.example.org'),
                                   ('https://play.example.org:8443', 'https://play.example.org:8443'),
                                   ('https://[::1]:8443', 'https://[::1]:8443')]:
            with self.subTest(supplied=supplied):
                calls, output = self.run_fixture(supplied)
                self.assertEqual(calls[0].args[0][-2:], ['--origin', expected])
                self.assertEqual(calls[1].kwargs['env']['ETWASM_RELAY_URL'], expected.replace('https:', 'wss:') + '/relay')
                self.assertIn('/relay and /relay/* -> 127.0.0.1:8084', output)

    def test_release_selection_and_precedence(self):
        self.run_fixture(release='downloaded release')
        self.run_fixture(inherited='existing-release')
        self.run_fixture(release='selected-release', inherited='different-release')

    def test_asset_directory_reaches_both_services(self):
        self.run_fixture(assets=True)

    def test_missing_explicit_release_starts_no_services(self):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / 'test.json'
            config.write_text(json.dumps({'server': '127.0.0.1'}), encoding='utf-8')
            with patch('sys.argv', ['run_online.py', '--config', str(config),
                                   '--build-dir', str(Path(directory) / 'missing')]), \
                    patch.object(run_online.subprocess, 'Popen') as start, \
                    contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as error:
                    run_online.main()
            self.assertEqual(error.exception.code, 2)
            start.assert_not_called()

    def test_invalid_release_starts_no_services(self):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / 'config.json'
            config.write_text(json.dumps({'server': '127.0.0.1'}), encoding='utf-8')
            with patch('sys.argv', ['run_online.py', '--config', str(config),
                                   '--build-dir', directory]), \
                    patch.object(run_online.subprocess, 'Popen') as start, \
                    contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as error:
                    run_online.main()
            self.assertEqual(error.exception.code, 2)
            start.assert_not_called()

    def test_check_validates_packs_without_starting_services(self):
        import zipfile
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            config = root / 'config.json'
            config.write_text(json.dumps({'server': '127.0.0.1'}), encoding='utf-8')
            argv = ['run_online.py', '--config', str(config), '--build-dir', directory,
                    '--assets-dir', directory, '--check']
            for name in ('pak0.pk3', 'pak1.pk3', 'pak2.pk3', 'etloose.pk3'):
                with zipfile.ZipFile(root / name, 'w') as archive:
                    archive.writestr('fixture.txt', 'test')
            with patch('sys.argv', argv), \
                    patch.object(run_online, 'verify_browser', return_value=7), \
                    patch.object(run_online.subprocess, 'Popen') as start, \
                    contextlib.redirect_stdout(io.StringIO()) as output:
                run_online.main()
                self.assertIn('Hosting preflight passed', output.getvalue())
                (root / 'pak0.pk3').write_bytes(b'corrupt')
                with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                    run_online.main()
                start.assert_not_called()

    def test_reject_non_origins(self):
        for value in ['http://play.example.org', 'https://', 'https://user@play.example.org',
                      'https://play.example.org/', 'https://play.example.org/game',
                      'https://play.example.org?query', 'https://play.example.org#fragment',
                      'https://play.example.org?', 'https://play.example.org#', 'https://play.example.org:',
                      'https://pl\u00e4y.example.org',
                      'https://play.example.org:0', 'https://play.example.org:65536',
                      'https://play.example.org:notaport', 'https://play.example.org\\evil',
                      'https://play.example.org\n']:
            with self.subTest(value=value), self.assertRaises(argparse.ArgumentTypeError):
                run_online.https_origin(value)


if __name__ == '__main__':
    unittest.main()
