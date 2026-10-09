"""Bounded, cached getinfo for the operator-configured game server only."""
import re
import secrets
import socket
import threading
import time
import unicodedata
import json
from pathlib import Path

MODULE_MANIFEST = Path(__file__).resolve().parents[2] / 'build_wasm/web-modules/identity.json'


def browser_module_identities():
    """Missing build metadata must fail closed for pure-server discovery."""
    try:
        modules = json.loads(MODULE_MANIFEST.read_text(encoding='utf-8'))['modules']
        result = {}
        for name, key in (('cgame', 'wasmCgame'), ('ui', 'wasmUI')):
            crc, size = modules[name]['crc'], modules[name]['size']
            if type(crc) is not int or not 0 <= crc <= 0xffffffff or type(size) is not int or not 0 < size <= 0xffffffff:
                return {}
            result[key] = f'{crc:08x}:{size}'
        return result
    except (OSError, ValueError, KeyError, TypeError):
        return {}

# Engine and mod versions do not identify the actual package selected by a
# server. Patched servers advertise wasmModules only after checking their PK3
# entries. The client still validates installed, server-allowed packages.


def clean_text(value, limit=160):
    value = re.sub(r'\^[^^]', '', value)
    return ''.join(char for char in value if not unicodedata.category(char).startswith('C')).strip()[:limit]


def parse_info(packet, challenge, latency):
    prefix = b'\xff\xff\xff\xffinfoResponse\n'
    if not packet.startswith(prefix) or len(packet) > 8192:
        raise ValueError('Invalid info packet')
    body = packet[len(prefix):].decode('utf-8', 'replace').rstrip('\x00\r\n')
    parts = body.split('\\')
    if not body.startswith('\\') or len(parts) % 2 != 1:
        raise ValueError('Invalid info fields')
    info = {}
    for key, value in zip(parts[1::2], parts[2::2]):
        if not key or key in info:
            raise ValueError('Ambiguous info fields')
        info[key] = value
    if info.get('challenge') != challenge:
        raise ValueError('Unmatched challenge')

    def number(key, maximum):
        value = info.get(key, '')
        return int(value) if re.fullmatch(r'[0-9]{1,5}', value) and int(value) <= maximum else None

    protocol = number('protocol', 1000)
    if protocol is None:
        raise ValueError('Missing protocol')
    mod = clean_text(info.get('game', ''), 64).lower()
    legacy = protocol == 84 and info.get('gamename', '').lower() == 'et' and info.get('game', '').lower() == 'legacy'
    version = re.match(r'^ET Legacy v?(2)\.([0-9]{1,2})(?:\.([0-9]{1,2}))?(?:[- ]|$)', info.get('version', ''))
    modern = bool(version and tuple(int(part or 0) for part in version.groups()) >= (2, 86, 0))
    identities = browser_module_identities()
    web_modules = (info.get('wasmModules') == '1' and len(identities) == 2 and
                   all(info.get(key) == value for key, value in identities.items()))
    compatible = legacy and modern and (info.get('pure') == '0' or (info.get('pure') == '1' and web_modules))
    reason = '' if compatible else ('This server build has not been verified for this static browser client. Pure servers need WebAssembly modules matching this browser version.' if legacy else 'This browser build needs an ET: Legacy server with protocol 84 and the Legacy mod.')
    return {'status': 'online', 'hostname': clean_text(info.get('hostname', '')),
            'map': clean_text(info.get('mapname', ''), 64),
            'players': number('clients', 128), 'capacity': number('sv_maxclients', 128),
            'humans': number('humans', 128), 'latencyMs': max(0, min(10000, round(latency))),
            'protocol': protocol, 'mod': mod, 'version': clean_text(info.get('version', ''), 96), 'pure': info.get('pure') == '1', 'password': info.get('needpass') == '1',
            'compatible': compatible, 'reason': reason}


def probe_server(host, port, timeout=1.5):
    """Connected UDP accepts replies from the configured peer, with a random echo."""
    family, _, _, _, target = socket.getaddrinfo(host, port, type=socket.SOCK_DGRAM)[0]
    challenge = secrets.token_hex(12)
    with socket.socket(family, socket.SOCK_DGRAM) as udp:
        udp.settimeout(timeout)
        udp.connect(target)
        started = time.monotonic()
        udp.send(b'\xff\xff\xff\xffgetinfo ' + challenge.encode('ascii') + b'\n')
        deadline = started + timeout
        while True:
            udp.settimeout(max(0.001, deadline - time.monotonic()))
            packet = udp.recv(8193)
            try:
                return parse_info(packet, challenge, (time.monotonic() - started) * 1000)
            except ValueError:
                if time.monotonic() >= deadline:
                    raise TimeoutError('No valid info response')


class ServerBrowser:
    def __init__(self, ttl=10, wait=1.8):
        self.ttl, self.wait = ttl, wait
        self.lock = threading.Lock()
        self.cached = None
        self.pending = None

    def get(self, host, port):
        unavailable = {'status': 'unavailable'}
        if not isinstance(host, str) or not host.strip() or len(host) > 253 or type(port) is not int or not 1 <= port <= 65535:
            return unavailable
        key = (host.strip(), port)
        with self.lock:
            if self.cached and self.cached[0] == key and time.monotonic() - self.cached[1] < self.ttl:
                return dict(self.cached[2])
            if self.pending:
                if self.pending[0] != key:
                    return unavailable
                completed = self.pending[1]
            else:
                completed = threading.Event()
                self.pending = (key, completed)

                def work():
                    try:
                        result = probe_server(*key)
                    except (OSError, ValueError):
                        result = unavailable
                    with self.lock:
                        self.cached = (key, time.monotonic(), result)
                        self.pending = None
                    completed.set()

                # A slow DNS lookup cannot hold an HTTP response indefinitely.
                # At most one probe runs, and simultaneous refreshes share it.
                threading.Thread(target=work, daemon=True).start()
        if not completed.wait(self.wait):
            return unavailable
        with self.lock:
            return dict(self.cached[2]) if self.cached and self.cached[0] == key else unavailable
