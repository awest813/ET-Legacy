"""Validate optional PK3 content without extracting or executing it."""
import hashlib
import os
from pathlib import Path
import re
import struct
import threading
import zipfile
import zlib

MAX_PACK = 256 * 1024 * 1024
MAX_EXPANDED = 1024 * 1024 * 1024
PACK_NAME = re.compile(r"[a-zA-Z0-9_][a-zA-Z0-9_.-]{0,119}\.pk3\Z")
MAP_NAME = re.compile(r"[a-zA-Z0-9_][a-zA-Z0-9_-]{0,63}\Z")
LEGACY_PACK = re.compile(r"legacy_v?\d[a-zA-Z0-9_.-]*\.pk3\Z")
LEGACY_MODULE = re.compile(r"(?:(?:cgame|ui)\.mp\.(?:x86_64|i386|aarch64|wasm32)\.so|(?:cgame|ui)_mp_(?:x64|x86)\.dll|lib(?:cgame|ui)\.mp\.android\.(?:arm64-v8a|i386|x86_64)\.so)\Z")
inventory_lock = threading.Lock()


def inspect_pack(path, game="etmain", progress=None):
    path = Path(path)
    if not PACK_NAME.fullmatch(path.name) or path.name.lower() in {"pak0.pk3", "pak1.pk3", "pak2.pk3", "etloose.pk3"}:
        raise ValueError("Choose a custom PK3; bundled packs cannot be replaced")
    if not 22 <= path.stat().st_size <= MAX_PACK:
        raise ValueError("PK3 files must be at most 256 MB")
    maps, names, expanded, ignored_modules = [], set(), 0, []
    legacy = game == 'legacy' and bool(LEGACY_PACK.fullmatch(path.name))
    version_header = None
    with zipfile.ZipFile(path) as archive:
        entries = archive.infolist()
        if not entries or len(entries) > 65536:
            raise ValueError("Invalid pack entry count")
        for entry in entries:
            if progress:
                progress()
            name = entry.filename.lower()
            parts = name.rstrip("/").split("/")
            if (not name or "\\" in name or ":" in name or name.startswith("/") or
                    any(part in ("", ".", "..") for part in parts) or
                    any(ord(c) < 32 for c in name) or (name in names and not (legacy and name == 'ui/version_generated.h'))):
                raise ValueError("Unsafe or duplicate pack paths")
            names.add(name)
            if (entry.flag_bits & 1 or (entry.external_attr >> 16) & 0o170000 == 0o120000 or
                    entry.compress_type not in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED)):
                raise ValueError("Encrypted, linked or unsupported ZIP entries")
            if name.endswith((".dll", ".so", ".dylib", ".exe", ".qvm", ".wasm", ".js")) or name.startswith("vm/"):
                if legacy and LEGACY_MODULE.fullmatch(name):
                    # Preserve the complete PK3 for sv_pure. Browser modules are
                    # linked statically; these entries are never extracted/loaded.
                    ignored_modules.append(name)
                else:
                    raise ValueError("Only game assets are supported, not executable mod modules")
            if name in ("autoexec.cfg", "etconfig.cfg", "browser-connect.cfg") or (name == 'default.cfg' and not legacy):
                raise ValueError("Packs cannot replace startup or player settings")
            expanded += entry.file_size
            if expanded > MAX_EXPANDED or entry.file_size > MAX_PACK:
                raise ValueError("Expanded pack is too large")
            # Streaming reads validate every entry CRC without allocating its contents.
            with archive.open(entry) as stream:
                if legacy and name == 'ui/version_generated.h':
                    if entry.file_size > 65536:
                        raise ValueError('Legacy version header is too large')
                    header_bytes = stream.read()
                    if version_header is not None and header_bytes != version_header:
                        raise ValueError('Conflicting Legacy version headers')
                    version_header = header_bytes
                if name.startswith("maps/") and name.endswith(".bsp"):
                    map_name = name[5:-4]
                    header = stream.read(144)
                    if (not MAP_NAME.fullmatch(map_name) or len(header) != 144 or
                            header[:4] != b"IBSP" or struct.unpack_from("<i", header, 4)[0] != 47):
                        raise ValueError("Unsupported Enemy Territory map BSP")
                    for offset, length in struct.iter_unpack("<ii", header[8:]):
                        if offset < 0 or length < 0 or offset + length > entry.file_size:
                            raise ValueError("Map has invalid BSP lump bounds")
                    maps.append(map_name)
                while stream.read(1024 * 1024):
                    if progress:
                        progress()
    digest, crc = hashlib.sha256(), 0
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            if progress:
                progress()
            digest.update(block)
            crc = zlib.crc32(block, crc)
    return {"name": path.name, "game": game, "size": path.stat().st_size,
            "sha256": digest.hexdigest(), "crc32": f"{crc:08x}", "maps": sorted(maps),
            "ignoredModules": ignored_modules}


def fingerprint(path):
    stat = path.stat()
    return (str(path.resolve()), stat.st_dev, stat.st_ino, stat.st_size,
            stat.st_mtime_ns, stat.st_ctime_ns)


def catalog(root, cache=None, checksum=None):
    root = Path(root).resolve()
    packs, errors, present = [], [], set()
    for game in ("etmain", "legacy"):
        directory = root / game
        if not directory.is_dir():
            continue
        for path in sorted(directory.glob("*.pk3")):
            key = (game, str(path))
            present.add(key)
            stamp = None
            try:
                resolved = path.resolve()
                if not resolved.is_relative_to(root):
                    raise ValueError("Pack is outside the custom asset directory")
                stamp = fingerprint(path)
                saved = cache.get(key) if cache is not None else None
                if saved and saved[0] == stamp:
                    if saved[1] is None:
                        raise ValueError(saved[2])
                    entry = saved[1]
                else:
                    entry = inspect_pack(path, game)
                    if checksum:
                        entry['checksum'] = checksum(path)
                    if fingerprint(path) != stamp:
                        raise ValueError('Pack changed during validation; refresh the map list')
                    if cache is not None:
                        cache[key] = (stamp, entry)
                packs.append(dict(entry))
                if len(packs) > 64:
                    raise ValueError("At most 64 custom packs are supported")
            except (OSError, ValueError, zipfile.BadZipFile, RuntimeError, NotImplementedError, zlib.error) as error:
                errors.append({"name": path.name, "reason": str(error)[:200]})
                if cache is not None and stamp is not None and len(packs) <= 64:
                    cache[key] = (stamp, None, str(error)[:200])
    if cache is not None:
        for key in list(cache):
            if key not in present:
                del cache[key]
    return {"version": 1, "packs": packs[:64], "errors": errors}
