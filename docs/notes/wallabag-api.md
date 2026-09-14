# Wallabag instead of Instapaper — the decision and the surface

Decided 2026-09-14 by the owner, who has a server to host it. Checked against
**wallabag 2.6.14** (`GET /api/info` on `app.wallabag.it`) and its published
OpenAPI 3.0.0 document, which the Swagger page at `https://app.wallabag.it/api/doc`
inlines. **The API hands back the article text with no second key, no credits and
no signing — and it will hand it back as an EPUB, which this device already
reads.** `docs/notes/instapaper-full-api.md` is the investigation this supersedes;
its findings about Instapaper stay true and stop being ours to care about.

**Nothing here has been run against a real instance.** Everything below is read
off the spec, and the one fact the whole shape now rests on — that a wallabag
EPUB export opens in this reader — is a five-minute test on the owner's own
server and has not been done. See **What to check first**.

---

## 1. Why it is a better fit, in the order the reasons matter

### `export.epub` means there may be no new parsing layer at all

```
GET /api/entries/{entry}/export.{_format}
    _format ∈ [xml, json, txt, csv, pdf, epub, mobi]
```

**`epub` is in that enum**, straight from the spec. This firmware has a complete,
hardened EPUB reader — six streaming layers, `openBook`, `ChapterReader`,
`PageBuilder`, the page ring, the return anchor, progress persistence, covers,
all of it proved against a 225-book corpus. An article fetched this way is
**written to the card and opened with what already exists**.

Against the alternative this replaces, that is the difference between a feature
and a project:

| | Instapaper `get_text` | wallabag `content` | wallabag `export.epub` |
|---|---|---|---|
| what arrives | HTML | HTML | **EPUB** |
| new parsing in `core/` | a tag-soup tokenizer | a tag-soup tokenizer | **none** |
| second paid key | **yes** | no | no |

**AND IT RETIRES AN ERROR IN `instapaper-full-api.md` §6, NOT JUST A COST.** That
section files the HTML tokenizer under "the fallback if `get_text` is
unaffordable", and that was wrong twice over: `document.cpp` parses through
`xml.h`, which is **strict XML by construction**, so *any* HTML-bearing backend
needs tag-soup tolerance — it was baseline, not fallback. `export.epub` is the
only one of the three that removes the requirement instead of relocating it.

### No second key, no credits, no rate ceiling

The whole of `instapaper-full-api.md` §2 — the Instaparser key, the 1,000
credits a month across the fleet, the `1044`–`1047` errors, the $150/month step,
and §7.4's unanswered per-call-or-per-article question — **does not exist here.**

### No signing, and no secret of ours to hide

`POST /oauth/v2/token` with `grant_type=password`, `client_id`, `client_secret`,
`username`, `password` returns `access_token`, `refresh_token`, `expires_in`
(3600) and `token_type: bearer`; every later call carries
`Authorization: Bearer <token>`.

**A plain form POST and a header.** No OAuth 1.0a, no HMAC-SHA1, so the signer
card is not needed — and `instaparser`'s §7.9, *"is a consumer secret compiled
into public firmware published or shared"*, **evaporates**: the client is created
by the user on their own instance at `/developer/client/create`, so the firmware
ships no secret and there is no third party to register with or be reviewed by.

### The TLS question may not arise

A self-hosted instance on the owner's own network can be plain HTTP, which is
**#112's largest unmeasured risk removed rather than measured** — an mbedTLS
handshake on a C3 with no PSRAM against a reading floor of 28,508 bytes. It comes
straight back if the instance is reached over the internet, so this is a property
of a deployment and not of the design.

## 2. The surface the feature needs, from the spec

| want | call |
|---|---|
| unread list, no bodies | `GET /api/entries?archive=0&detail=metadata&perPage=N` |
| only what changed | the same plus `since=<unix ts>` |
| the article | `GET /api/entries/{entry}/export.epub` |
| archive | `PATCH /api/entries/{entry}?archive=1` |
| like | `PATCH /api/entries/{entry}?starred=1` |
| is it reachable / which version | `GET /api/info` — needs no token |

Two of those are better than the Instapaper design assumed:

- **`detail=metadata` excludes the `content` field.** The Articles list costs
  metadata only, so the board's list screen never pulls an article body it is not
  going to show. Instapaper had no equivalent.
- **`since` is a timestamp.** Incremental sync is one integer, where Instapaper's
  `have` wanted `id:hash:progress:timestamp` tuples for every bookmark held. For
  a device that syncs rarely and holds its state on a card, this is the cheaper
  contract by a wide margin.

An entry carries `id`, `title`, `url`, `domain_name`, `content`, `created_at`,
`updated_at`, `is_archived`, `is_starred`, `language`, `mimetype`,
`preview_picture`, `reading_time`, `tags` — so `Articles.dc.html`'s rows and
`ArticleEnd`'s reading time need no derivation and no second call.

## 3. What it costs, stated rather than discovered

- **The reader has to run a server.** That is the trade, and it is the whole of
  it: "sign in to Instapaper" is a feature anyone can use, "point it at your
  wallabag" is a feature for people who already self-host. It narrows the
  audience and it is the owner's call, taken.
- **The token expires in 3600 s.** Instapaper's did not. So the client owns a
  refresh, and a refresh that fails has to fall back to the stored credentials
  rather than to a screen nobody is standing in front of.
- **FIVE VALUES REACH THE DEVICE, TWO OF THEM ~50-CHARACTER RANDOM STRINGS.**
  Server URL, `client_id`, `client_secret`, username, password. Measured against
  the shipped keyboard in `instapaper-full-api.md` §6, a 17-character email is 92
  presses; a 51-character `client_id` is several hundred. **The keyboard is not a
  credible route for this** and that is an open design question — see below.
- **Unverified: that a wallabag EPUB opens in this reader.** It is one file from
  one generator, and this reader has been wrong about real EPUBs before.

## 4. What to check first, and it is cheap

**Export one article from the owner's instance as `.epub`, put it in `/books`,
and open it on the device.** That is the fact the whole shape rests on, it needs
no firmware change, and it costs five minutes. Three things to look at, each one
something this project has already been bitten by:

1. **Does it open at all** — `Epub::open` validates every spine entry against the
   manifest and refuses the book if one is missing.
2. **Is the text there** — a converted single article may be one spine entry, and
   spine entry 0 of a real book is usually a cover that paginates to nothing.
3. **What the title and author read as** — Home, the Library and the sleep card
   all draw `dc:title` and `dc:creator`, and a generator that leaves them empty
   makes three screens look broken.

If it fails, `detail=full`'s `content` is the fallback and the HTML tokenizer is
back on the bill — which is exactly the estimate §6 of the other note already
carries, so nothing is lost but the shortcut.
