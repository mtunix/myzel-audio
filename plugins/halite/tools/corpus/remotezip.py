"""List or extract members of a zip on a web server without downloading the whole archive.

    python3 -I remotezip.py list <url> [glob ...]
    python3 -I remotezip.py size <url-or-local-zip> [--dest <dir>] [glob ...]
    python3 -I remotezip.py extract <url> <dest-dir> <glob> [glob ...]

Reads the central directory and the chosen members with HTTP Range requests (the server must
answer 206). zipfile checks each member's CRC-32, so a partial fetch is still verified per file.
Members already present at their full size are skipped, so an interrupted run resumes; `size`
with --dest also reports the bytes still missing there, for the caller's disk check.
"""

import collections
import fnmatch
import http.client
import io
import os
import sys
import time
import urllib.error
import urllib.request
import zipfile

CHUNK = 1 << 20
BLOCK = 256 << 10
CACHE_BLOCKS = 64    # 16 MB: one read (at most half of it) plus the read-ahead always fit
MAX_AHEAD = 16
RETRIES = 6
USER_AGENT = "myzel-corpus-fetch/1"


class HttpRangeFile(io.RawIOBase):
    """A read-only, seekable file over HTTP Range requests."""

    def __init__(self, url):
        self.url = url
        self.pos = 0
        self.size = self._probe_size()
        self.fetched = 0
        self.blocks = collections.OrderedDict()
        self.ahead = 1
        self.next_block = -1

    def _request(self, first, last):
        req = urllib.request.Request(self.url, headers={"Range": f"bytes={first}-{last}",
                                                        "User-Agent": USER_AGENT})
        return urllib.request.urlopen(req, timeout=60)

    def _fetch(self, first, last):
        """Bytes first..last, retried with back-off; a 429 or 503 waits as long as the server asks."""
        for attempt in range(RETRIES):
            try:
                with self._request(first, last) as r:
                    if r.status != 206:
                        raise OSError(f"{self.url}: server ignores Range requests (HTTP {r.status})")
                    data = r.read()
                if len(data) != last - first + 1:
                    raise OSError(f"short read at {first}")
                return data
            except urllib.error.HTTPError as e:
                if e.code not in (429, 500, 502, 503, 504) or attempt == RETRIES - 1:
                    raise
                wait = float(e.headers.get("Retry-After") or 2 ** attempt)
            except (OSError, http.client.HTTPException) as e:
                if attempt == RETRIES - 1 or "ignores Range" in str(e):
                    raise
                wait = 2 ** attempt
            print(f"  retrying in {min(wait, 120):.0f} s (bytes {first}-{last})", file=sys.stderr)
            time.sleep(min(wait, 120))
        raise OSError("unreachable")

    def _probe_size(self):
        with self._request(0, 0) as r:
            if r.status != 206:
                raise OSError(f"{self.url}: server ignores Range requests (HTTP {r.status})")
            return int(r.headers["Content-Range"].rsplit("/", 1)[1])

    def _load(self, first_block, last_block):
        """Fetches the missing blocks of a span in as few requests as possible."""
        last_in_file = (self.size - 1) // BLOCK
        b = first_block
        while b <= last_block:
            if b in self.blocks:
                self.blocks.move_to_end(b)
                b += 1
                continue
            run_end = b
            while run_end + 1 <= last_block and run_end + 1 not in self.blocks:
                run_end += 1
            # Sequential reads (a member's data) fetch further ahead each time; a jump starts small
            # again, so scattered members don't drag in their neighbours.
            self.ahead = min(2 * self.ahead, MAX_AHEAD) if b == self.next_block else 1
            while run_end < min(b + self.ahead - 1, last_in_file) and run_end + 1 not in self.blocks:
                run_end += 1
            first = b * BLOCK
            data = self._fetch(first, min(self.size, (run_end + 1) * BLOCK) - 1)
            self.fetched += len(data)
            for i in range(b, run_end + 1):
                self.blocks[i] = data[(i - b) * BLOCK:(i - b + 1) * BLOCK]
            while len(self.blocks) > CACHE_BLOCKS:
                self.blocks.popitem(last=False)
            self.next_block = run_end + 1
            b = run_end + 1

    def readable(self):
        return True

    def seekable(self):
        return True

    def tell(self):
        return self.pos

    def seek(self, offset, whence=io.SEEK_SET):
        base = {io.SEEK_SET: 0, io.SEEK_CUR: self.pos, io.SEEK_END: self.size}[whence]
        self.pos = max(0, base + offset)
        return self.pos

    def readinto(self, buffer):
        if self.pos >= self.size or len(buffer) == 0:
            return 0
        # A short read keeps the span inside the cache; BufferedReader asks again for the rest.
        end = min(self.size, self.pos + len(buffer), (self.pos // BLOCK + CACHE_BLOCKS // 2) * BLOCK)
        self._load(self.pos // BLOCK, (end - 1) // BLOCK)
        n = 0
        while self.pos < end:
            block = self.blocks[self.pos // BLOCK]
            offset = self.pos % BLOCK
            piece = block[offset:offset + (end - self.pos)]
            buffer[n:n + len(piece)] = piece
            n += len(piece)
            self.pos += len(piece)
        return n


def open_remote(url):
    if os.path.exists(url):
        return None, zipfile.ZipFile(url)
    raw = HttpRangeFile(url)
    return raw, zipfile.ZipFile(io.BufferedReader(raw, buffer_size=BLOCK))


def matching(zf, globs):
    names = [i for i in zf.infolist() if not i.is_dir()]
    if not globs:
        return names
    # A subset names thousands of members; plain names are looked up, not matched one by one.
    exact = {g for g in globs if not any(c in g for c in "*?[")}
    patterns = [g for g in globs if g not in exact]
    return [i for i in names
            if i.filename in exact or any(fnmatch.fnmatchcase(i.filename, g) for g in patterns)]


def present(dest, info):
    target = os.path.join(dest, info.filename)
    return os.path.exists(target) and os.path.getsize(target) == info.file_size


def extract(url, dest, globs):
    raw, zf = open_remote(url)
    members = matching(zf, globs)
    for info in members:
        target = os.path.join(dest, info.filename)
        if not os.path.realpath(target).startswith(os.path.realpath(dest) + os.sep):
            raise OSError(f"refusing member outside the destination: {info.filename}")
        if present(dest, info):
            continue
        os.makedirs(os.path.dirname(target), exist_ok=True)
        partial = target + ".part"
        with zf.open(info) as src, open(partial, "wb") as out:
            while block := src.read(CHUNK):
                out.write(block)
        os.replace(partial, target)
    print(f"  transferred {raw.fetched / 1e6:.1f} MB")


def main(argv):
    if len(argv) >= 2 and argv[0] == "list":
        _, zf = open_remote(argv[1])
        for info in matching(zf, argv[2:]):
            print(f"{info.file_size:>12}  {info.filename}")
        return 0
    if len(argv) >= 2 and argv[0] == "size":
        dest, globs = (argv[3], argv[4:]) if argv[2:3] == ["--dest"] and len(argv) >= 4 else (None, argv[2:])
        _, zf = open_remote(argv[1])
        members = matching(zf, globs)
        print(f"members {len(members)}")
        print(f"bytes {sum(i.file_size for i in members)}")
        print(f"transfer {sum(i.compress_size for i in members)}")
        if dest is not None:
            print(f"missing {sum(i.file_size for i in members if not present(dest, i))}")
        return 0
    if len(argv) >= 4 and argv[0] == "extract":
        extract(argv[1], argv[2], argv[3:])
        return 0
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
