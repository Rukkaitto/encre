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

### The web route, which is the normal one and needs no terminal

Log in, visit `/developer/client/create`, and the form has **two fields, one of
them optional**: `name`, and `redirect_uris` with `'required' => false`
(`src/Form/Type/Api/ClientType.php`). The docs say *"provide the redirect URL of
your application … if your application is a desktop one, use any URL that suits
your needs"*, and the form is softer than the docs — **a device can leave it
blank**. Both values then appear with copy-to-clipboard buttons.

**It offers no control over grant types**, and that is not a gap in the form but
a decision in the controller: `Api\DeveloperController.php:47` calls
`setAllowedGrantTypes(['token', 'authorization_code', 'password',
'refresh_token'])` — hardcoded, all four, for every client made this way.

**And unlike Instapaper's, the secret is NOT a one-time reveal.**
`templates/Developer/index.html.twig:44` renders `client.secret` in the
existing-clients list, so `/developer` shows it again whenever it is wanted. A
reader who loses the card, or the file on it, goes and looks rather than
re-registering — which is worth knowing before designing any recovery flow,
because there is nothing to recover.

### The console route, which a self-hoster may prefer and nobody needs

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

**`--grant-types` narrows what the client may do**, and it is the one capability
the web form does not have: the allowed set is `token`, `authorization_code`,
`password`, `refresh_token`, the default is all four, and this device needs
`password,refresh_token`.

**IT IS TIDINESS RATHER THAN DEFENCE, WHICH §5 GOT WRONG.** That section called
it *"a real answer to the plaintext-on-the-card cost"* and it is not: the same
file carries the username and the password, so anyone holding the card gets a
token through the `password` grant whatever else the client is allowed to do.
Narrowing removes grants this device will not use; it does not narrow what a lost
card gives up. **And the instructions cannot require it anyway**, because the
route most readers will take is the web form, which has no such control.

## 4. What it costs, stated rather than discovered

- **The reader has to have a wallabag, and every setup step is theirs.** "Sign in
  to Instapaper" is a feature anyone can use; this one is for people who
  self-host **or will pay wallabag.it from €11/yr** — which is the half that
  keeps it addressable rather than personal. It narrows the audience, the owner's
  call is taken, and **§5 prices it step by step** rather than leaving it as this
  sentence, because the developer pays none of it and a note written by the
  developer will forget that.
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

**The stated cost is plaintext on a removable card, and it is not bounded by
anything clever.** What a lost card gives up is one wallabag account — the file
carries the username and the password, so the `password` grant is available to
whoever holds it and no grant-type narrowing changes that. §3 offers
`--grant-types=password,refresh_token` and **this section previously called it "a
real answer" to this cost, which was wrong twice over**: it does not reduce what
the card exposes, and it is unavailable on the web route most readers will take.

What actually bounds it is the deployment: the reader's own card, their own
server, and an account whose blast radius is their reading list.
**wallabag's own ecosystem makes the same trade and says so plainly** —
Wallabagger's documentation carries a *"Security warning — your password is
stored in the browser local storage as a plain text"*. A precedent, not a
defence.

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

### What it costs a READER, which is what the developer never pays

**Every one of these steps is the reader's, every time. There is nothing for the
developer to do, ever, and that is the same fact as "the firmware ships no
secret" seen from the other side.** Instapaper's shape was the reverse: one
registration by us, baked in, and the reader typed an email and a password. This
note argued that asymmetry as an advantage four times without once pricing the
half a reader pays, so here it is.

