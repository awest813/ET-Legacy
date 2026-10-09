#!/usr/bin/env python3
"""Dev server for the ET: Legacy WebAssembly build.

Routes:
  /etl.html, /etl.js, /etl.wasm  -> build output
  /assets/<file>.pk3             -> downloaded original game paks
  /                              -> shell page
"""
import http.server
import os
import sys
import functools
import hashlib
import io
import json
import threading
import zipfile
import zlib
import tempfile
import time
from urllib.parse import unquote, urlsplit, parse_qs
from custom_assets import catalog, inspect_pack, MAX_PACK, PACK_NAME, inventory_lock
from server_assets import install as install_server_pack, pack_checksum, DownloadCancelled, public_error
from server_browser import ServerBrowser
from public_servers import PublicCatalog, issue_ticket

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BUILD = os.path.join(REPO, "build_wasm")
ASSETS = os.environ.get("ETWASM_ASSETS", r"C:\Users\allen\Downloads\etlegacy-wasm\assets")
BUILD_FILES = {"etl.html", "etl.js", "etl.wasm", "etl.data", "etl.js.map", "etl.wasm.map"}
BUILD_FILES.update({"sw.js", "manifest.webmanifest", "icon-192.png", "icon-512.png"})
ASSET_FILES = {"etloose.pk3", "pak0.pk3", "pak1.pk3", "pak2.pk3"}
CUSTOM_ASSETS = os.environ.get("ETWASM_CUSTOM_ASSETS", os.path.join(REPO, "web-assets"))
_custom_lock = threading.Lock()
_custom_cache = {}
_manifest_lock = threading.Lock()
_manifest_cache = {}
_server_browser = ServerBrowser()
_public_catalog = PublicCatalog()


def online_settings():
    try:
        with open(os.path.join(REPO, 'web-server.json'), encoding='utf-8') as stream:
            value = json.load(stream)
        if not isinstance(value, dict):
            return {}
        for key in ('relay', 'serverLabel', 'downloadBase', 'server'):
            if key in value and not isinstance(value[key], str):
                return {}
        if 'assetSources' in value and (not isinstance(value['assetSources'], dict) or
                not all(isinstance(key, str) and isinstance(url, str) for key, url in value['assetSources'].items())):
            return {}
        return value
    except (OSError, ValueError):
        return {}


def custom_catalog():
    with _custom_lock:
        value = catalog(CUSTOM_ASSETS, cache=_custom_cache, checksum=pack_checksum)
        for error in value['errors']:
            print('Custom pack skipped:', error['name'], error['reason'], flush=True)
        return value


def asset_manifest():
    """Version packs by their complete bytes, after checking every ZIP entry."""
    with _manifest_lock:
        paths = [(name, os.path.realpath(os.path.join(ASSETS, name))) for name in sorted(ASSET_FILES)]
        for _, path in paths:
            if os.path.commonpath([os.path.realpath(ASSETS), path]) != os.path.realpath(ASSETS):
                raise ValueError("Asset outside pack directory")
        key = tuple((path, os.stat(path).st_size, os.stat(path).st_mtime_ns) for _, path in paths)
        if _manifest_cache.get("key") == key:
            return _manifest_cache["data"]
        packs = {}
        for name, path in paths:
            with zipfile.ZipFile(path) as archive:
                if archive.testzip() is not None:
                    raise ValueError("Pack CRC failure")
            digest, crc = hashlib.sha256(), 0
            with open(path, "rb") as stream:
                for block in iter(lambda: stream.read(1024 * 1024), b""):
                    digest.update(block)
                    crc = zlib.crc32(block, crc)
            packs[name] = {"size": os.stat(path).st_size, "crc32": f"{crc:08x}", "sha256": digest.hexdigest()}
        if key != tuple((path, os.stat(path).st_size, os.stat(path).st_mtime_ns) for _, path in paths):
            raise ValueError("Packs changed during validation")
        data = json.dumps({"version": 1, "packs": packs}, separators=(",", ":")).encode("utf-8")
        _manifest_cache.update(key=key, data=data)
        return data

