"""Public ET master discovery, following the native client's wire format.

The HTTP catalog must run discovery in a bounded worker, as DNS itself can block.
Only public numeric destinations are returned; clients cannot supply a master.
Game-server compatibility still requires a challenged getinfo response.
"""
import ipaddress
import base64
import hashlib
import hmac
import json
import queue
import re
import socket
import threading
import time
from server_browser import probe_server

MASTER_HOST = 'master.etlegacy.com'
MASTER_PORT = 27950
MAX_SERVERS = 4096  # Match the native client's global server capacity.
MAX_PACKET_SERVERS = 1024
MAX_PACKET = 8192


def parse_master_packet(packet):
    regular = b'\xff\xff\xff\xffgetserversResponse'
    extended = b'\xff\xff\xff\xffgetserversExtResponse'
    if len(packet) > MAX_PACKET:
        raise ValueError('Master packet too large')
    if packet.startswith(extended):
        offset, ipv6 = len(extended), True
    elif packet.startswith(regular):
        offset, ipv6 = len(regular), False
    else:
        raise ValueError('Invalid master response')
    if offset == len(packet):
        raise ValueError('Missing master entries or terminator')
    addresses, seen, records = [], set(), 0
    while offset < len(packet):
        if packet[offset:] in (b'\\EOT', b'\\EOT\x00', b'\\EOT\\', b'/EOT', b'/EOT\x00'):
            break
        delimiter = packet[offset]
        # Large native lists span UDP packets; a complete non-final packet may
        # end with the separator after its last address instead of EOT.
        if records and offset + 1 == len(packet) and delimiter in (ord('\\'), ord('/')):
            break
        size = 4 if delimiter == ord('\\') else 16 if ipv6 and delimiter == ord('/') else 0
        if not size or offset + size + 3 > len(packet):
            raise ValueError('Invalid master address')
        end = offset + size + 3
        if end < len(packet) and packet[end] not in (ord('\\'), ord('/')):
            raise ValueError('Invalid master address boundary')
        address = ipaddress.ip_address(packet[offset + 1:offset + 1 + size])
        port = int.from_bytes(packet[offset + 1 + size:end], 'big')
        key = (str(address), port)
        records += 1
        # Reject loopback, private, link-local, multicast, reserved and mapped
        # private addresses before any server probe or relay can use this list.
        if port and address.is_global and not address.is_multicast and key not in seen:
            seen.add(key)
            addresses.append(key)
            if len(addresses) == MAX_PACKET_SERVERS:
                break
        offset = end
    return addresses


def query_master(host=MASTER_HOST, port=MASTER_PORT, timeout=2.0):
    """Try resolved master addresses within one shared UDP query deadline."""
    addresses = socket.getaddrinfo(host, port, type=socket.SOCK_DGRAM)
    groups = {}
    for family, _, _, _, target in addresses:
        if family not in (socket.AF_INET, socket.AF_INET6):
            continue
        group = groups.setdefault(family, [])
        if target not in group and len(group) < 4:
            group.append(target)
    # Alternate families so several unreachable IPv6 addresses cannot delay IPv4.
    targets = [(family, group[index]) for index in range(4)
               for family, group in groups.items() if index < len(group)]
    deadline = time.monotonic() + timeout
    for index, (family, target) in enumerate(targets):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            break
        try:
            return _query_master_target(family, target, remaining / (len(targets) - index))
        except OSError:
            # A silent or unreachable address must leave time for its alternatives.
            continue
    raise TimeoutError('The public server master did not return a valid list')


def _query_master_target(family, target, timeout):
    """Connected UDP rejects replies from other peers for each resolved target."""
    # Match CL_GlobalServers_f: IPv4 uses getservers; IPv6 uses the extended query.
    request = b'getserversExt et 84 full empty' if family == socket.AF_INET6 else b'getservers 84 full empty'
    with socket.socket(family, socket.SOCK_DGRAM) as udp:
        udp.connect(target)
        deadline = time.monotonic() + timeout
        udp.send(b'\xff\xff\xff\xff' + request)
        result, seen, valid = [], set(), False
        for _ in range(64):
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            udp.settimeout(min(remaining, .2) if valid else remaining)
            try:
                packet = udp.recv(MAX_PACKET + 1)
            except TimeoutError:
                break
            try:
                entries = parse_master_packet(packet)
            except ValueError:
                continue
            valid = True
            for entry in entries:
                if entry not in seen:
                    seen.add(entry)
                    result.append(entry)
                    if len(result) == MAX_SERVERS:
                        return result
        if not valid:
            raise TimeoutError('The public server master did not return a valid list')
        return result


