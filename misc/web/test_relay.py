"""Real WebSocket/UDP loopback checks; no public game servers are contacted."""
import asyncio
import contextlib
import socket
import unittest
from unittest import mock

from websockets.asyncio.client import connect
from websockets.exceptions import ConnectionClosed, InvalidStatus
import relay
from public_servers import issue_ticket, resolve_ticket

ORIGIN = "http://localhost:8081"


class Echo(asyncio.DatagramProtocol):
    def __init__(self):
        self.peers = set()
        self.received = []

    def connection_made(self, transport):
        self.transport = transport

    def datagram_received(self, data, address):
        self.peers.add(address)
        self.received.append(data)
        self.transport.sendto(data, address)


class RelayChecks(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.udp, self.echo = await asyncio.get_running_loop().create_datagram_endpoint(
            Echo, local_addr=("127.0.0.1", 0))
        self.relay, self.server = await relay.start_relay(self.udp.get_extra_info('sockname'), [ORIGIN], port=0)
        self.url = f"ws://127.0.0.1:{self.server.sockets[0].getsockname()[1]}/relay"

    async def asyncTearDown(self):
        self.server.close()
        await self.server.wait_closed()
        self.udp.close()

    def client(self, **kwargs):
        return connect(self.url, origin=ORIGIN, subprotocols=[relay.PROTOCOL], proxy=None, **kwargs)

    async def test_binary_datagrams_and_client_isolation(self):
        async with self.client() as one, self.client() as two:
            for data in (b'\xff\xff\xff\xffgetchallenge 123',bytes(range(256)),b'one',b'two'):
                await one.send(data)
                self.assertEqual(await asyncio.wait_for(one.recv(), 2), data)
            await two.send(b'other-client')
            self.assertEqual(await asyncio.wait_for(two.recv(), 2),b'other-client')
            self.assertEqual(len(self.echo.peers),2)
            # A third UDP peer cannot inject a reply into a connected relay socket.
            with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as outsider:
                for peer in self.echo.peers:
                    outsider.sendto(b'forged reply',peer)
            await one.send(b'legitimate reply')
            self.assertEqual(await asyncio.wait_for(one.recv(),2),b'legitimate reply')
        for _ in range(20):
            if self.relay.active == 0:
                break
            await asyncio.sleep(.01)
        self.assertEqual(self.relay.active,0)

    async def test_rejects_cross_origin_and_missing_origin(self):
        for origin in ('http://untrusted.invalid',None):
            with self.subTest(origin=origin), self.assertRaises(InvalidStatus) as error:
                async with connect(self.url,origin=origin,subprotocols=[relay.PROTOCOL],proxy=None):
                    pass
            self.assertEqual(error.exception.response.status_code,403)

    async def test_departure_packets_precede_orderly_close(self):
        packets = [b'departure-one', b'departure-two', b'departure-three']
        async with self.client() as websocket:
            for packet in packets:
                await websocket.send(packet)
            # The browser closes after native departure without awaiting replies.
            await websocket.close()
        for _ in range(40):
            if len(self.echo.received) == len(packets) and self.relay.active == 0:
                break
            await asyncio.sleep(.01)
        self.assertEqual(self.echo.received, packets)
        self.assertEqual(self.relay.active, 0)

    async def test_route_and_protocol_required(self):
        with self.assertRaises(InvalidStatus) as error:
            async with connect(self.url+'/other',origin=ORIGIN,subprotocols=[relay.PROTOCOL],proxy=None):
                pass
        self.assertEqual(error.exception.response.status_code,404)
        with self.assertRaises(InvalidStatus):
            async with connect(self.url,origin=ORIGIN,proxy=None):
                pass

    async def test_invalid_packets_close_connection(self):
        for data, code in (('text',1003),(b'',1003),(b'x'*32769,1009)):
            with self.subTest(size=len(data)):
                async with self.client() as websocket:
                    await websocket.send(data)
                    with self.assertRaises(ConnectionClosed) as error:
                        await asyncio.wait_for(websocket.recv(),2)
                    self.assertEqual(error.exception.rcvd.code,code)

    async def test_client_limit_and_bounded_queue(self):
        self.relay.max_clients=1
        async with self.client():
            with self.assertRaises(InvalidStatus) as error:
                async with self.client():
                    pass
            self.assertEqual(error.exception.response.status_code,503)
        queue=relay.DatagramQueue()
        for _ in range(129):
            queue.datagram_received(b'a',None)
        self.assertEqual(queue.queue.qsize(),128)
        self.assertTrue(queue.failed.is_set())
        budget=relay.Budget()
        budget.packets=0
        self.assertFalse(budget.allow(1))

    async def test_udp_queue_byte_limit_and_release(self):
        queue = relay.DatagramQueue()
        # Isolate queue capacity from the independent packet/rate budget.
        queue.budget.allow = lambda _size: True
        packet = b'x' * relay.MAX_PACKET
        for _ in range(32):
            queue.datagram_received(packet, None)
        self.assertEqual(queue.queued_bytes, 1048576)
        self.assertEqual(await queue.receive(), packet)
        self.assertEqual(queue.queued_bytes, 1048576 - len(packet))
        queue.datagram_received(packet, None)
        self.assertFalse(queue.failed.is_set())
        queue.datagram_received(b'x', None)
        self.assertTrue(queue.failed.is_set())
        self.assertEqual(queue.queued_bytes, 1048576)
        self.assertEqual(queue.queue.qsize(), 32)

    async def test_udp_connection_loss_signals_failure(self):
        queue = relay.DatagramQueue()
        queue.connection_lost(None)
        self.assertTrue(queue.failed.is_set())
        self.assertEqual(queue.failure_reason, 'UDP server unavailable')

    async def test_udp_error_reports_upstream_failure_without_overwriting_queue_failure(self):
        queue = relay.DatagramQueue()
        queue.error_received(OSError('test upstream failure'))
        self.assertTrue(queue.failed.is_set())
        self.assertEqual(queue.failure_reason, 'UDP server unavailable')
        congested = relay.DatagramQueue()
        for _ in range(129):
            congested.datagram_received(b'a', None)
        congested.connection_lost(None)
        self.assertEqual(congested.failure_reason, 'UDP unavailable or receive queue full')

    async def test_closed_udp_transport_closes_websocket_and_releases_slot(self):
        class TrackedQueue(relay.DatagramQueue):
            def connection_made(self, transport):
                self.transport = transport

        queue = TrackedQueue()
        with mock.patch.object(relay, 'DatagramQueue', lambda: queue):
            async with self.client() as websocket:
                await websocket.send(b'check upstream')
                self.assertEqual(await asyncio.wait_for(websocket.recv(), 2), b'check upstream')
                queue.transport.close()
                with self.assertRaises(ConnectionClosed) as error:
                    await asyncio.wait_for(websocket.recv(), 2)
                self.assertEqual(error.exception.rcvd.code, 1013)
                self.assertEqual(error.exception.rcvd.reason, 'UDP server unavailable')
        for _ in range(20):
            if self.relay.active == 0:
                break
            await asyncio.sleep(.01)
        self.assertEqual(self.relay.active, 0)

    async def test_ipv6_upstream_when_available(self):
        try:
            udp,_=await asyncio.get_running_loop().create_datagram_endpoint(
                Echo,local_addr=('::1',0),family=socket.AF_INET6)
        except OSError:
            self.skipTest('IPv6 loopback unavailable')
        server=None
        try:
            target=('::1',udp.get_extra_info('sockname')[1])
            _,server=await relay.start_relay(target,[ORIGIN],port=0,family=socket.AF_INET6)
            url=f"ws://127.0.0.1:{server.sockets[0].getsockname()[1]}/relay"
            async with connect(url,origin=ORIGIN,subprotocols=[relay.PROTOCOL],proxy=None) as websocket:
                await websocket.send(b'IPv6 upstream')
                self.assertEqual(await asyncio.wait_for(websocket.recv(),2),b'IPv6 upstream')
        finally:
            if server is not None:
                server.close();await server.wait_closed()
            udp.close()

    async def test_signed_selection_routes_each_browser_and_rejects_forgery(self):
        secret = 'ab' * 32
        other_udp, other_echo = await asyncio.get_running_loop().create_datagram_endpoint(Echo, local_addr=('127.0.0.1', 0))
        targets = {'8.8.8.8': self.udp.get_extra_info('sockname'), '1.1.1.1': other_udp.get_extra_info('sockname')}
        self.relay.public_secret = secret
        # Validate production signatures/public filtering, then substitute only
        # the already validated public peer for a disposable loopback fixture.
        def resolve(key, token):
            (host, port), family = resolve_ticket(key, token)
            return targets[host], family
        first, second = (issue_ticket(secret, host, 27960) for host in targets)
        try:
            with mock.patch.object(relay, 'resolve_ticket', side_effect=resolve):
                async with connect(self.url+'/'+first, origin=ORIGIN, subprotocols=[relay.PROTOCOL], proxy=None) as one, connect(self.url+'/'+second, origin=ORIGIN, subprotocols=[relay.PROTOCOL], proxy=None) as two:
                    await one.send(b'first selection'); await two.send(b'second selection')
                    self.assertEqual(await asyncio.wait_for(one.recv(), 2), b'first selection')
                    self.assertEqual(await asyncio.wait_for(two.recv(), 2), b'second selection')
                    self.assertEqual(self.echo.received, [b'first selection'])
                    self.assertEqual(other_echo.received, [b'second selection'])
                for suffix in ('/'+first[:-3]+'abc', '/'+first+'?server=127.0.0.1', '/8.8.8.8:27960'):
                    with self.assertRaises(InvalidStatus) as error:
                        async with connect(self.url+suffix, origin=ORIGIN, subprotocols=[relay.PROTOCOL], proxy=None): pass
                    self.assertEqual(error.exception.response.status_code, 404)
        finally:
            other_udp.close()


if __name__ == '__main__':
    unittest.main()