MIME = {
    ".wasm": "application/wasm",
    ".js": "text/javascript",
    ".html": "text/html",
    ".pk3": "application/octet-stream",
    ".webmanifest": "application/manifest+json",
    ".png": "image/png",
}


def network_config(identity=None):
    """Select a freshly verified master entry; never accept a client UDP address."""
    settings = online_settings()
    relay = os.environ.get("ETWASM_RELAY_URL", settings.get('relay', ''))
    try:
        parsed = urlsplit(relay)
        valid = (parsed.scheme in ("ws", "wss") and parsed.hostname and parsed.path == "/relay" and
                 (parsed.port is None or 1 <= parsed.port <= 65535) and
                 not parsed.username and not parsed.password and not parsed.query and not parsed.fragment)
    except ValueError:
        valid = False
    secret = os.environ.get('ETWASM_PUBLIC_SECRET', '')
    public = bool(valid and len(secret) == 64 and all(char in '0123456789abcdef' for char in secret))
    selected = _public_catalog.select(identity) if public and identity else None
    if public and identity and selected is None:
        return {'version': 1, 'enabled': False, 'publicServers': True, 'reason': 'Refresh the public list and choose an available server.'}
    server_info = selected['info'] if selected else _server_browser.get(settings.get('server', ''), settings.get('udpPort', 27960)) if valid else None
    if selected:
        relay += '/' + issue_ticket(secret, selected['host'], selected['port'])
    return {"version": 1, "enabled": bool(valid), "relay": relay if valid else "",
            "serverLabel": (server_info['hostname'] or 'Public Legacy server') if selected else os.environ.get("ETWASM_SERVER_LABEL", settings.get('serverLabel', 'ET: Legacy server')).strip()[:160] or "ET: Legacy server",
            "publicServers": public, "serverId": identity if selected else '',
            "autoAssets": os.environ.get('ETWASM_AUTO_ASSETS', str(settings.get('autoAssets', False))).lower() in ('1','true'),
            "engineAddress": "192.0.2.1:27960", "serverInfo": server_info}


