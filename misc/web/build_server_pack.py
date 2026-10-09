"""Build a matching native/WebAssembly Legacy PK3 for pure server verification.

Build the browser and native cgame/ui targets from the same source first.
This packages locally compiled modules and repository assets, never stock PK3s.
"""
import argparse
import json
from pathlib import Path
import subprocess
import zipfile

from custom_assets import inspect_pack, LEGACY_PACK, LEGACY_MODULE
from server_assets import pack_checksum

ROOT = Path(__file__).resolve().parents[2]
WRAPPER = '''#include <stdint.h>
extern intptr_t PREFIX_vmMain(intptr_t, intptr_t, intptr_t, intptr_t, intptr_t,
    intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, intptr_t, intptr_t);
extern void PREFIX_dllEntry(intptr_t (*)(intptr_t, ...));
intptr_t vmMain(intptr_t command, intptr_t a0, intptr_t a1, intptr_t a2,
    intptr_t a3, intptr_t a4, intptr_t a5, intptr_t a6, intptr_t a7,
    intptr_t a8, intptr_t a9, intptr_t a10, intptr_t a11, intptr_t a12,
    intptr_t a13, intptr_t a14, intptr_t a15) {
    return PREFIX_vmMain(command,a0,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11);
}
void dllEntry(intptr_t (*syscalls)(intptr_t, ...)) { PREFIX_dllEntry(syscalls); }
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='emcc')
    parser.add_argument('--node', default='node')
    parser.add_argument('--browser-build', type=Path, default=ROOT / 'build_wasm')
    parser.add_argument('--native-build', type=Path, default=ROOT / 'build_native/server')
    parser.add_argument('--output', type=Path, default=ROOT / 'build_native/fixture/legacy/legacy_v2.86.0-browser.pk3')
    args = parser.parse_args()
    if not LEGACY_PACK.fullmatch(args.output.name):
        parser.error('Use a versioned Legacy PK3 name, such as legacy_v2.86.0-browser.pk3')
    stage = args.output.parent / 'browser-module-build'
    stage.mkdir(parents=True, exist_ok=True)
    modules = []
    for module, prefix in (('cgame', 'cg'), ('ui', 'ui')):
        wrapper = stage / (module + '_entry.c')
        wrapper.write_text(WRAPPER.replace('PREFIX', prefix), encoding='utf-8')
        output = stage / (module + '.mp.wasm32.so')
        subprocess.run([args.compiler, str(wrapper), str(args.browser_build / ('lib' + module + '.a')),
                        str(args.browser_build / 'libbundled_cjson.a'), '-O2', '-sSIDE_MODULE=2',
                        '-sEXPORTED_FUNCTIONS=["_vmMain","_dllEntry"]', '-o', str(output)], check=True)
        subprocess.run([args.node, '-e',
                        'const fs=require("node:fs"),assert=require("node:assert/strict");'
                        'const m=new WebAssembly.Module(fs.readFileSync(process.argv[1]));'
                        'const exports=WebAssembly.Module.exports(m).map(x=>x.name);'
                        'assert(exports.includes("vmMain")&&exports.includes("dllEntry"));', str(output)], check=True)
        modules.append(output)
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
