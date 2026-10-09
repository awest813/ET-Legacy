"""Check the generated browser bundle and exact pure-server publication files."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import zlib

from build_server_pack import published_modules

CORE = {'etl.html', 'etl.js', 'etl.wasm', 'etl.data', 'manifest.webmanifest',
        'icon-192.png', 'icon-512.png'}


def verify(build):
    entries = re.findall(r"\{url:'([^']+)',sha256:'([0-9a-f]{64})'\}",
                         (build / 'sw.js').read_text(encoding='utf-8'))
    if len(entries) != len(CORE) or {name for name, _ in entries} != CORE:
        raise ValueError('Worker must describe each of the seven browser files exactly once')
    for name, expected in entries:
        if hashlib.sha256((build / name).read_bytes()).hexdigest() != expected:
            raise ValueError('Browser bundle changed: ' + name)
    manifest = json.loads((build / 'web-modules/identity.json').read_text(encoding='utf-8'))
    for path in published_modules(build):
        name = path.name.split('.')[0]
        metadata = manifest['modules'][name]
        data = path.read_bytes()
        if (type(metadata['crc']) is not int or type(metadata['size']) is not int or
                metadata['crc'] != zlib.crc32(data) or metadata['size'] != len(data)):
            raise ValueError('Published module identity changed: ' + name)
    return len(entries)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=Path, nargs='?', default=Path('build_wasm'))
    args = parser.parse_args()
    print(f'Verified {verify(args.build)} browser files and both published module identities/archives.')
