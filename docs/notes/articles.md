# Articles over wallabag

Extracted from `CLAUDE.md`, which keeps a stub under this heading and is where
the cross-references to it point. Same standing as anything in that file.

Five cards, one feature: a list on the card, a sync over the radio, and an
article that reads through `openBook` like any other EPUB. `docs/notes/wallabag-api.md`
holds the API and the measurements; what is here is the six decisions the code
does not explain by itself, and the one hardware fact that shaped all of them.

**AN ARTICLE IS AN EPUB ON THE CARD AND NOTHING BELOW THE SHELL KNOWS OTHERWISE.**
It goes through `openBook`, the same Reader, the same page ring, the same
sidecar; `last.json` carries its path exactly as it carries a book's, so Home's
CONTINUE offers it and the sleep card names it. **The ONE thing that differs is
which board the last page turns into** — `ReaderScreen::setEndScreen`, `BookEnd`
by default and `ArticleEnd` when the path is under `/.reader/articles/`. A second
Reader would have been a second copy of paging, the rewind, the ring and the
index, each carrying rules a copy would have to re-earn. Derived from the PATH in
one place rather than threaded down, because two of the three callers — the wake
restore and Home's CONTINUE — do not know what they are opening.

**NO AGES ANYWHERE, BECAUSE THIS DEVICE HAS NO CLOCK (#132).** Every timestamp is
the server's, handed back untouched as the `since` watermark. The list's stamp
and the account screen's `Last sync` are OUTCOMES (`NEVER`, `NO NEW`, `3 NEW`,
`FAILED`) and never `2 H AGO`, which the boards' own notes carry. A device that
cannot tell the time must not draw a clock.

**THE QUEUE IS MARKER FILES, and the alternative was a mutable list.** One empty
file per pending action under `/.reader/articles/queue/`, named `<id>.archive` or
`<id>.star`. Two properties fall out that a list does not have: starring and then
unstarring owes the server ONE action rather than two contradictory ones, because
the opposite marker is REMOVED rather than a second one appended; and a power cut
mid-queue leaves a directory that is still correct, where a rewritten list can be
half written.

**PER-ARTICLE SIDECARS, NOT THE FLAT PARSER.** `/.reader/articles/<id>.json` per
article, read by `JsonScanner` — the pull tokenizer written for the listing —
rather than by `json.h`, which has no nesting and no arrays and is the settings
file's parser. A listing page is 5 KB of HAL with `_links` inside every item; the
flat parser cannot see it, and widening it for this would have made the settings
file's parser something the settings file does not need.

**PUSH BEFORE PULL, ALWAYS.** The sync sends what the device owes before it asks
what is new, so a star made offline is on the server before the listing that
would otherwise report the entry unstarred and overwrite it. It is also why a
completed sync's stamp is empty: the queue is cleared by definition.

**AND THE WATERMARK ADVANCES ONLY AFTER EVERY DOWNLOAD LANDED.** A sync that
fetched nineteen of twenty does not move `since`, so the next one asks for all
twenty again and fetches the one that is missing — which is what makes cancelling
free rather than merely cheap. `since` also drops `archive=0`: without that an
entry archived on a phone would never come back, and the local file would sit
there for ever.

**THE HARDWARE FACT THAT SHAPED THE REST: A SYNC RESTARTS THE DEVICE.** One
verified TLS handshake takes the largest free block from 61,428 bytes to 34,804
and never returns it above 36,852, against `Inflater::begin`'s 36,956 — so a
device that has synced cannot open a book until it resets. The free heap recovers
in full every time, which is why nothing saw it: see **Hardware facts**, which has
the measurements. Three consequences worth knowing here:

- **The two body glyph arenas are given back for the length of a sync** — 26 KB
  that nothing in this flow draws with, since every screen here uses the embedded
  `.rfnt` ramp. Without it the fourth request aborted with 716 bytes free.
- **The restart is invisible because the result is on the CARD.** The stamp comes
  from the watermark, `WallabagConnecting` is `Restore::Never` so the record
  already reads `…;articles:N`, and e-ink holds the fetching screen through the
  reset. What the reader sees is one transition flash.
- **It is gated on the TRANSPORT's scheme**, so a plain-HTTP server — the LAN case
  — never pays a restart it does not owe, a plain round trip costing no block at
  all. And it is taken when the error panel is DISMISSED rather than when a sync
  fails, because that screen is `Restore::Never` too.

**THE TRANSPORT ASKS FOR HTTP/1.0, AND THAT IS NOT A PREFERENCE.** `HTTPClient`
de-chunks only inside `writeToStream()`, so a poll-shaped reader taking
`getStreamPtr()` receives the chunk framing along with the body — a 5,368-byte
listing arrived as `14eb\r\n{…}\r\n0\r\n\r\n` and the parser correctly refused
it. HTTP/1.0 has no chunked encoding, so the framing cannot appear. Writing a
de-chunker was the alternative: a second parser, on the path where the heap is
scarcest, for bytes the protocol lets us decline.

**AND A `PATCH`'s PARAMETERS GO IN THE BODY.** wallabag reads them off Symfony's
`$request->request`, which FOSRestBundle fills from the body; PHP never populates
`$_POST` for a PATCH. A query string reaches nothing and the server answers
**200** anyway — so the push looked like it worked, the queue acked on the 2xx,
and the intent was discarded. **A wrong 200 is the worst answer this API can give
us**, because nothing downstream can tell it from a right one.
