"""Audit installed PK3 bytes, source freshness, and the six offline map bundles."""
import collections
import json
from pathlib import Path, PurePosixPath
import zipfile
import zlib

import serve


def audit():
    root = Path(serve.REPO)
    names = set()
    report = {"packs": [], "maps": [], "loose_source_differences": []}
    for name in sorted(serve.ASSET_FILES):
        path = Path(serve.ASSETS) / name
        with zipfile.ZipFile(path) as archive:
            entries = archive.infolist()
            normalized = [entry.filename.lower().replace("\\", "/") for entry in entries]
            names.update(normalized)
            unsafe = [name for name in normalized if PurePosixPath(name).is_absolute()
                      or ".." in PurePosixPath(name).parts or ":" in name]
            report["packs"].append({"name": path.name, "bytes": path.stat().st_size,
                "entries": len(entries), "unpacked_bytes": sum(entry.file_size for entry in entries),
                "crc_failure": archive.testzip(), "duplicate_names": sum(
                    count - 1 for count in collections.Counter(normalized).values() if count > 1),
                "unsafe_paths": unsafe})
            if name == "etloose.pk3":
                packed = {entry.filename.lower(): entry for entry in entries}
                for source in (root / "etmain").rglob("*"):
                    if not source.is_file():
                        continue
                    relative = source.relative_to(root / "etmain").as_posix().lower()
                    entry = packed.get(relative)
                    if not entry or entry.CRC != zlib.crc32(source.read_bytes()):
                        report["loose_source_differences"].append(relative)
    bot_root = root / "vendor/omni-bot-browser"
    for name in ("oasis", "goldrush", "battery", "fueldump", "radar", "railgun"):
        report["maps"].append({"map": name, "bsp": f"maps/{name}.bsp" in names,
            "navigation": (bot_root / f"et/nav/{name}.way").is_file(),
            "script": (bot_root / f"et/nav/{name}.gm").is_file()})
    bot_files = [path for path in bot_root.rglob("*") if path.is_file()]
    report["bot_file_count"] = len(bot_files)
    report["native_bot_binaries"] = [str(path.relative_to(bot_root)) for path in bot_files
        if path.suffix.lower() in {".dll", ".exe", ".so", ".dylib"}]
    report["font_files"] = sorted(name for name in names if name.endswith(".ttf"))
    report["crosshair_assets"] = sorted(name for name in names if "gfx/2d/crosshair" in name)
    report["custom_assets"] = serve.custom_catalog()
    for pack in report['custom_assets']['packs']:
        pack['bot_navigation'] = {name: (bot_root / f'et/nav/{name}.way').is_file() and
            (bot_root / f'et/nav/{name}.gm').is_file() for name in pack['maps']}
    return report


if __name__ == "__main__":
    report = audit()
    output = Path(serve.BUILD) / "asset-integrity-audit.json"
    output.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"Audited {sum(pack['entries'] for pack in report['packs'])} entries; report: {output}")
    failed = any(pack["crc_failure"] or pack["duplicate_names"] or pack["unsafe_paths"]
                 for pack in report["packs"])
    failed |= any(not all(item[key] for key in ("bsp", "navigation", "script")) for item in report["maps"])
    failed |= bool(report["native_bot_binaries"] or report["loose_source_differences"])
    failed |= bool(report["custom_assets"]["errors"])
    raise SystemExit(1 if failed else 0)
