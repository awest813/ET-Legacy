"""Bounded, cached getinfo for the operator-configured game server only."""
import re
import secrets
import socket
import threading
import time
import unicodedata

# A native engine version alone does not establish browser compatibility. These
# published mod archives have been inspected for both wasm32 module entries.
# Keep this conservative until additional packages are verified; native pure
# validation still checks the actual required package during connection.
WEB_MODULE_BUILDS = ('2.86.0-34-g50cffc8',)


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
    version = re.match(r'^ET Legacy v(2)\.([0-9]{1,2})\.([0-9]{1,2})(?:[- ]|$)', info.get('version', ''))
    modern = bool(version and tuple(map(int, version.groups())) >= (2, 86, 0))
    web_modules = any(re.match(r'^ET Legacy v' + re.escape(build) + r'(?: |$)', info.get('version', '')) for build in WEB_MODULE_BUILDS)
    compatible = legacy and (web_modules or (modern and info.get('pure') == '0'))
    reason = '' if compatible else ('This server build has not been verified for this static browser client. Pure servers need published WebAssembly module packages.' if legacy else 'This browser build needs an ET: Legacy server with protocol 84 and the Legacy mod.')
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