class Handler(http.server.SimpleHTTPRequestHandler):
    def do_POST(self):
        # Import is local only and requires the exact page origin plus a custom
        # header. Cross-origin forms and arbitrary remote clients cannot write.
        origin = self.headers.get('Origin', '')
        parsed = urlsplit(origin)
        try:
            allowed = (parsed.scheme == 'http' and parsed.hostname in ('localhost', '127.0.0.1', '::1') and
                       parsed.port == self.server.server_port and not parsed.path and
                       self.client_address[0] in ('127.0.0.1', '::1') and
                       self.server.server_address[0] in ('127.0.0.1', '::1') and
                       self.headers.get('X-ETL-Import') == '1')
        except ValueError:
            allowed = False
        if not allowed:
            self.send_error(403, 'Import is available only from the local launcher')
            return
        path = unquote(urlsplit(self.path).path)
        name = path.removeprefix('/assets/import/')
        if not path.startswith('/assets/import/') or not PACK_NAME.fullmatch(name):
            self.send_error(400, 'Choose a custom PK3 file')
            return
        try:
            length = int(self.headers.get('Content-Length', '0'))
        except ValueError:
            length = 0
        if not 22 <= length <= MAX_PACK or self.headers.get('Transfer-Encoding'):
            self.send_error(413, 'PK3 files must be at most 256 MB')
            return
        temporary = None
        try:
            directory = os.path.join(CUSTOM_ASSETS, 'etmain')
            os.makedirs(directory, exist_ok=True)
            root = os.path.realpath(CUSTOM_ASSETS)
            if os.path.commonpath([root, os.path.realpath(directory)]) != root:
                raise ValueError('Map directory is outside the custom asset directory')
            # A private staging directory retains the original filename for validation.
            with tempfile.TemporaryDirectory(dir=directory) as stage:
                temporary = os.path.join(stage, name)
                self.connection.settimeout(30)
                with open(temporary, 'wb') as stream:
                    remaining = length
                    while remaining:
                        chunk = self.rfile.read(min(remaining, 1024 * 1024))
                        if not chunk:
                            raise ValueError('Incomplete upload')
                        stream.write(chunk)
                        remaining -= len(chunk)
                entry = inspect_pack(temporary)
                if not entry['maps']:
                    raise ValueError('Choose a map pack containing maps/*.bsp')
                destination = os.path.join(directory, name)
                with inventory_lock:
                    if sum(len([name for name in os.listdir(folder) if name.endswith('.pk3')]) for folder in
                           (os.path.join(CUSTOM_ASSETS, game) for game in ('etmain', 'legacy')) if os.path.isdir(folder)) >= 64:
                        raise ValueError('At most 64 imported map packs are supported')
                    if os.path.lexists(destination):
                        raise FileExistsError('A pack with that name already exists; rename the new pack')
                    # Link publishes without replacing a concurrently installed pack.
                    os.link(temporary, destination)
            self.wfile.write(self.send_json({'installed': entry}).getvalue())
        except FileExistsError as error:
            self.send_error(409, str(error))
        except (OSError, ValueError, zipfile.BadZipFile, RuntimeError, NotImplementedError, zlib.error) as error:
            self.send_error(400, str(error)[:200])

    def send_json(self, value):
        data = json.dumps(value, separators=(',', ':')).encode('utf-8')
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        return io.BytesIO(data)

    def stream_server_pack(self, parts, checksum, base, source):
        # One request owns one transfer. Closing it interrupts progress writes,
        # releases the installer lock and removes the private staging directory.
        self.send_response(200)
        self.send_header('Content-Type', 'application/x-ndjson')
        self.send_header('X-Accel-Buffering', 'no')
        self.send_header('Connection', 'close')
        self.end_headers()
        self.close_connection = True
        self.connection.settimeout(10)
        last_stage, last_write = None, 0
        def emit(value):
            nonlocal last_stage, last_write
            now = time.monotonic()
            stage = value.get('stage')
            complete = value.get('total', 0) > 0 and value.get('loaded') == value.get('total')
            if stage and stage == last_stage and now - last_write < .25 and not complete:
                return
            try:
                self.wfile.write((json.dumps(value, separators=(',', ':')) + '\n').encode('utf-8'))
                self.wfile.flush()
                last_stage, last_write = stage, now
            except OSError as error:
                raise DownloadCancelled() from error
        try:
            entry = install_server_pack(CUSTOM_ASSETS, parts[0], parts[1], checksum, base, source, progress=emit)
            entry['checksum'] = checksum
            emit({'installed': entry})
        except DownloadCancelled:
            pass
        except (OSError, ValueError, zipfile.BadZipFile, RuntimeError, zlib.error) as error:
            print('Server asset unavailable:', str(error), flush=True)
            code, message = public_error(error)
            try:
                emit({'error': code, 'message': message})
            except DownloadCancelled:
                pass
        return None

    def route_path(self, path):
        path = unquote(urlsplit(path).path)
        if path in ("", "/"):
            root, name = BUILD, "etl.html"
        elif path[1:] in BUILD_FILES and path.startswith("/"):
            root, name = BUILD, path[1:]
        elif path.startswith("/assets/") and path[len("/assets/"):] in ASSET_FILES:
            root, name = ASSETS, path[len("/assets/"):]
        else:
            return None
        root = os.path.realpath(root)
        target = os.path.realpath(os.path.join(root, name))
        # Resolve symlinks too; only engine output and the named packs are served.
        try:
            return target if os.path.commonpath([root, target]) == root else None
        except ValueError:
            return None

    def translate_path(self, path):
        return self.route_path(path)

    def send_head(self):
        path = unquote(urlsplit(self.path).path)
        if path == '/network/recovery':
            # This independent page stays reachable if a saved launcher cannot run.
            with open(os.path.join(REPO, 'misc', 'web', 'pwa', 'recovery.html'), 'rb') as recovery:
                data = recovery.read()
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            return io.BytesIO(data)
        if path.startswith('/network/assets/'):
            config = network_config()
            if self.command != 'GET' or not config['enabled'] or not config['autoAssets'] or self.headers.get('X-ETL-Assets') != '1' or self.headers.get('Sec-Fetch-Site') != 'same-origin':
                self.send_error(403, 'Automatic server assets are unavailable')
                return None
            parts = path.removeprefix('/network/assets/').split('/')
            try:
                query = parse_qs(urlsplit(self.path).query, strict_parsing=True)
                if len(parts) != 2 or set(query) != {'checksum'} or len(query['checksum']) != 1:
                    raise ValueError('Invalid required pack request')
                checksum = int(query['checksum'][0])
                settings = online_settings()
                base = os.environ.get('ETWASM_DOWNLOAD_BASE', settings.get('downloadBase', ''))
                source = settings.get('assetSources', {}).get('/'.join(parts), '')
                if self.headers.get('Accept') == 'application/x-ndjson':
                    return self.stream_server_pack(parts, checksum, base, source)
                entry = install_server_pack(CUSTOM_ASSETS, parts[0], parts[1], checksum, base, source)
                entry['checksum'] = checksum
                return self.send_json({'installed':entry})
            except (OSError, ValueError, zipfile.BadZipFile, RuntimeError, zlib.error) as error:
                print('Server asset unavailable:', str(error), flush=True)
                self.send_error(502, 'Required server pack unavailable or checksum mismatch')
                return None
        if path == '/assets/custom.json' or path.startswith('/assets/custom/'):
            try:
                value = custom_catalog()
                if path == '/assets/custom.json':
                    return self.send_json(value)
                for entry in value['packs']:
                    expected = '/assets/custom/' + entry['game'] + '/' + entry['sha256'] + '/' + entry['name']
                    if path == expected:
                        stream = open(os.path.join(CUSTOM_ASSETS, entry['game'], entry['name']), 'rb')
                        self.send_pack_headers(entry)
                        return stream
                self.send_error(404, 'Custom pack not found; refresh the map list')
            except (OSError, ValueError):
                self.send_error(503, 'Custom packs are unavailable')
            return None
        if path == '/network/servers.json':
            secret = os.environ.get('ETWASM_PUBLIC_SECRET', '')
            enabled = len(secret) == 64 and all(char in '0123456789abcdef' for char in secret)
            value = _public_catalog.get() if enabled else {'version': 1, 'status': 'disabled', 'servers': [], 'total': 0, 'checked': 0}
            return self.send_json(value)
        if path == '/network/config.json':
            query = parse_qs(urlsplit(self.path).query)
            selections = query.get('server')
            identity = '' if selections is None else selections[0] if len(selections) == 1 else 'invalid'
            return self.send_json(network_config(identity))
        if unquote(urlsplit(self.path).path) == "/assets/manifest.json":
            try:
                data = asset_manifest()
            except (OSError, ValueError, zipfile.BadZipFile, RuntimeError):
                self.send_error(503, "Game packs unavailable or invalid")
                return None
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            return io.BytesIO(data)
        if self.route_path(self.path) is None:
            self.send_error(404, "File not found")
            return None
        return super().send_head()

    def send_pack_headers(self, entry):
        self.send_response(200)
        self.send_header('Content-Type', 'application/octet-stream')
        self.send_header('Content-Length', str(entry['size']))
        self.end_headers()
        return True

    def list_directory(self, path):
        self.send_error(404, "File not found")
        return None

    def guess_type(self, path):
        ext = os.path.splitext(path)[1].lower()
        return MIME.get(ext, "application/octet-stream")

    def end_headers(self):
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        super().end_headers()


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    os.makedirs(BUILD, exist_ok=True)
    handler = functools.partial(Handler, directory=BUILD)
    server = http.server.ThreadingHTTPServer((os.environ.get("ETWASM_BIND", "127.0.0.1"), port), handler)
    print(f"Serving ET:Legacy WASM build on http://localhost:{port}/")
    print(f"  build output : {BUILD}")
    print(f"  game paks    : {ASSETS}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
