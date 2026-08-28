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
POLITE_DELAY_S = 1.0


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
    ap.add_argument("command", choices=["fetch", "verify"])
    ap.add_argument("--cache", default=DEFAULT_CACHE)
    ap.add_argument("--manifest", default=MANIFEST)
    a = ap.parse_args()
    return fetch(a.cache, a.manifest) if a.command == "fetch" else verify(a.cache, a.manifest)


if __name__ == "__main__":
    raise SystemExit(main())
