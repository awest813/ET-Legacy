"""Fetch exact server packs from an operator-approved HTTPS source."""
import html
from html.parser import HTMLParser
import os
from pathlib import Path
import re
import struct
import tempfile
import threading
import time
import urllib.request
import urllib.error
from urllib.parse import urljoin, urlsplit
import zipfile

from custom_assets import inspect_pack, MAX_PACK, PACK_NAME, inventory_lock

_lock = threading.Lock()


class DownloadCancelled(Exception):
    """The browser stopped receiving the pack's progress stream."""


def public_error(error):
    """Actionable messages without exposing operator URLs or filesystem paths."""
    if isinstance(error, TimeoutError) or isinstance(getattr(error, 'reason', None), TimeoutError):
        return 'timeout', 'The pack source took too long. Retry the connection.'
    if isinstance(error, urllib.error.HTTPError):
        return 'source', 'The approved pack source is unavailable. Retry later or ask the server operator for this pack.'
    message = str(error).lower()
    if 'checksum' in message:
        return 'checksum', 'The source has a different version of this pack. Ask the server operator to update its download source.'
    if 'catalog' in message:
        return 'source', 'This pack is missing from the approved sources. Ask the server operator to add its download source.'
    if any(reason in message for reason in ('at most 64', 'size limit', 'too large', 'at most 256 mb')):
        return 'limit', 'The pack exceeds the launcher limits. Ask the server operator to review installed packs.'
    if isinstance(error, (ValueError, zipfile.BadZipFile, RuntimeError)):
        return 'invalid', 'This pack could not be verified. Ask the server operator for a valid copy.'
    return 'source', 'The approved pack source could not be reached. Retry the connection or try again later.'


def md4(data):
    """RFC 1320; the engine hashes ZIP entry CRCs, not the complete ZIP bytes."""
    length = len(data) * 8
    data += b'\x80' + b'\0' * ((55 - len(data)) % 64) + struct.pack('<Q', length)
    state = [0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476]
    orders = [list(range(16)), [i + j for j in range(4) for i in (0,4,8,12)],
              [0,8,4,12,2,10,6,14,1,9,5,13,3,11,7,15]]
    shifts = [(3,7,11,19), (3,5,9,13), (3,9,11,15)]
    for offset in range(0,len(data),64):
        words = struct.unpack_from('<16I',data,offset)
        a,b,c,d = state
        for phase in range(3):
            for n,k in enumerate(orders[phase]):
                f = ((b & c) | (~b & d)) if phase == 0 else ((b & c) | (b & d) | (c & d)) if phase == 1 else b ^ c ^ d
                value = (a + f + words[k] + (0,0x5a827999,0x6ed9eba1)[phase]) & 0xffffffff
                shift = shifts[phase][n % 4]
                value = ((value << shift) | (value >> (32-shift))) & 0xffffffff
                a,b,c,d = d,value,b,c
        state = [(old + value) & 0xffffffff for old,value in zip(state,(a,b,c,d))]
    return struct.pack('<4I',*state)


def pack_checksum(path):
    with zipfile.ZipFile(path) as archive:
        data = b''.join(struct.pack('<I',entry.CRC) for entry in archive.infolist() if entry.file_size > 0)
    digest = struct.unpack('<4I',md4(data))
    return struct.unpack('<i',struct.pack('<I',digest[0]^digest[1]^digest[2]^digest[3]))[0]


class HTTPSOnly(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, message, headers, new_url):
        parsed = urlsplit(new_url)
        original = urlsplit(request.full_url)
        if parsed.scheme != 'https' or (parsed.hostname, parsed.port or 443) != (original.hostname, original.port or 443) or parsed.username or parsed.password:
            raise ValueError('Pack download redirected outside its configured HTTPS host')
        return super().redirect_request(request,fp,code,message,headers,new_url)


