# Build and host the browser app

Run commands from the repository root. The online launcher needs a persistent
Python process with outbound UDP access and a TLS reverse proxy; a static-only
host cannot run discovery, server-pack recovery or the WebSocket-to-UDP relay.
Original game PK3s are supplied separately from the browser CI artifacts.

## Select a coherent release

Use a successful **Browser port** workflow for the intended commit. Download
`browser-bundle-<commit>` into a new release directory. Keep it intact, including
`sw.js`, all seven browser files and `web-modules/identity.json`. The identity
file is needed by server discovery, even when the compiler archives are absent.

To build from source, activate Emscripten 6.0.10 and install Python, CMake and
Ninja, then run:

```sh
./build_wasm.sh configure
./build_wasm.sh etl -j2
python misc/web/verify_bundle.py build_wasm
```

The separate `browser-module-publication-<commit>` artifact is required when
building a matching pure-server pack. Combine it with the browser bundle from
**the same commit** and run `verify_bundle.py` on that directory. Its archive
and module hashes must match the identity manifest. Compiler archives are not
required just to host the browser bundle.

## Validate before starting

Create a Python virtual environment, install the service dependency and copy
`misc/web/online-server.example.json` to a private configuration file. Set its
configured game server and distinct web/relay ports.

```sh
python -m venv .venv-web
.venv-web/bin/python -m pip install -r misc/web/requirements.txt
.venv-web/bin/python misc/web/run_online.py --config web-server.json \
  --build-dir /srv/et/releases/COMMIT --assets-dir /srv/et/assets --check
```

On Windows use `.venv-web/Scripts/python.exe`. The pack directory must contain
`pak0.pk3`, `pak1.pk3`, `pak2.pk3` and `etloose.pk3`. `--check` verifies the
browser file sizes and SHA-256 hashes against the worker, checks module identity
metadata, and reads every ZIP entry to validate all four packs. It exits without
opening ports or replacing running services. This detects missing/corrupt files;
it does not establish asset provenance, TLS availability or game compatibility.
Normal startup also validates the browser bundle before launching either child.
`--assets-dir` overrides `ETWASM_ASSETS` for both preflight and the running web
service. Set `ETWASM_CUSTOM_ASSETS` to a persistent custom/server-pack directory.

## Run behind HTTPS

Use a real DNS name pointing at the hosting machine, and keep Caddy certificate
storage and custom packs persistent. Set the same exact origin in both processes:

```sh
export ETWASM_SITE=https://play.example.org
.venv-web/bin/python misc/web/run_online.py --config web-server.json \
  --build-dir /srv/et/releases/COMMIT --assets-dir /srv/et/assets \
  --public-origin "$ETWASM_SITE"
# In a separate terminal with the same ETWASM_SITE:
caddy validate --config misc/web/Caddyfile.example --adapter caddyfile
caddy run --config misc/web/Caddyfile.example --adapter caddyfile
```

Use a process supervisor for the runner and Caddy. Both Python services remain
on loopback. If their ports differ from 8081/8082, set `ETWASM_WEB_UPSTREAM` and
`ETWASM_RELAY_UPSTREAM` to the corresponding loopback addresses. Preserve full
`/relay` and `/relay/<ticket>` paths and browser Origin. Caddy handles WebSocket
upgrades; the supplied configuration also blocks hosted device imports.
See [Caddy reverse proxy documentation](https://caddyserver.com/docs/caddyfile/directives/reverse_proxy).

## Update, verify and roll back

Stage each release in a new directory and preflight it before changing the
runner's `--build-dir`. Keep the previous complete release for rollback.
Do not copy a new worker or engine file into a directory being served: clients
could receive a mixture of releases. Switching backends requires a planned
service restart and interrupts online connections. The generated worker offers
a coherent app update to existing saved clients.

After deployment, verify the actual HTTPS origin: Wasm MIME, COOP/COEP headers,
stock-pack downloads, saved offline startup, both configured and signed-public
relay routes, two-client replication and reconnect. Test with a server matching
this browser's module identities; arbitrary public Legacy servers may be
incompatible. Repeat checks after rollback. Preflight success alone is not a
public hosting or Internet gameplay test.
