"""Wire-format, destination filtering and real UDP tests for public discovery."""
import ipaddress
import socket
import threading
import time
import unittest
from unittest.mock import patch
from public_servers import MAX_PACKET_SERVERS, MAX_SERVERS, parse_master_packet, query_master, PublicCatalog, server_id, issue_ticket, resolve_ticket

REGULAR = b'\xff\xff\xff\xffgetserversResponse'
EXTENDED = b'\xff\xff\xff\xffgetserversExtResponse'


def record(host, port=27960):
    address = ipaddress.ip_address(host)
    return (b'\\' if address.version == 4 else b'/') + address.packed + port.to_bytes(2, 'big')


class DiscoveryTests(unittest.TestCase):
    def test_ipv4_and_deduplication(self):
        packet = REGULAR + record('8.8.8.8') + record('8.8.8.8') + record('1.1.1.1', 27961) + b'\\EOT'
        self.assertEqual(parse_master_packet(packet), [('8.8.8.8', 27960), ('1.1.1.1', 27961)])

    def test_extended_mixed_families(self):
        packet = EXTENDED + record('2606:4700:4700::1111') + record('8.8.8.8') + b'\\EOT'
        self.assertEqual(parse_master_packet(packet), [('2606:4700:4700::1111', 27960), ('8.8.8.8', 27960)])

    def test_non_public_destinations_and_zero_port(self):
        blocked = ['127.0.0.1', '10.1.2.3', '192.168.1.1', '169.254.1.1', '100.64.0.1',
                   '224.0.0.1', '255.255.255.255', '192.0.2.1', '::1', 'fe80::1',
                   'fd00::1', 'ff02::1', '::ffff:127.0.0.1']
        packet = EXTENDED + b''.join(record(host) for host in blocked) + record('8.8.8.8', 0) + b'\\EOT'
        self.assertEqual(parse_master_packet(packet), [])

    def test_invalid_and_truncated_packets(self):
        for packet in (b'wrong', REGULAR, REGULAR + b'\\\x08', REGULAR + record('8.8.8.8')[:-1],
                       REGULAR + record('8.8.8.8') + b'!', REGULAR + record('::1') + b'\\EOT',
                       REGULAR + b'x' * 8192):
            with self.subTest(packet=packet[:30]), self.assertRaises(ValueError):
                parse_master_packet(packet)

    def test_empty_list_and_terminators(self):
        for marker in (b'\\EOT', b'\\EOT\x00', b'\\EOT\\', b'/EOT'):
            self.assertEqual(parse_master_packet(REGULAR + marker), [])

    def test_result_bound(self):
        entries = b''.join(record('8.8.8.8', 10000 + i) for i in range(MAX_PACKET_SERVERS + 1))
        self.assertEqual(len(parse_master_packet(REGULAR + entries + b'\\EOT')), MAX_PACKET_SERVERS)

    def test_global_native_capacity_across_packets(self):
        packets = [REGULAR + b''.join(record('8.8.8.8', 10000 + i) for i in range(start, min(start + 100, MAX_SERVERS + 1)))
                   for start in range(0, MAX_SERVERS + 1, 100)]
        class FakeUDP:
            def __enter__(self): return self
            def __exit__(self, *args): pass
            def connect(self, address): pass
            def send(self, packet): pass
            def settimeout(self, timeout): pass
            def recv(self, size): return packets.pop(0)
        with patch('public_servers.socket.getaddrinfo', return_value=[(socket.AF_INET, socket.SOCK_DGRAM, 0, '', ('1.1.1.1', 27950))]), patch('public_servers.socket.socket', return_value=FakeUDP()):
            result = query_master(timeout=2)
        self.assertEqual(len(result), MAX_SERVERS)
        self.assertEqual(result[-1], ('8.8.8.8', 10000 + MAX_SERVERS - 1))

    def test_non_final_packet_separator(self):
        self.assertEqual(parse_master_packet(REGULAR + record('8.8.8.8') + b'\\'), [('8.8.8.8', 27960)])
        # The live ET master also ends chunks directly after the last port.
        self.assertEqual(parse_master_packet(REGULAR + record('8.8.8.8')), [('8.8.8.8', 27960)])

    def test_connected_udp_rejects_other_peer_and_merges_replies(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as master, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as stranger:
            master.bind(('127.0.0.1', 0))
            master.settimeout(2)
            def respond():
                request, client = master.recvfrom(1024)
                self.assertEqual(request, b'\xff\xff\xff\xffgetservers 84 full empty')
                stranger.sendto(REGULAR + record('9.9.9.9') + b'\\EOT', client)
                master.sendto(b'bad packet', client)
                master.sendto(REGULAR + record('8.8.8.8'), client)
                master.sendto(REGULAR + record('8.8.8.8') + record('1.1.1.1') + b'\\EOT', client)
            thread = threading.Thread(target=respond)
            thread.start()
            try:
                result = query_master('127.0.0.1', master.getsockname()[1], timeout=1)
            finally:
                thread.join(2)
            self.assertEqual(result, [('8.8.8.8', 27960), ('1.1.1.1', 27960)])


class CatalogTests(unittest.TestCase):
    def test_signed_public_tickets_expiry_tampering_and_private_destinations(self):
        secret = 'ab' * 32
        for host in ('8.8.8.8', '2606:4700:4700::1111'):
            token = issue_ticket(secret, host, 27960, now=100)
            target, family = resolve_ticket(secret, token, now=101)
            self.assertEqual(target, (host, 27960))
            self.assertEqual(family, socket.AF_INET6 if ':' in host else socket.AF_INET)
            for key, value, now in (('wrong', token, 101), (secret, token[:-3] + 'abc', 101),
                                    (secret, token, 14500), (secret, token, 99), ('', token, 101),
                                    (secret, token + '?target=127.0.0.1', 101)):
                with self.assertRaises(ValueError): resolve_ticket(key, value, now=now)
        for host in ('127.0.0.1', '::1', '10.0.0.1', '169.254.1.1', '224.0.0.1'):
            with self.assertRaises(ValueError): issue_ticket(secret, host, 27960)
        with self.assertRaises(ValueError): issue_ticket(secret, '8.8.8.8', 0)

    def test_shared_worker_partial_list_and_selection_expiry(self):
        started, release = threading.Event(), threading.Event()
        catalog = PublicCatalog(ttl=60, max_age=180)
        def master():
            started.set(); release.wait(2)
            return [('8.8.8.8', 27960), ('1.1.1.1', 27961)]
        def probe(host, port, **kwargs):
            return {'status': 'online', 'compatible': host == '8.8.8.8', 'hostname': 'Fixture', 'map': 'oasis', 'latencyMs': 10, 'humans': 0}
        with patch('public_servers.query_master', side_effect=master) as query, patch('public_servers.probe_server', side_effect=probe):
            self.assertEqual(catalog.get()['status'], 'checking')
            self.assertTrue(started.wait(1))
            for _ in range(20): self.assertEqual(catalog.get()['status'], 'checking')
            self.assertEqual(query.call_count, 1, 'A blocked master cannot spawn new workers')
            catalog.started = time.monotonic() - 100
            self.assertEqual(catalog.get()['status'], 'unavailable', 'A hung DNS worker cannot leave the browser checking forever')
            self.assertTrue(catalog.pending)
            self.assertEqual(query.call_count, 1)
            release.set()
            deadline = time.monotonic() + 2
            while catalog.pending and time.monotonic() < deadline: time.sleep(.005)
            value = catalog.get()
        self.assertEqual((value['status'], value['total'], value['checked']), ('ready', 2, 2))
        self.assertEqual(len(value['servers']), 1)
        identity = server_id('8.8.8.8', 27960)
        self.assertEqual(catalog.select(identity)['host'], '8.8.8.8')
        self.assertIsNone(catalog.select(server_id('1.1.1.1', 27961)))
        catalog.entries[identity] = (time.monotonic() - 181, catalog.entries[identity][1])
        self.assertIsNone(catalog.select(identity), 'Expired destinations cannot be authorized')

    def test_refresh_removes_missing_and_incompatible_entries(self):
        catalog = PublicCatalog()
        one, two = server_id('8.8.8.8', 27960), server_id('1.1.1.1', 27960)
        catalog.entries = {one: (time.monotonic(), {'id': one}), two: (time.monotonic(), {'id': two})}
        with patch('public_servers.query_master', return_value=[('1.1.1.1', 27960)]), patch('public_servers.probe_server', return_value={'compatible': False}):
            catalog._scan()
        self.assertEqual(catalog.entries, {})
        with patch('public_servers.query_master', side_effect=TimeoutError()): catalog._scan()
        self.assertEqual(catalog.get()['status'], 'unavailable')


if __name__ == '__main__':
    unittest.main()
