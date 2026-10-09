"""Forward binary ET datagrams to a configured or signed public UDP server.

Install misc/web/requirements.txt into an isolated environment. This process
is separate from the asset preview; it never runs the game or its bots.
"""
import argparse
import asyncio
import contextlib
import os
import socket
import time
from http import HTTPStatus

from websockets.asyncio.server import serve
from websockets.exceptions import ConnectionClosed
from public_servers import resolve_ticket
from server_browser import probe_resolved_server, resolved_targets

MAX_PACKET = 32768
MAX_QUEUED_BYTES = 1048576
PROTOCOL = "et-udp-v1"


class Budget:
    def __init__(self):
        self.packets = 256.0
        self.bytes = 1048576.0
        self.updated = time.monotonic()

    def allow(self, size):
        now = time.monotonic()
        elapsed, self.updated = now - self.updated, now
        self.packets = min(256, self.packets + elapsed * 256)
        self.bytes = min(1048576, self.bytes + elapsed * 1048576)
        if self.packets < 1 or self.bytes < size:
            return False
        self.packets -= 1
        self.bytes -= size
        return True


class DatagramQueue(asyncio.DatagramProtocol):
    def __init__(self):
        self.queue = asyncio.Queue(maxsize=128)
        self.queued_bytes = 0
        self.failed = asyncio.Event()
        self.failure_reason = 'UDP unavailable or receive queue full'
        self.budget = Budget()

    def datagram_received(self, data, _address):
        if self.failed.is_set():
            return
        if (not 0 < len(data) <= MAX_PACKET or
                self.queued_bytes + len(data) > MAX_QUEUED_BYTES or
                not self.budget.allow(len(data))):
            self.failed.set()
            return
        try:
            self.queue.put_nowait(data)
            self.queued_bytes += len(data)
        except asyncio.QueueFull:
            self.failed.set()

    def error_received(self, _error):
        # Keep the first failure: transport teardown after congestion must not
        # replace the queue diagnosis with an upstream-unavailable message.
        if not self.failed.is_set():
            self.failure_reason = 'UDP server unavailable'
        self.failed.set()

    def connection_lost(self, _error):
        self.error_received(_error)

    async def receive(self):
        data = await self.queue.get()
        self.queued_bytes -= len(data)
        return data


class Relay:
    def __init__(self, target, family=socket.AF_INET, max_clients=8, public_secret=''):
        self.target, self.family = target, family
        self.max_clients = max_clients
        self.active = 0
        self.public_secret = public_secret

    def destination(self, path):
        if path == '/relay':
            return self.target, self.family
        if path.startswith('/relay/'):
            return resolve_ticket(self.public_secret, path[len('/relay/'):])
        raise ValueError('Unknown relay route')

    def request(self, connection, request):
        try:
            self.destination(request.path)
        except ValueError:
            return connection.respond(HTTPStatus.NOT_FOUND, "Relay route not found\n")
        if self.active >= self.max_clients:
            return connection.respond(HTTPStatus.SERVICE_UNAVAILABLE, "Relay is full\n")
        return None

    async def handle(self, websocket):
        if websocket.subprotocol != PROTOCOL:
            await websocket.close(1002, "ET relay protocol required")
            return
        try:
            target, family = self.destination(websocket.request.path)
        except ValueError:
            await websocket.close(1008, 'Server selection expired')
            return
        if self.active >= self.max_clients:
            await websocket.close(1013, "Relay is full")
            return
        self.active += 1
        transport = None
        tasks = []
        try:
            # Connected UDP accepts replies only from this selected upstream. Each
            # browser gets its own source port, preserving ET challenge/qport use.
            transport, incoming = await asyncio.get_running_loop().create_datagram_endpoint(
                DatagramQueue, remote_addr=target, family=family)
            budget = Budget()

            async def upload():
                async for data in websocket:
                    if not isinstance(data, bytes) or not 0 < len(data) <= MAX_PACKET:
                        await websocket.close(1003, "Binary ET packets required")
                        return
                    if not budget.allow(len(data)):
                        await websocket.close(1008, "Packet rate exceeded")
                        return
                    transport.sendto(data)

            async def download():
                while True:
                    await websocket.send(await incoming.receive())

            async def udp_failure():
                await incoming.failed.wait()
                await websocket.close(1013, incoming.failure_reason)

            tasks = [asyncio.create_task(job()) for job in (upload, download, udp_failure)]
            done, _pending = await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
            for task in done:
                task.result()
        except ConnectionClosed:
            pass
        except OSError:
            await websocket.close(1011, "UDP server unavailable")
        finally:
            for task in tasks:
                task.cancel()
            await asyncio.gather(*tasks, return_exceptions=True)
            if transport is not None:
                transport.close()
            self.active -= 1


async def start_relay(target, origins, host="127.0.0.1", port=8082, family=socket.AF_INET, public_secret=''):
    relay = Relay(target, family, public_secret=public_secret)
    server = await serve(relay.handle, host, port, origins=origins,
                         subprotocols=[PROTOCOL], process_request=relay.request,
                         compression=None, max_size=MAX_PACKET, max_queue=32,
                         write_limit=65536, open_timeout=5, close_timeout=2,
                         ping_interval=20, ping_timeout=10)
    return relay, server


async def resolve_configured_target(host, port, timeout=1.5):
    addresses = await asyncio.get_running_loop().getaddrinfo(host, port, type=socket.SOCK_DGRAM)
    targets = resolved_targets(addresses)
    if not targets:
        raise OSError('No supported configured server address')
    family, address = targets[0]
    if len(targets) > 1:
        try:
            _, family, address = await asyncio.to_thread(probe_resolved_server, addresses, timeout)
        except OSError:
            # getinfo may be disabled; retain the operator's original destination.
            pass
    host = address[0]
    if family == socket.AF_INET6 and address[3]:
        host += f"%{address[3]}"
    return (host, address[1]), family


async def main(args):
    target, family = await resolve_configured_target(args.server, args.udp_port)
    relay, server = await start_relay(target, args.origin, args.bind, args.port, family, os.environ.get('ETWASM_PUBLIC_SECRET', ''))
    print(f"ET relay ws://{args.bind}:{args.port}/relay -> {args.server}:{args.udp_port}", flush=True)
    print(f"Allowed page origins: {', '.join(args.origin)}", flush=True)
    async with server:
        await server.serve_forever()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", required=True, help="Fixed ET server hostname or IP")
    parser.add_argument("--udp-port", type=int, default=27960)
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8082)
    parser.add_argument("--origin", action="append", required=True,
                        help="Exact allowed launcher origin; repeat for additional origins")
    options = parser.parse_args()
    if not 1 <= options.udp_port <= 65535 or not 1 <= options.port <= 65535:
        parser.error("Ports must be between 1 and 65535")
    with contextlib.suppress(KeyboardInterrupt):
        asyncio.run(main(options))
