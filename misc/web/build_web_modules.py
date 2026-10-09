"""Publish module identities for the cgame/UI archives linked into the browser.

The side modules are not executed by the static browser build. Their ZIP CRC
and length identify the exact module entries that may satisfy its pure report.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import zlib

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


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--node', required=True)
    parser.add_argument('--cgame', type=Path, required=True)
    parser.add_argument('--ui', type=Path, required=True)
    parser.add_argument('--cjson', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    entries = {}
    for module, prefix in (('cgame', 'cg'), ('ui', 'ui')):
        wrapper = args.output / (module + '_entry.c')
        wrapper.write_text(WRAPPER.replace('PREFIX', prefix), encoding='utf-8')
        output = args.output / (module + '.mp.wasm32.so')
        subprocess.run([args.compiler, str(wrapper), str(getattr(args, module)),
                        str(args.cjson), '-O2', '-sSIDE_MODULE=2',
                        '-sEXPORTED_FUNCTIONS=["_vmMain","_dllEntry"]', '-o', str(output)], check=True)
        subprocess.run([args.node, '-e',
                        'const fs=require("node:fs"),assert=require("node:assert/strict");'
                        'const m=new WebAssembly.Module(fs.readFileSync(process.argv[1]));'
                        'const exports=WebAssembly.Module.exports(m).map(x=>x.name);'
                        'assert(exports.includes("vmMain")&&exports.includes("dllEntry"));', str(output)], check=True)
        data = output.read_bytes()
        entries[module] = {'crc': zlib.crc32(data), 'size': len(data), 'sha256': digest(output)}
    source = '/* Generated from the statically linked browser module archives. */\n'
    for field, symbol in (('crc', 'CRC'), ('size', 'Size')):
        values = ', '.join(str(entries[module][field]) + 'UL' for module in ('cgame', 'ui'))
        source += f'const unsigned long etlWebModule{symbol}[2] = {{ {values} }};\n'
    (args.output / 'identity.c').write_text(source, encoding='utf-8')
    manifest = {'modules': entries, 'archives': {name: digest(getattr(args, name))
                for name in ('cgame', 'ui', 'cjson')}}
    (args.output / 'identity.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
