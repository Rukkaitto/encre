#!/usr/bin/env python3
"""Fetch a corpus of real EPUBs, and verify it against a committed manifest.

WHY A MANIFEST AND NOT A DIRECTORY: a refusal rate is a measurement, and a
measurement that cannot be repeated is an anecdote. The manifest is committed so a
run today and a run in three months are over the same books; the books themselves
are hundreds of megabytes and are never committed.

The cache defaults to ~/.cache/encre-corpus, deliberately outside the repo.

Usage:
    python3 tools/corpus.py fetch                     # everything in the manifest
    python3 tools/corpus.py verify                    # re-hash what is on disk
    python3 tools/corpus.py fetch --cache /some/dir
"""

import argparse
import hashlib
import os
import time
import urllib.error
import urllib.parse
import urllib.request

MANIFEST = os.path.join(os.path.dirname(os.path.abspath(__file__)), "corpus.manifest")
DEFAULT_CACHE = os.path.expanduser("~/.cache/encre-corpus")
# Gutenberg asks not to be crawled hard, and Standard Ebooks is one volunteer's
# server. One second between requests to the same host, and a User-Agent that says
# who is calling, is the price of using them at all.
USER_AGENT = "encre-corpus/1.0 (+https://github.com/Rukkaitto/encre)"
# MEASURED, not guessed: at one second Standard Ebooks returned twenty HTTP 429s and
# thirty-one books were skipped. It is one volunteer's server. Three seconds is the
# price of being allowed to use it, and a discovery run is a thing you do once.
POLITE_DELAY_S = 3.0


def read_manifest(path=MANIFEST):
    """source, sha256, size, url -- tab separated, '#' comments."""
    out = []
    if not os.path.exists(path):
        return out
    with open(path) as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            source, digest, size, url = line.split("\t")
            out.append({"source": source, "sha256": digest, "size": int(size), "url": url})
    return out


def cache_path(cache, entry):
    """Named by digest, so two sources naming one book cost one file and one fetch."""
    return os.path.join(cache, entry["source"], entry["sha256"][:16] + ".epub")


def sha256_of(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def download(url):
    if url.startswith("file://"):
        with open(urllib.parse.unquote(url[7:]), "rb") as fh:
            return fh.read()
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read()


# Gutenberg ids spread across the whole range on purpose: its EPUB generator has
# changed over twenty years, so an id from 1994 and one from last year are different
# toolchains wearing the same name.
GUTENBERG_IDS = list(range(1, 200)) + list(range(20000, 20100)) + list(range(60000, 60100))


def discover_gutenberg(limit):
    return ["https://www.gutenberg.org/ebooks/%d.epub3.images" % i
            for i in GUTENBERG_IDS[:limit]]


def discover_standardebooks(limit):
    """Standard Ebooks serves an HTML interstitial on the bare download URL; the
    `?source=download` its own meta-refresh points at is the file. Two formats per
    book, and the .kepub is the point -- it is Kobo's own dialect, which is a
    toolchain this corpus otherwise has no sample of.

    A book's path is /ebooks/author/title or /ebooks/author/title/translator, and the
    filename is those segments joined by underscores, so the depth is derived rather
    than assumed."""
    import re
    urls = []
    for page in range(1, 16):
        try:
            html = download("https://standardebooks.org/ebooks?page=%d" % page).decode("utf-8")
        except Exception as err:
            print("standardebooks: page %d unavailable (%s)" % (page, err))
            break
        found = 0
        for path in sorted(set(re.findall(r'href="(/ebooks/[^"?#]+)"', html))):
            parts = [p for p in path.split("/") if p][1:]  # drop "ebooks"
            if len(parts) < 2:
                continue
            name = "_".join(parts)
            urls.append("https://standardebooks.org%s/downloads/%s.epub?source=download"
                        % (path, name))
            found += 1
            # Every fourth book also in Kobo's dialect. Not all of them: the point is
            # a sample of a second toolchain, not a second copy of the corpus.
            if found % 4 == 0:
                urls.append("https://standardebooks.org%s/downloads/%s.kepub.epub?source=download"
                            % (path, name))
        time.sleep(POLITE_DELAY_S)
        if len(urls) >= limit:
            break
    return urls[:limit]


def discover_local(root, limit):
    """The author's own library: publisher output and Calibre, which is the only
    sample of the population this firmware will actually meet."""
    out = []
    for dirpath, _, names in os.walk(os.path.expanduser(root)):
        for n in sorted(names):
            if n.lower().endswith(".epub"):
                out.append("file://" + urllib.parse.quote(os.path.join(dirpath, n)))
    return sorted(out)[:limit]


def discover(sources, limit, root, cache):
    """DISCOVERY SAVES WHAT IT DOWNLOADS. It has to read every book to hash it, and
    hashing then discarding would make `fetch` download the whole corpus a second
    time -- 600 requests where 300 do, against two servers that asked to be treated
    politely. `fetch` still exists and is still the way to rebuild a cache from a
    manifest someone else committed."""

    found = []
    if "gutenberg" in sources:
        found += [("gutenberg", u) for u in discover_gutenberg(limit)]
    if "standardebooks" in sources:
        found += [("standardebooks", u) for u in discover_standardebooks(limit)]
    if "local" in sources and root:
        found += [("local", u) for u in discover_local(root, limit)]
    rows, failed = [], 0
    last_host = None
    for source, url in found:
        try:
            host = urllib.parse.urlsplit(url).netloc
            if host and host == last_host:
                time.sleep(POLITE_DELAY_S)
            last_host = host
            data = download(url)
        except Exception as err:
            print("skip %s (%s)" % (url, err))
            failed += 1
            continue
        # IT MUST BE A ZIP. Every remote source here has served an HTML page in place
        # of a book at least once, and a corpus quietly full of error pages would
        # report a refusal rate for a parser that was handed no books.
        if not data.startswith(b"PK\x03\x04"):
            print("skip %s (not a zip: %r)" % (url, data[:16]))
            failed += 1
            continue
        digest = hashlib.sha256(data).hexdigest()
        rows.append((source, digest, len(data), url))
        dst = cache_path(cache, {"source": source, "sha256": digest})
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, "wb") as fh:
            fh.write(data)
    rows.sort()
    with open(MANIFEST, "w") as fh:
        fh.write("# GENERATED by tools/corpus.py discover. source, sha256, bytes, url.\n")
        fh.write("# The books are NOT committed; `corpus.py fetch` puts them in the cache.\n")
        for r in rows:
            fh.write("%s\t%s\t%d\t%s\n" % r)
    print("manifest: %d books, %d skipped" % (len(rows), failed))
    return 0