def download(url, path, limit=MAX_PACK, progress=None):
    parsed = urlsplit(url)
    if parsed.scheme != 'https' or not parsed.hostname or parsed.username or parsed.password or parsed.fragment:
        raise ValueError('Pack source must use HTTPS without credentials')
    opener = urllib.request.build_opener(HTTPSOnly())
    with opener.open(url,timeout=20) as response, Path(path).open('wb') as stream:
        if response.status != 200:
            raise ValueError('Pack source unavailable')
        total = 0
        deadline = time.monotonic() + 180
        expected = int(response.headers.get('Content-Length', '0'))
        if expected < 0 or expected > limit:
            raise ValueError('Pack source exceeds its size limit')
        if progress:
            progress(stage='downloading', loaded=0, total=expected)
        while block := response.read(1048576):
            total += len(block)
            if time.monotonic() > deadline:
                raise TimeoutError('Pack source download timed out')
            if total > limit:
                raise ValueError('Pack source exceeds its size limit')
            stream.write(block)
            if progress:
                progress(stage='downloading', loaded=total, total=expected)
        if expected and total != expected:
            raise ValueError('Incomplete pack download')


class ReleaseArchiveParser(HTMLParser):
    """Read the official stable page's mod-only archive without running its JS."""
    def __init__(self):
        super().__init__()
        self.current = None
        self.links = []

    def handle_starttag(self, tag, attrs):
        if tag == 'a':
            value = dict(attrs)
            self.current = [value.get('data-href', value.get('href', '')), []]

    def handle_data(self, data):
        if self.current is not None:
            self.current[1].append(data)

    def handle_endtag(self, tag):
        if tag == 'a' and self.current is not None:
            url, label = self.current
            if ' '.join(''.join(label).split()) == 'All supported archive':
                parsed = urlsplit(urljoin('https://www.etlegacy.com', url))
                if (parsed.scheme == 'https' and parsed.netloc == 'www.etlegacy.com' and
                        re.fullmatch(r'/download/file/[0-9]+', parsed.path) and not parsed.query and not parsed.fragment):
                    self.links.append(parsed.geturl())
            self.current = None


def official_source(game, name, stage, progress=None):
    if game == 'etmain':
        page = stage / 'source.html'
        download('https://www.etlegacy.com/packages/' + name[:-4], page, 1048576, progress)
        source = page.read_text(encoding='utf-8')
        links = re.findall(r'href=[\"\']([^\"\']*/packages/download/\d+)[\"\']',source)
        if not links:
            raise ValueError('Map is absent from the configured official package catalog')
        return urljoin('https://www.etlegacy.com',html.unescape(links[0])), False
    version = name.removeprefix('legacy_').removesuffix('.pk3')
    # Released versions leave the rolling snapshot catalog. Find their mod-only
    # archive on the fixed official release page, then extract/check the exact PK3.
    stable = re.fullmatch(r'v(2)\.([0-9]{2})\.([0-9]{1,2})', version)
    if stable:
        page = stage / 'release.html'
        download('https://www.etlegacy.com/download/release/' + ''.join(stable.groups()), page, 2097152, progress)
        parser = ReleaseArchiveParser()
        parser.feed(page.read_text(encoding='utf-8'))
        if len(parser.links) != 1:
            raise ValueError('Required Legacy archive is absent or ambiguous in the official release catalog')
        return parser.links[0], True
    page = stage / 'source.html'
    download('https://www.etlegacy.com/workflow-files',page, 2097152, progress)
    # Select the published archive for precisely this Legacy version.
    links = re.findall(r'href=[\"\']([^\"\']+)[\"\']',page.read_text(encoding='utf-8'))
    for link in links:
        if link.endswith('/etlegacy-mod-' + version + '.zip'):
            return urljoin('https://www.etlegacy.com',html.unescape(link)), True
    raise ValueError('Required Legacy version is absent from the official snapshot catalog')


