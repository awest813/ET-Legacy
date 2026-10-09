"""Select one browser release for both static files and server compatibility."""
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def browser_build():
    return Path(os.environ.get('ETWASM_BUILD', ROOT / 'build_wasm')).resolve()