| # | step | whose | what it takes |
|--:|---|---|---|
| 1 | **have a wallabag** — self-host, or **wallabag.it from €11/yr** | wallabag's | hours and a server, **or five minutes and €11** |
| 2 | an account on it | wallabag's | trivial |
| 3 | a way to save articles — the browser extension | wallabag's | **it needs its own client id and secret too** |
| 4 | an API client for the device, `/developer/client/create` | ours to document | one web form, two fields, redirect URI blank |
| 5 | write `/.reader/wallabag.json` | **ours** | **hand-written JSON — the weakest link** |
| 6 | join Wi-Fi | ours, shipped (#107) | the six-button keyboard |

**STEP 1 IS THE FILTER AND EVERYTHING ELSE IS NOISE BESIDE IT.** Self-hosting is
not a step, it is a hobby, and assuming it because the owner does it is how this
feature would ship for an audience of one. **wallabag.it collapses it to a
signup** — from €11 a year, hosted in Europe, a 14-day trial that takes no card —
and **that belongs at the top of any setup document rather than in a footnote**,
because it is the difference between "you need a server" and "you need €11 or a
server". Encre's audience already flashes its own firmware, so the overlap with
self-hosters is real; it is not total, and the hosted option is what makes the
feature addressable rather than personal.

**Step 3 means the client dance happens twice.** Wallabagger's own setup wants a
client id and secret exactly as we do, so a reader arriving at step 4 has
probably done it once already — which makes step 4 familiar rather than novel,
and is worth saying in the copy for that reason. It is still two.

**Step 5 is ours and it is the one that produces a device that silently does
nothing.** A missing comma, a wrong key, a `.reader` folder the file manager
hides, macOS writing `._wallabag.json` beside it. **So the firmware seeds the
file** — see below, which is this project's own precedent rather than a new idea.

### The file is SEEDED, not demanded

`armCardProbes()` already writes `/.reader/settings.json` when the card has none
(`shell/src/main.cpp:1586`), and `saveSettings` creates `/.reader` on the way. So
a card that has been in this device once already has the folder and a worked
example of the format. **`wallabag.json` gets the same treatment**: written with
the five keys and empty string values when absent, so the reader fills in blanks
in a file that exists rather than authoring JSON from a wiki page.

Three things it inherits and one it must not:

- **Only when ABSENT.** A file that exists is the reader's, however wrong it is.
  `loadAndApplySettings` already refuses to overwrite a corrupt settings file —
  *"overwriting it would destroy the only copy of their edit"* — and a
  half-finished `wallabag.json` is exactly that.
- **A failed write is reported, not assumed**, in `armCardProbes`'s own idiom:
  the card may be full, write-protected or failing, and a seed that did not land
  must not read as one that did.
- **Empty values are NOT a configuration error.** A seeded file with blank
  strings is the normal state of a device nobody has set up yet, so it reads as
  *not configured* and lands on `ArticlesSetup`, never on an error.
- **It does NOT join the probe's reason.** The settings file is created where it
  is because `useFileProbeTarget` needs a guaranteed target to read; this one has
  no such job and must not become a second one, or a reader who deletes it takes
  the card probe down with it.

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

## 6. It was checked, and it works

**2026-09-15, against a real export from the owner's own instance** — *Why Is
Everything An "Epidemic" Now?*, 60,853 bytes, generated by `PHPePub 4.0.6`.
Run through this repo's own desktop probes at `Release`, not inspected by eye.

| check | result |
|---|---|
| `Epub::open` accepts the spine | **yes** — `opened: true`, `unreadable: 0` of 4 |
| `dc:title` | **`Why Is Everything An “Epidemic” Now?`** — curly quotes, and both are in the subset |
| `dc:creator` | **`Cole Hastings`** |
| text extracted | **30 blocks, 7,964 bytes**, `truncated: 0`, `splits: 0` |
| largest block | **644 B** against `kMaxBlockBytes`' 8 KB — nowhere near the cap |
| pagination (`paging_probe --whole`) | **25 turns, 0 duplicate pages** |
| with interrupted idle walks (`--idle --interrupt 2`) | **48 walks cut, still 0 duplicates** |
| every codepoint against `fontc.py`'s subset | **all 8,717 characters inside it — no notdef risk** |
| the cover | `decodeCover` → **`Ok`**, 768×403 at 1/1, renders legibly in four levels |

**So #114's shortcut holds: an article is written to the card, opened with
`openBook`, and read by what already exists.** The HTML tokenizer stays off the
bill and the `content`-field fallback stays unused.

### Three things the file taught that the spec did not

- **Three of its four spine entries are wrappers.** `CoverPage.xhtml`,
  `<hash>_cover.html`, the article, `Cover2.html` — so **the cover-paginates-to-
  nothing case is live on every article**, not an edge. `ReaderScreen` already
  owns it (*"an entry with no pages is skipped in whichever direction the reader
  was already going"*), and 25 turns over 7,964 bytes is that working.
- **It is EPUB 3.0 carrying BOTH an NCX and a nav document**, with
  `spine toc="ncx"`. `toc.h` reads the NCX and it is there, so the measurement
  that chose the NCX over the nav document holds for wallabag too.
- **`dc:description` is the generic `"Some articles saved on my wallabag"` and
  `dc:date` is the EXPORT time**, not the article's. Nothing reads either today;
  worth knowing before anything does.

### What this did NOT check

- **No reader page was rendered to pixels.** `reader_sim app` cannot open a book
  from the Library — `Action::Open` carries no path and the SHELL resolves it —
  so the text path was proved by the probes and the subset scan rather than by
  looking. The drawing below `document.h` is shared with every other EPUB and has
  nothing article-specific in it, which is why that is sufficient here and would
  not be for a new screen.
- **Nothing on device.** No HTTP, no card write, no heap under a real session.
- **One file from one generator.** It is the generator every wallabag emits, and
  it is one article.

### And one question it raises for #112

The `guide` reads `title="Entry 1 of 1"`, so this generator can put **several
entries in one EPUB** — wallabag's web UI exports a whole list that way. The API
route we costed is per entry (`/api/entries/{entry}/export.epub`), which makes a
sync *N files*. **One book per article or one per sync is a real design choice**
— the second is fewer requests and one `openBook`, the first is what the Articles
list and per-article progress assume. #112's.

## 7. The check itself, kept because it is re-runnable

**Export one article as `.epub` and run the repo's own probes over it.** No
firmware change, and it is the fact the whole shape rests on, so it is worth
re-running whenever wallabag's generator moves:

```
./build/corpus_probe article.epub          # opened, title, author, spine, blocks, splits
./build/paging_probe article.epub --whole --idle --interrupt 2
./build/reader_sim  cover article.epub out.png --canvas 528x792 --fit whole
``` Three things to look at on a re-run, each one
something this project has already been bitten by:

1. **Does it open at all** — `Epub::open` validates every spine entry against the
   manifest and refuses the book if one is missing.
2. **Is the text there** — a converted single article may be one spine entry, and
   spine entry 0 of a real book is usually a cover that paginates to nothing.
3. **What the title and author read as** — Home, the Library and the sleep card
   all draw `dc:title` and `dc:creator`, and a generator that leaves them empty
   makes three screens look broken.

If a future export fails any of them, `detail=full`'s `content` is the fallback
and the HTML tokenizer is back on the bill — the estimate `instapaper-full-api.md`
§6 carries — so nothing is lost but the shortcut.

---

## 8. What a round trip costs on the C3 — the probe, and the rule before the numbers

**NOT YET RUN.** This section is written before the measurement on purpose: #140
asks for the probe at the point where the answer can still change the design, and
a decision rule written afterwards is a rule fitted to whatever turned up.

### Why it cannot be answered from a desktop

The reading floor with Wi-Fi linked is **28,508 bytes**, measured on glass, and
the stack already costs **21,328 bytes of static RAM at every instant** whether
or not the radio is ever switched on. What nothing here can price is an mbedTLS
handshake on a part with no PSRAM. This project has been wrong about the C3 from
desktop evidence three times — the `__divdi3` in a hot loop the desktop does not
have, the cover decode that peaked **17–25 KB above** its desktop twin because
the allocator is simply different, and the `dynamic_cast` that compiled on macOS
and failed on the first firmware build.

### Running it

It needs `/.reader/wallabag.json` filled in and one saved Wi-Fi network marked
`AUTO`; without either it says so and does nothing. It runs at the END of
`setup()`, after the first paint — which is the measurement rather than a
convenience, because what #140 asks is what a round trip costs with **no book
open**, the state the sync flow actually runs in.

**AND THAT PLACEMENT IS WHY `make firmware` THEN `pio device monitor` CAPTURES
NOTHING.** `HWCDC::write` short-circuits on `!isCDC_Connected()`, so a line
printed before a terminal has OPENED the port is **dropped rather than
buffered** — and `setup()`'s own wait for a host is capped at 400 ms because
every boot pays it. By the time a monitor started by hand attaches, the probe has
already run and its output is gone. The first version of this section said to do
exactly that; it cannot work.

Two routes that do.

**THE CARD, WHICH ALWAYS WORKS.** Set `"logToCard": true` in
`/.reader/settings.json`, flash, let the device sit for a few seconds, then read
`/encre.log` off the card on a computer. Nothing is racing: `logf` tees into the
card buffer whether or not a host is there, and `loop()` flushes it in the first
quiet window. This is the route for a device on battery, where there is no host
coming at all — the case the card log was built for.

**THE CABLE, IF YOU WANT IT LIVE.** The probe WAITS for a terminal, up to 30 s,
whenever `isPlugged()` says a host is there — so start the monitor and the probe
will be waiting for you. **One command:**

```
make probe
```

**IT IS A MAKE TARGET BECAUSE THE TWO-COMMAND FORM SILENTLY FLASHES THE WRONG
BUILD, AND THAT IS WHAT WENT WRONG TWICE.** `PLATFORMIO_BUILD_FLAGS` is an
ENVIRONMENT VARIABLE, so it applies only to the command it is written on — and
`pio run -t upload` REBUILDS before it uploads. So this:

```
PLATFORMIO_BUILD_FLAGS="-DENCRE_WALLABAG_PROBE=1" make firmware   # builds WITH the probe
pio run -e xteink -t upload -t monitor                            # rebuilds WITHOUT it, uploads that
```

builds the probe, throws it away, and flashes the default firmware, with nothing
anywhere saying so. Proved rather than reasoned: the ELF's own probe banner goes
from present to absent between those two commands.

`make probe-build` is the same build without uploading, for checking it compiles.
Unplugged the probe does not wait at all, because nobody is coming.

**IT SAYS WHICH BUILD IS RUNNING BEFORE IT SAYS ANYTHING ELSE.** The first line
is `[probe] ENCRE_WALLABAG_PROBE build` with the heap, and the second says
whether the card log is on. Without them, a build flashed WITHOUT the flag and a
probe that returned early because nothing was configured look identical:
silence. If neither line appears, the running firmware is not a probe build.

Three `[probe]` lines come back, each with the heap before, after, spent, the
minimum since boot, and the largest free BLOCK — which is the number that decides
an allocation and which `getFreeHeap` cannot see:

| line | what it measures |
|---|---|
| `http-info` | a plain round trip to the reader's own server |
| `tls-info` | the same against `app.wallabag.it`, so the handshake is priced even on a device whose own server is plain |
| `download` | one article streamed 4 KB at a time onto the card |

The download reopens the file per chunk, because `SdMan` is the SDK's singleton
and `sd_fs.h` does not export it — so the probe's **wall clock is pessimistic**
and its **heap**, which is the number wanted, is not.

### The decision rule, written before the numbers arrive

**If TLS leaves less than 40 KB free with the radio up and no book open**, the
transport supports **plain HTTP only** in this release, and the account screen's
band says so as a stated limit rather than a device that fails on some servers
and not others. **If it fits**, TLS is enabled and nothing else changes.

40 KB is not a round number chosen for looking like one: it is the 28,508-byte
reading floor plus room for the largest single allocation the open path makes on
a normal book, and it is the point below which a sync would be trading a reader's
ability to open the article it just fetched.

### What the probe costs, measured

`-DENCRE_WALLABAG_PROBE=1` is **+1,880 bytes of static RAM and +154 KB of flash**
against the default build (46,652 → 48,532 and 2,241,403 → 2,395,467), almost all
of it the TLS stack being linked at all. The default build is byte-identical with
the flag absent, which is the whole point of `ENCRE_FS_SELFTEST`'s idiom: a
diagnostic that ships in every build is one every reader pays for.

**Remove the probe once it has answered**, as `ENCRE_COVER_PROBE` was removed
after it answered its own question.