def install(root, game, name, checksum, base_url='', source_url='', progress=None):
    deadline = time.monotonic() + 210
    def report(**value):
        if time.monotonic() > deadline:
            raise TimeoutError('Server pack preparation timed out')
        if progress:
            progress(value)
    def resolving(**value):
        report(stage='resolving')
    if game not in ('etmain','legacy') or not PACK_NAME.fullmatch(name) or name.lower() in ('pak0.pk3','pak1.pk3','pak2.pk3','etloose.pk3'):
        raise ValueError('Invalid server asset path')
    if not -2147483648 <= checksum <= 2147483647:
        raise ValueError('Invalid server checksum')
    root = Path(root).resolve()
    directory = root / game
    directory.mkdir(parents=True,exist_ok=True)
    if not directory.resolve().is_relative_to(root):
        raise ValueError('Pack directory is outside the configured asset root')
    report(stage='waiting')
    while not _lock.acquire(timeout=1):
        report(stage='waiting')
    try:
        report(stage='checking')
        for candidate in directory.glob('*.pk3'):
            report(stage='checking')
            try:
                if candidate.resolve().is_relative_to(root) and pack_checksum(candidate) == checksum:
                    report(stage='verifying')
                    entry = inspect_pack(candidate,game,progress=lambda: report(stage='verifying'))
                    report(stage='ready')
                    return entry
            except TimeoutError:
                raise
            except (OSError, ValueError, zipfile.BadZipFile):
                continue
        if sum(1 for candidate in root.glob('*/*.pk3')) >= 64:
            raise ValueError('At most 64 extra packs are supported')
        with tempfile.TemporaryDirectory(dir=directory) as temp:
            stage = Path(temp)
            report(stage='resolving')
            if source_url:
                url, bundled = source_url, urlsplit(source_url).path.lower().endswith('.zip')
            elif base_url:
                url, bundled = base_url.rstrip('/') + '/' + game + '/' + name, False
            else:
                url, bundled = official_source(game,name,stage,resolving)
                if urlsplit(url).hostname != 'www.etlegacy.com':
                    raise ValueError('Unrecognized official package host')
            pack = stage / name
            payload = stage / 'payload.zip' if bundled else pack
            try:
                download(url,payload,progress=lambda **value: report(**value))
            except urllib.error.HTTPError as error:
                error.close()
                if error.code != 404 or not base_url or source_url:
                    raise
                # An operator mirror may omit newer official releases.
                report(stage='resolving')
                url, bundled = official_source(game,name,stage,resolving)
                if urlsplit(url).hostname != 'www.etlegacy.com':
                    raise ValueError('Unrecognized official package host')
                payload = stage / 'payload.zip' if bundled else pack
                download(url,payload,progress=lambda **value: report(**value))
            if bundled:
                report(stage='extracting')
                with zipfile.ZipFile(payload) as archive:
                    entries = [entry for entry in archive.infolist() if Path(entry.filename).name == name and not entry.is_dir()]
                    if len(entries) != 1 or entries[0].file_size > MAX_PACK:
                        raise ValueError('Archive does not contain the exact required pack')
                    with archive.open(entries[0]) as source, pack.open('wb') as target:
                        while block := source.read(1048576):
                            report(stage='extracting')
                            target.write(block)
            report(stage='verifying')
            entry = inspect_pack(pack,game,progress=lambda: report(stage='verifying'))
            if pack_checksum(pack) != checksum:
                raise ValueError('Downloaded pack does not match the game server checksum')
            destination = directory / name
            report(stage='publishing')
            with inventory_lock:
                if destination.exists():
                    destination = directory / (name[:-4] + '.' + f'{checksum & 0xffffffff:08x}' + '.pk3')
                if not destination.exists():
                    if sum(1 for candidate in root.glob('*/*.pk3')) >= 64:
                        raise ValueError('At most 64 extra packs are supported')
                    os.link(pack,destination)
                elif not destination.resolve().is_relative_to(root) or pack_checksum(destination) != checksum:
                    raise ValueError('Existing checksum-named pack conflicts with the required asset')
            entry['name'] = destination.name
            return entry
    finally:
        _lock.release()
