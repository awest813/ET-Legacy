"""Run the launcher, public discovery and relay using web-server.json defaults."""
import json
import os
import secrets
from pathlib import Path
import subprocess
import sys
import time

def main():
    root = Path(__file__).resolve().parents[2]
    try:
        settings=json.loads((root/'web-server.json').read_text(encoding='utf-8'))
        server=settings['server']
        if not isinstance(server,str) or not server or server.startswith('-'): raise ValueError('Invalid server')
        ports=[settings.get(key,default) for key,default in [('udpPort',27960),('webPort',8081),('relayPort',8082)]]
        if any(type(port) is not int or not 1<=port<=65535 for port in ports) or ports[1]==ports[2]: raise ValueError('Invalid ports')
    except (OSError,ValueError,KeyError,TypeError) as error:
        raise SystemExit('Create web-server.json using misc/web/online-server.example.json: '+str(error))
    udp,web,relay=ports
    env=os.environ.copy()
    env.update(ETWASM_RELAY_URL=f'ws://127.0.0.1:{relay}/relay',ETWASM_BIND='127.0.0.1')
    # Shared only by these child services, never written to disk or browser JSON.
    env['ETWASM_PUBLIC_SECRET'] = secrets.token_hex(32)
    flags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0
    children=[]; logs=[]
    try:
        (root/'build_wasm').mkdir(exist_ok=True)
        logs=[(root/'build_wasm'/name).open('a',encoding='utf-8') for name in ('online-relay.log','online-launcher.log')]
        children.append(subprocess.Popen([sys.executable,'-u',str(root/'misc/web/relay.py'),'--server',server,'--udp-port',str(udp),'--port',str(relay),'--origin',f'http://localhost:{web}','--origin',f'http://127.0.0.1:{web}'],cwd=root,env=env,creationflags=flags,stdout=logs[0],stderr=subprocess.STDOUT))
        children.append(subprocess.Popen([sys.executable,'-u',str(root/'misc/web/serve.py'),str(web)],cwd=root,env=env,creationflags=flags,stdout=logs[1],stderr=subprocess.STDOUT))
        print(f'Online launcher: http://localhost:{web}/ (Ctrl+C stops both services)',flush=True)
        print('Service logs: build_wasm/online-launcher.log and online-relay.log',flush=True)
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