def server_id(host, port):
    return hashlib.sha256(f'{host}:{port}'.encode('ascii')).hexdigest()[:32]


def public_target(host, port):
    address = ipaddress.ip_address(host)
    if not address.is_global or address.is_multicast or type(port) is not int or not 1 <= port <= 65535:
        raise ValueError('Invalid public target')
    return str(address), port


def issue_ticket(secret, host, port, now=None):
    """Only the launcher can authorize a previously probed public destination."""
    host, port = public_target(host, port)
    if not secret:
        raise ValueError('Public relay is not configured')
    payload = json.dumps([host, port, int(time.time() if now is None else now) + 14400], separators=(',', ':')).encode('ascii')
    signed = payload + hmac.digest(secret.encode('ascii'), payload, 'sha256')
    return base64.urlsafe_b64encode(signed).decode('ascii').rstrip('=')


def resolve_ticket(secret, token, now=None):
    if not secret or not isinstance(token, str) or not re.fullmatch(r'[A-Za-z0-9_-]{60,220}', token):
        raise ValueError('Invalid server ticket')
    try:
        signed = base64.urlsafe_b64decode(token + '=' * (-len(token) % 4))
        payload, signature = signed[:-32], signed[-32:]
        if not hmac.compare_digest(signature, hmac.digest(secret.encode('ascii'), payload, 'sha256')):
            raise ValueError('Invalid server ticket')
        host, port, expires = json.loads(payload)
        current = time.time() if now is None else now
        if type(expires) is not int or not current < expires <= current + 14400:
            raise ValueError('Expired server ticket')
        host, port = public_target(host, port)
    except (ValueError, TypeError, UnicodeError) as error:
        raise ValueError('Invalid server ticket') from error
    return (host, port), socket.AF_INET6 if ':' in host else socket.AF_INET


class PublicCatalog:
    """One shared discovery job, eight bounded UDP workers, immediate HTTP reads.

    Numeric master entries avoid per-server DNS. A hung master DNS lookup keeps
    this single job pending; repeated HTTP reads cannot spawn more jobs.
    """
    def __init__(self, ttl=60, max_age=180, scan_seconds=90):
        self.ttl, self.max_age, self.scan_seconds = ttl, max_age, scan_seconds
        self.lock = threading.Lock()
        self.pending = False
        self.started = 0
        self.finished = None
        self.entries = {}
        self.total = self.checked = 0
        self.error = False

    def get(self):
        with self.lock:
            if not self.pending and (self.finished is None or time.monotonic() - self.finished >= self.ttl):
                self.pending = True
                self.started = time.monotonic()
                self.error = False
                self.checked = 0
                threading.Thread(target=self._scan, daemon=True).start()
            now = time.monotonic()
            entries = [dict(value[1]) for value in self.entries.values() if now - value[0] < self.max_age]
            entries.sort(key=lambda entry: (-(entry['info'].get('humans') or 0), entry['info']['latencyMs'], entry['id']))
            stalled = self.pending and now - self.started > self.scan_seconds + 3
            return {'version': 1, 'status': 'unavailable' if self.error or stalled else 'checking' if self.pending else 'ready',
                    'total': self.total, 'checked': self.checked, 'servers': entries}

    def select(self, identity):
        with self.lock:
            value = self.entries.get(identity)
            return dict(value[1]) if value and time.monotonic() - value[0] < self.max_age else None

    def _scan(self):
        try:
            addresses = query_master()
            jobs = queue.Queue()
            for address in addresses:
                jobs.put(address)
            deadline = time.monotonic() + self.scan_seconds
            with self.lock:
                self.total = len(addresses)
                # A removed master entry must never remain selectable.
                identities = {server_id(*address) for address in addresses}
                self.entries = {key: value for key, value in self.entries.items() if key in identities}

            def probe():
                while time.monotonic() < deadline:
                    try:
                        host, port = jobs.get_nowait()
                    except queue.Empty:
                        return
                    try:
                        info = probe_server(host, port, timeout=1.0)
                    except (OSError, ValueError):
                        info = {'compatible': False}
                    identity = server_id(host, port)
                    with self.lock:
                        self.checked += 1
                        if info.get('compatible') is True:
                            self.entries[identity] = (time.monotonic(), {'id': identity, 'host': host, 'port': port, 'info': info})
                        else:
                            self.entries.pop(identity, None)
            workers = [threading.Thread(target=probe, daemon=True) for _ in range(8)]
            for worker in workers:
                worker.start()
            for worker in workers:
                worker.join()
        except (OSError, ValueError):
            with self.lock:
                self.error = True
        finally:
            with self.lock:
                self.finished = time.monotonic()
                self.pending = False
