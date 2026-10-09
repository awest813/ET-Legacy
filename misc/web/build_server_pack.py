"""Build a matching native/WebAssembly Legacy PK3 for pure server verification.

Build the browser and native cgame/ui targets from the same source first.
This packages locally compiled modules and repository assets, never stock PK3s.
"""
import argparse
import json
from pathlib import Path
import zipfile

from custom_assets import inspect_pack, LEGACY_PACK, LEGACY_MODULE
from server_assets import pack_checksum
from build_web_modules import digest

ROOT = Path(__file__).resolve().parents[2]



def published_modules(browser_build):
    # Package exactly the modules whose identities were linked into etl.
    # Rebuilding them independently can silently publish a different build.
    published = browser_build / 'web-modules'
    manifest = json.loads((published / 'identity.json').read_text(encoding='utf-8'))
    for name, archive in (('cgame', 'libcgame.a'), ('ui', 'libui.a'), ('cjson', 'libbundled_cjson.a')):
        if digest(browser_build / archive) != manifest['archives'][name]:
            raise ValueError('Browser module archives changed; rebuild the etl target first')
    modules = [published / (name + '.mp.wasm32.so') for name in ('cgame', 'ui')]
    for name, output in zip(('cgame', 'ui'), modules):
        if digest(output) != manifest['modules'][name]['sha256']:
            raise ValueError('Published browser module changed; rebuild the etl target first')
    return modules


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='emcc', help=argparse.SUPPRESS)  # Older invocations accepted.
    parser.add_argument('--node', default='node', help=argparse.SUPPRESS)
    parser.add_argument('--browser-build', type=Path, default=ROOT / 'build_wasm')
    parser.add_argument('--native-build', type=Path, default=ROOT / 'build_native/server')
    parser.add_argument('--output', type=Path, default=ROOT / 'build_native/fixture/legacy/legacy_v2.86.0-browser.pk3')
    args = parser.parse_args()
    if not LEGACY_PACK.fullmatch(args.output.name):
        parser.error('Use a versioned Legacy PK3 name, such as legacy_v2.86.0-browser.pk3')
    stage = args.output.parent / 'browser-module-build'
    stage.mkdir(parents=True, exist_ok=True)
    modules = published_modules(args.browser_build)
    # Native fixture modules must use the same checksum containers as wasm.
    native = [path for path in (args.native_build / 'legacy').iterdir()
              if path.is_file() and LEGACY_MODULE.fullmatch(path.name) and not path.name.endswith('.wasm32.so')]
    if not any('cgame' in path.name for path in native) or not any('ui' in path.name for path in native):
        raise ValueError('Build matching native cgame and UI modules first')
    modules += sorted(native)
    version = args.native_build / 'etmain/ui/version_generated.h'
    temporary = stage / args.output.name
    with zipfile.ZipFile(temporary, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for path in sorted((ROOT / 'etmain').rglob('*')):
            if path.is_file() and not path.is_symlink() and path.relative_to(ROOT / 'etmain').as_posix() != 'ui/version_generated.h':
                archive.write(path, path.relative_to(ROOT / 'etmain').as_posix())
        archive.write(version, 'ui/version_generated.h')
        for path in modules:
            archive.write(path, path.name)
    result = inspect_pack(temporary, 'legacy')
    result['checksum'] = pack_checksum(temporary)
    temporary.replace(args.output)
    args.output.with_suffix('.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