def fetch(cache, manifest=MANIFEST):
    entries = read_manifest(manifest)
    if not entries:
        print("manifest is empty -- run `corpus.py discover` first")
        return 1
    have = got = failed = 0
    last_host = None
    for e in entries:
        dst = cache_path(cache, e)
        if os.path.exists(dst) and sha256_of(dst) == e["sha256"]:
            have += 1
            continue
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        host = urllib.parse.urlsplit(e["url"]).netloc
        if host and host == last_host:
            time.sleep(POLITE_DELAY_S)
        last_host = host
        try:
            data = download(e["url"])
        except (urllib.error.URLError, OSError) as err:
            print("FAILED %s: %s" % (e["url"], err))
            failed += 1
            continue
        digest = hashlib.sha256(data).hexdigest()
        if digest != e["sha256"]:
            # NOT a warning. The manifest is what makes the measurement repeatable,
            # so a book whose bytes changed is a different book and must not silently
            # enter the corpus under the old name.
            print("MISMATCH %s: manifest %s, got %s" % (e["url"], e["sha256"][:16], digest[:16]))
            failed += 1
            continue
        with open(dst, "wb") as fh:
            fh.write(data)
        got += 1
    print("%d already had, %d fetched, %d failed, of %d" % (have, got, failed, len(entries)))
    return 1 if failed else 0


def verify(cache, manifest=MANIFEST):
    entries = read_manifest(manifest)
    ok = bad = absent = 0
    for e in entries:
        dst = cache_path(cache, e)
        if not os.path.exists(dst):
            absent += 1
        elif sha256_of(dst) == e["sha256"]:
            ok += 1
        else:
            bad += 1
            print("CORRUPT %s" % dst)
    print("%d verified, %d corrupt, %d not fetched, of %d" % (ok, bad, absent, len(entries)))
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=["discover", "fetch", "verify"])
    ap.add_argument("--cache", default=DEFAULT_CACHE)
    ap.add_argument("--manifest", default=MANIFEST)
    ap.add_argument("--sources", default="gutenberg,standardebooks,local",
                    help="comma separated")
    ap.add_argument("--limit", type=int, default=120, help="books per source")
    ap.add_argument("--root",
                    default="~/Library/Mobile Documents/com~apple~CloudDocs/Calibre Library",
                    help="the local library, for --sources local")
    a = ap.parse_args()
    if a.command == "discover":
        return discover(set(a.sources.split(",")), a.limit, a.root, a.cache)
    return fetch(a.cache, a.manifest) if a.command == "fetch" else verify(a.cache, a.manifest)


if __name__ == "__main__":
    raise SystemExit(main())
