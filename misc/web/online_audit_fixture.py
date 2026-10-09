"""Loopback browser/relay/UDP recovery fixture; deliberately does not host a match.

Run with .venv-web/Scripts/python.exe misc/web/online_audit_fixture.py and open
http://localhost:8083/. Join Online, then Retry twice. The three UDP connections
exercise protocol mismatch, game-name mismatch, and an accepted handshake followed
by a deliberate rejection before gamestate. All listeners bind to loopback.
"""
import asyncio
import argparse
import contextlib
import functools
import http.server
import os
import threading

import relay
import serve


class AuditEndpoint(asyncio.DatagramProtocol):
    def __init__(self, reject_delay=1):
        self.peers = {}
        self.timers = []
        self.reject_delay = reject_delay

    def connection_made(self, transport):
        self.transport = transport

    def datagram_received(self, data, address):
        if data.startswith(b'\xff\xff\xff\xffgetchallenge'):
            if address not in self.peers:
                self.peers[address] = {'attempt': len(self.peers) + 1, 'rejected': False}
            state = self.peers[address]
            command = 'getchallenge'
            reply = b'\xff\xff\xff\xffchallengeResponse 1234 0\n'
        elif address not in self.peers:
            return
        else:
            state = self.peers[address]
            if data.startswith(b'\xff\xff\xff\xffgetinfo'):
                command = 'getinfo'
                protocol = 83 if state['attempt'] == 1 else 84
                game = 'audit-other-game' if state['attempt'] == 2 else 'et'
                reply = (f'\xff\xff\xff\xffinfoResponse\n\\protocol\\{protocol}'
                         f'\\gamename\\{game}\\version\\ET Legacy v2.86\n').encode('latin1')
            elif data.startswith(b'\xff\xff\xff\xffconnect'):
                command = 'connect'
                reply = b'\xff\xff\xff\xffconnectResponse\n'
            else:
                command = 'netchan'
                reply = None
                if not state['rejected']:
                    state['rejected'] = True
                    rejection = (b'\xff\xff\xff\xffprint\n[err_dialog] Handshake audit passed. '
                                 b'This local endpoint deliberately stops before gamestate; it does not host a match.\n')
                    self.timers.append(asyncio.get_running_loop().call_later(
                        self.reject_delay, self.transport.sendto, rejection, address))
        print(f"Attempt {state['attempt']}: {command}, {len(data)} bytes", flush=True)
        if reply:
            self.transport.sendto(reply, address)

    def connection_lost(self, _error):
        for timer in self.timers:
            timer.cancel()


async def main(reject_delay=1):
    os.environ['ETWASM_RELAY_URL'] = 'ws://127.0.0.1:8084/relay'
    os.environ['ETWASM_SERVER_LABEL'] = 'Local online recovery audit'
    preview = http.server.ThreadingHTTPServer(
        ('127.0.0.1', 8083), functools.partial(serve.Handler, directory=serve.BUILD))
    thread = threading.Thread(target=preview.serve_forever, daemon=True)
    thread.start()
    udp = server = None
    try:
        udp, _ = await asyncio.get_running_loop().create_datagram_endpoint(
            lambda: AuditEndpoint(reject_delay), local_addr=('127.0.0.1', 27961))
        _, server = await relay.start_relay(('127.0.0.1', 27961), ['http://localhost:8083'], port=8084)
        print('Online recovery fixture: http://localhost:8083/ (no gameplay)', flush=True)
        async with server:
            await server.serve_forever()
    finally:
        if server is not None:
            server.close()
            await server.wait_closed()
        if udp is not None:
            udp.close()
        preview.shutdown()
        preview.server_close()
        thread.join()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reject-delay', type=float, default=1,
                        help='Seconds before deliberate rejection; use 15 to audit native Disconnect')
    args = parser.parse_args()
    if not 0 < args.reject_delay <= 60:
        parser.error('Rejection delay must be above zero and at most 60 seconds')
    with contextlib.suppress(KeyboardInterrupt):
        asyncio.run(main(args.reject_delay))
