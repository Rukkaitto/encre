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

## 3. Where the credentials come from

Read from wallabag's own source at `wallabag/wallabag`, because it decides whether
the five values can reach the device at all.

**The `client_id` is `<id>_<50 random chars>`** — `Client::getPublicId()` is
`getId() . '_' . getRandomId()` (`src/Entity/Api/Client.php:108`), so the
instance's own how-to page shows
`12_5um6nz50ceg4088c0840wwc0kgg44g00kk84og044ggkscso0k`. The secret is a bare
50-character random string. **That is ~103 characters of random text before
anybody types a username**, and it is the whole of why §4 says the keyboard
cannot carry this.

### The web route, which works on any instance

Log in, visit `/developer/client/create`, give it a redirect URI — the docs say
*"if your application is a desktop one, use any URL that suits your needs"* — and
it shows both values with copy-to-clipboard buttons.

**And unlike Instapaper's, the secret is NOT a one-time reveal.**
`templates/Developer/index.html.twig:44` renders `client.secret` in the
existing-clients list, so `/developer` shows it again whenever it is wanted. A
reader who loses the card, or the file on it, goes and looks rather than
re-registering — which is worth knowing before designing any recovery flow,
because there is nothing to recover.

### The console route, which is why self-hosting is the easier case

`src/Command/CreateApiClientCommand.php`:

```
php bin/console wallabag:api-client:create <username> --format=json
```

prints

```json
{ "client_id": "…", "client_secret": "…", "name": "…" }
```

**Those key names are the ones a config file wants**, so on a self-hosted
instance the credentials can be generated and copied to the card without either
value passing through a human's hands or a clipboard. `--display-name` names the
client in the web list, which is how a reader later revokes *this device* rather
than all of them.

**`--grant-types` narrows what the client may do.** The allowed set is `token`,
`authorization_code`, `password`, `refresh_token` and the default is all four;
this device needs **`password,refresh_token`** and nothing else, so that is what
the instructions should say. A client that cannot do `authorization_code` is one
fewer thing a leaked card can be used for.

## 4. What it costs, stated rather than discovered

- **The reader has to run a server.** That is the trade, and it is the whole of
  it: "sign in to Instapaper" is a feature anyone can use, "point it at your
  wallabag" is a feature for people who already self-host. It narrows the
  audience and it is the owner's call, taken.
- **The token expires in 3600 s.** Instapaper's did not. So the client owns a
  refresh, and a refresh that fails has to fall back to the stored credentials
  rather than to a screen nobody is standing in front of.
- **FIVE VALUES REACH THE DEVICE, AND ~103 CHARACTERS OF THEM ARE RANDOM.**
  Server URL, `client_id` (`<id>_<50 chars>`), `client_secret` (50 chars),
  username, password — the format is §3's, off the entity rather than off an
  example. Measured against the shipped keyboard in `instapaper-full-api.md` §6,
  a 17-character email is **92 presses**; this is comfortably past six hundred
  before a password. **The keyboard is not a credible route** and that is an open
  design question — **answered in §5**: the credentials arrive on the card.
- **Unverified: that a wallabag EPUB opens in this reader.** It is one file from
  one generator, and this reader has been wrong about real EPUBs before.

## 5. The credentials arrive on the card, and the flow end to end

**Decided 2026-09-14.** `/.reader/wallabag.json`, hand-edited, five strings. It is
`logToCard`'s precedent — a hand-edited card key with no Settings row — and needs
no mechanism this firmware does not have: `readAll` plus the flat one-object JSON
parser `settings.json` already uses. It closes the alternatives rather than
deferring them: **no HTTP server** (#110 stays closed), **no keyboard for this**
(#126 keeps its own argument and stops being #111's blocker), no password on
glass and none over plain HTTP on a LAN.

```json
{ "server": "http://wallabag.lan", "clientId": "12_5um6…", "clientSecret": "3qd1…",
  "username": "…", "password": "…" }
```

**THE PASSWORD STAYS IN THE FILE, AND THAT IS EVIDENCED RATHER THAN ASSUMED.**
wallabag's own browser extension documents its token as expiring *"once in two
weeks"*, so the refresh token is good for about a fortnight — and this device
sleeps for days at a time. A reader who does not sync for three weeks needs a
fresh `grant_type=password`, and there is nobody standing in front of the panel
to ask. Deleting the password after first use would strand exactly that reader.

**The stated cost is plaintext on a removable card**, and two things bound it
rather than excuse it. It is the reader's own card and their own server. And
`--grant-types=password,refresh_token` (§3) means the client on that card cannot
be used for an `authorization_code` flow at all, so what a lost card gives up is
one wallabag account, not a credential with more reach than the device needs.
**wallabag's own ecosystem makes the same trade and says so**: Wallabagger's
documentation carries a *"Security warning — your password is stored in the
browser local storage as a plain text"*. That is a precedent, not a defence.

### What the device does with it

`GET /api/info` needs no token, so *"that URL is not a wallabag"* is answerable
before any credential is used and is a different message from *"those credentials
were refused"*. Then `POST /oauth/v2/token`, tokens into NVS, and the list.

**Neither timestamp the design wants needs a clock, which matters because this
device has none** (#25). `since` is served by storing the largest `updated_at`
the last sync returned and sending it back — the server's own clock, never ours.
And `expires_in: 3600` is relative to a grant that may have happened before a
deep sleep, which resets `millis()`, so the device **cannot predict expiry and
must not try**: call, and refresh on a 401. That is one extra round trip on the
first call after a long sleep and no state to get wrong.

### The whole path, and the four things it leaves open

Setting up wallabag, and putting articles in it, are entirely outside this
firmware — a bookmarklet, the web UI's `+`, the official browser extension or the
mobile apps. **Encre never adds an article**: it has no browser and no practical
way to type a URL, which is a fact about the device rather than a gap. What the
device does is join Wi-Fi (#107, and that flow *does* use the keyboard, so the
keyboard is on the setup path whatever this decision says), read the card file,
sync, and read.

Four things this path raises that no card answers yet:

1. **When is the EPUB fetched — at sync, or when the article is opened?**
   `InstapaperAccount.dc.html` reads `Keep offline · NEWEST 50`, and fetching at
   open would need Wi-Fi at reading time, which is the opposite of what an
   offline reader is for. So: at sync — which makes a sync *one listing plus N
   file downloads* and is what `SyncDone` stamps. #112.
2. **Where do article files live?** Not `/books`: they would appear in the
   Library among books, and the Library's own delete would remove something the
   server still has. #114.
3. **Do articles reach Home's CONTINUE block and the sleep card?** They are
   opened through `openBook`, so `last.json` would carry them for free — which is
   probably right and is a design decision, not a consequence to discover. #114.
4. **Does archiving delete the local file?** It should, or the card fills with
   things the reader has finished — and that drops the `/.reader/state` sidecar
   with it, which for an archived article is correct. #112.

## 6. What to check first, and it is cheap

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
