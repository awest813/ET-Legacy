"""Run the launcher, public discovery and relay using web-server.json defaults."""
import json
import argparse
import os
import secrets
from pathlib import Path
import subprocess
import sys
import time
import zipfile
from urllib.parse import urlsplit
from build_paths import browser_build
from verify_bundle import verify_browser

def https_origin(value):
    """An exact TLS proxy origin, with no credentials, path or query."""
    try:
        parsed = urlsplit(value)
        port = parsed.port
        if (parsed.scheme != 'https' or not parsed.hostname or parsed.username is not None
                or parsed.password is not None or parsed.path or parsed.query or parsed.fragment
                or any(char.isspace() for char in value) or any(char in value for char in '\\?#')
                or value.endswith(':') or not parsed.hostname.isascii()
                or (port is not None and not 1 <= port <= 65535)):
            raise ValueError()
    except ValueError:
        raise argparse.ArgumentTypeError('Use an exact HTTPS origin, for example https://play.example.org')
    # Browser Origin serialization omits the default HTTPS port.
    host = parsed.hostname
    if ':' in host: host = '[' + host + ']'
    return 'https://' + host + (':' + str(port) if port and port != 443 else '')

def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, default=root/'web-server.json',
                        help='Server configuration file (default: web-server.json)')
    parser.add_argument('--public-origin', type=https_origin,
                        help='Exact HTTPS origin of a TLS reverse proxy; services stay on loopback')
    parser.add_argument('--build-dir', type=Path,
                        help='Browser release directory (default: ETWASM_BUILD or build_wasm)')
    parser.add_argument('--check', action='store_true',
                        help='Validate configuration, browser bundle and original packs, then exit without starting services')
    parser.add_argument('--assets-dir', type=Path,
                        help='Original game pack directory (overrides ETWASM_ASSETS)')
    args = parser.parse_args()
    config = args.config.resolve()
    try:
        settings=json.loads(config.read_text(encoding='utf-8'))
        if not isinstance(settings, dict): raise ValueError('Expected a configuration object')
        server=settings['server']
        if not isinstance(server,str) or not server or server.startswith('-'): raise ValueError('Invalid server')
        ports=[settings.get(key,default) for key,default in [('udpPort',27960),('webPort',8081),('relayPort',8082)]]
        if any(type(port) is not int or not 1<=port<=65535 for port in ports) or ports[1]==ports[2]: raise ValueError('Invalid ports')
    except (OSError,ValueError,KeyError,TypeError) as error:
        raise SystemExit('Check '+str(config)+' using misc/web/online-server.example.json: '+str(error))
    udp,web,relay=ports
    build = args.build_dir.resolve() if args.build_dir is not None else browser_build()
    if args.build_dir is not None and not build.is_dir():
        parser.error('--build-dir must name an existing browser release directory')
    try:
        verify_browser(build)
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.error('Browser release failed validation: ' + str(error))
    env=os.environ.copy()
    if args.assets_dir is not None:
        env['ETWASM_ASSETS'] = str(args.assets_dir.resolve())
    if args.check:
        assets = Path(env.get('ETWASM_ASSETS', r'C:\Users\allen\Downloads\etlegacy-wasm\assets'))
        try:
            for name in ('pak0.pk3', 'pak1.pk3', 'pak2.pk3', 'etloose.pk3'):
                with zipfile.ZipFile(assets / name) as archive:
                    if not archive.infolist() or archive.testzip() is not None:
                        raise ValueError('Empty or corrupt pack: ' + name)
        except (OSError, ValueError, zipfile.BadZipFile, RuntimeError, NotImplementedError) as error:
            parser.error('Game packs failed validation: ' + str(error))
        print(f'Hosting preflight passed: {build}; four game packs in {assets.resolve()}')
        print('No services started. TLS, DNS and server compatibility require separate live checks.')
        return
    origins = [args.public_origin] if args.public_origin else [f'http://localhost:{web}', f'http://127.0.0.1:{web}']
    relay_url = 'wss://' + args.public_origin[len('https://'):] + '/relay' if args.public_origin else f'ws://127.0.0.1:{relay}/relay'
    env.update(ETWASM_RELAY_URL=relay_url,ETWASM_BIND='127.0.0.1',
               ETWASM_SERVER_CONFIG=str(config), ETWASM_BUILD=str(build))
    # Shared only by these child services, never written to disk or browser JSON.
    env['ETWASM_PUBLIC_SECRET'] = secrets.token_hex(32)
    flags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0
    children=[]; logs=[]
    try:
        (root/'build_wasm').mkdir(exist_ok=True)
        log_names = (f'online-relay-{relay}.log', f'online-launcher-{web}.log')
        logs=[(root/'build_wasm'/name).open('a',encoding='utf-8') for name in log_names]
        origin_args = [arg for origin in origins for arg in ('--origin', origin)]
        children.append(subprocess.Popen([sys.executable,'-u',str(root/'misc/web/relay.py'),'--server',server,'--udp-port',str(udp),'--port',str(relay),*origin_args],cwd=root,env=env,creationflags=flags,stdout=logs[0],stderr=subprocess.STDOUT))
        children.append(subprocess.Popen([sys.executable,'-u',str(root/'misc/web/serve.py'),str(web)],cwd=root,env=env,creationflags=flags,stdout=logs[1],stderr=subprocess.STDOUT))
        print(f'Online launcher: {args.public_origin or f"http://localhost:{web}"}/ (Ctrl+C stops both services)',flush=True)
        print(f'Browser release: {build}', flush=True)
        if args.public_origin:
            print(f'TLS proxy required: /relay and /relay/* -> 127.0.0.1:{relay}; other paths -> 127.0.0.1:{web}',flush=True)
        print('Service logs: '+', '.join('build_wasm/'+name for name in log_names),flush=True)
        while all(child.poll() is None for child in children): time.sleep(.5)
        raise SystemExit('Launcher or relay stopped. Check the output above.')
    except KeyboardInterrupt: pass
    finally:
        for child in children:
            if child.poll() is None: child.terminate()
        for child in children:
            try: child.wait(timeout=5)
            except subprocess.TimeoutExpired: child.kill(); child.wait()
        for log in logs: log.close()

if __name__=='__main__': main()
