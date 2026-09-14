# Instapaper Full API — what the credentials cover

> **SUPERSEDED 2026-09-14: Encre uses wallabag instead.** See
> `docs/notes/wallabag-api.md`. Every finding below is still true of Instapaper
> and none of it is ours to care about any more — the Instaparser key, the
> fleet-wide credit ceiling, the xAuth signing and §7's ten open questions all go
> with it. **Two things in here outlived the decision and are recorded there
> rather than lost**: §6's HTML tokenizer was filed as fallback-only and is
> baseline for any HTML-bearing backend (`document.cpp` parses through `xml.h`,
> which is strict XML), and §6's keyboard press counts are what say the six-button
> keyboard cannot carry a 51-character `client_id`. Kept whole rather than
> trimmed: this is the investigation that produced the decision, and a note edited
> down to its conclusion cannot be checked.

Issue #115. Investigated 2026-09-11 against Instapaper's live developer
documentation at `https://www.instapaper.com/developers`, its API Terms of Use,
its Premium pages, the Internet Archive's copies of the previous docs, and
`instaparser.com`. **Credentials are self-serve now — the request form is gone —
but the issue's premise is wrong in a way that changes the design: xAuth does
NOT let a browser page do the exchange, because the exchange is an OAuth 1.0a
signed request and signing needs the consumer secret. Something we control and
that holds the secret must perform it.**

**And that changed the design the same day.** Only something holding the secret
can perform the exchange, so it is the device — which the boards already assumed.
What the boards also assumed, and what turned out to be buying nothing, is the
*route the password takes to get there*: a page the device serves, on the
strength of `InstapaperConnect.dc.html`'s `PASSWORD STAYS IN THE BROWSER.` With
that sentence false, the web page's whole argument goes with it, and **V1.1 signs
in on the Wi-Fi keyboard instead** — which is not a new mechanism, and not even a
new promise, because joining Wi-Fi already types a password on it. §6 has the
whole of it.

Two further findings outrank the credentials question:

- **`bookmarks/get_text` now requires a separate, paid third-party API key**
  (Instaparser) for any use beyond the developer's own account. It is not part
  of the Instapaper credentials at all. The free tier is **1,000 articles per
  month across all users of the app**.
- **The docs were rewritten since this issue was filed.** Every URL the issue
  and the wider internet point at (`/api`, `/api/full`,
  `/main/request_oauth_consumer_token`) now redirects somewhere else, and the
  content differs in substance, not just wording.

**Nothing here is blocked on a lead time any more.** Registration is immediate;
what needs a human, and carries no stated turnaround, is the review that takes an
application out of Owner Only — and that gates *shipping*, not *building*.

No Instapaper account was created, no form submitted, no email sent, and no API
call made. Everything below is read from published pages.

---

## 1. Getting Full API credentials, as of 2026-09-11

**It is self-serve. The "fill this out" request form no longer exists.**

`https://www.instapaper.com/api` now 302s to `https://www.instapaper.com/developers`,
and `https://www.instapaper.com/api/full` to
`https://www.instapaper.com/developers/v1/full-api`. The old request form at
`/main/request_oauth_consumer_token` — which the archived docs linked as "fill
this out" ([snapshot 2023-12-05](https://web.archive.org/web/20231205051257/https://www.instapaper.com/api/full))
— now redirects to the new registration page.

### Where

`https://www.instapaper.com/developers/applications/create`

It requires being signed in to an ordinary Instapaper account. There is no
separate developer account and no application fee.

### What it asks for

Four fields plus a checkbox. Read from the page's own published source map
(`/js/ApplicationsShared-BK_RpB1y.js.map`, component `ApplicationFormFields`),
since the rendered form is behind the sign-in gate:

| Field | Type |
|---|---|
| Title | single line |
| Description | multi-line |
| Application URL | single line |
| Admin contact email | email |

Plus: *"I have read and accept the API Terms of Use."*

Prepare those four values before starting. Nothing asks for a redirect URI, a
platform, screenshots, or expected volume.

### What happens next

From `https://www.instapaper.com/developers/v1/full-api/authentication`:

> New applications start in Owner Only mode; submit one for review to request
> approval, which a human grants before activation.

So there are two stages, and registration alone is enough to start building:

1. **Owner Only** — granted immediately on registration. An application in this
   state carries a consumer key and secret.
2. **Submitted → Approved** — requested with a "submit for review" action on the
   app's edit page, granted by a human.

The four statuses the dashboard renders are **Owner Only, Submitted, Approved,
Suspended** (`ApplicationStatusBadge`, same source map). Error code `1042` is
*"Application is suspended"*.

**The consumer secret is shown once.** The application model's own comment says
so:

> The consumer_secret is only present on creation and on reads while it is still
> unacknowledged — it is a one-time reveal.
> — `/js/ApplicationsShared-BK_RpB1y.js.map`, `developerApplications.ts`

Record it at registration time.

**No turnaround is stated anywhere.** The old docs said only *"All token requests
are reviewed by a human before being activated"* (archived 2023-12-05); the new
docs say only that a human grants approval. Neither gives a number.

### Claiming an existing application

`https://www.instapaper.com/developers/applications/claim` links a pre-existing
consumer key to an account, and asks for **Consumer key** and **Claim token**.
Encre has neither, so this is not our path — noted only so it is not mistaken
for the registration flow.

## 2. Does any endpoint we need require a paid subscription?

**Instapaper Premium: no. But `get_text` has a different paid dependency, and
that one is real.**

### Instapaper Premium is not required for API access

The comparison table on Instapaper's own pricing page lists **"API integration
for third-party apps"** with a checkmark in **both** the Free and the Premium
column (`https://www.instapaper.com/premium`, read from the served HTML).

That matches the first-party announcement that made it so, 2015-06-17:

> we're opening Instapaper's Full API for all users rather than just Premium
> subscribers, which has traditionally been the case
> — [blog.instapaper.com/post/121774203371](https://blog.instapaper.com/post/121774203371)

It is a genuine reversal, and worth knowing because the old state is still
findable: the 2012 docs carried a section headed "Subscription accounts" saying
*"Most of the Full API's methods require the authenticating user to have a
Subscription account"*, with only `oauth/access_token`, `account/verify_credentials`,
`bookmarks/add` and `folders/list` exempt ([snapshot 2012-06-16](https://web.archive.org/web/20120616055910/http://www.instapaper.com/api/full)).
**Under that rule three of our four endpoints would have needed a subscription.**
That section is absent from both the 2023 docs and the current ones.

Premium today is **$5.99/month or $59.99/year** and its listed features are full-text
search, PDF reader, permanent archive, unlimited notes, X saves, Kindle digests,
playlists, speed reading and no ads (`https://www.instapaper.com/docs/premium/overview`).
None of those is an endpoint we call.

Error code **`1041: Premium account required`** still exists in the current error
table (`/developers/v1/full-api/responses`). **The docs never say which endpoints
can return it.** They are silent, and that silence is recorded in §7 rather than
guessed at. What is documented nearby: `1220` fires for domains whose content
needs *"a login or Instapaper Premium"* to crawl — that is about `bookmarks/add`,
not about reading back what is already saved — and highlights are capped for
non-subscribers (the 2023 docs said *"Non-subscribers are limited to 5 highlights
per month"*; the Premium page sells "Unlimited Notes" against a "5-note monthly
limit"). Neither touches list / get_text / archive / star.

### `get_text` requires an Instaparser API key — this is the blocker

From `https://www.instapaper.com/developers/v1/full-api/bookmark-api`:

> This endpoint requires an Instaparser API key, which has a free tier. It can be
> used without a key for personal use, when the application developer is the
> authenticated user.

and on the `instaparser_api_key` parameter:

> Supplying a key is the only supported way to use this endpoint for non-personal
> use; a non-personal request without a key returns 1044.

The matching error codes are new since the 2023 docs, which had none of them:

| Code | Meaning |
|---|---|
| 1044 | Application is not authorized for non-personal content access; an Instaparser API key is required. |
| 1045 | Invalid or unauthorized Instaparser API key. |
| 1046 | Instaparser free credit limit exceeded. |
| 1047 | Instaparser rate-limit exceeded. |

Instaparser is a live commercial product by the same company —
`instaparser.com`, footer *"© 2026 Instapaper Holdings, Inc."*, *"Instaparser is
built by the team behind Instapaper."* Its published tiers:

| Tier | Price | Credits/month | Rate limit | Overage |
|---|---|--:|---|---|
| Trial | $0 forever | 1,000 | 1 req/sec | **No** |
| Beta | $150/mo | 100,000 | 10 req/sec | $0.0012 |
| Live | $500/mo | 500,000 | 25 req/sec | $0.0011 |
| Scale | $900/mo | 1,000,000 | 50 req/sec | $0.001 |

The free tier's **"Overage: No"** is the column that matters. Running out is not a
bill, it is `1046` and a fleet that stops fetching article text until the month
rolls over.

> Each successful Article API call uses 1 credit.
> — `https://www.instaparser.com/docs/1/article_api`

**The key belongs to the application, not to the user**, so the free tier's 1,000
credits per month is a ceiling on the *whole fleet*: every article every Encre
user opens spends from one pool, and 1 req/sec is the aggregate rate. The step
above free is $150/month. The other three endpoints we need — `bookmarks/list`,
`bookmarks/archive`, `bookmarks/star` — carry no such requirement and are plain
Full API calls.

The personal-use exemption lines up exactly with Owner Only mode: an unapproved
app used by its own developer can call `get_text` with no key. That is enough to
build and test the whole feature, and not enough to ship it.

## 3. The auth flow, and whether a browser can do it

### It is OAuth 1.0a with xAuth, and the parameters are as the issue states

From `https://www.instapaper.com/developers/v1/full-api/authentication`:

- `POST https://www.instapaper.com/api/1/oauth/access_token`
- `x_auth_username` (required), `x_auth_password` (required, *"if the account has
  one"*), `x_auth_mode` (required, must be `client_auth`)
- Response is one line, **not JSON**: `oauth_token=aabbccdd&oauth_token_secret=efgh1234`
- Tokens and secrets are up to 50 characters, case-sensitive, letters and digits
  only

One thing to design for that is easy to miss: **Instapaper accounts need not have
a password at all.** *"Passwords are not required, and many users do not have
one… you cannot treat an empty password as a user error"*, and *"If an account
does not have a password, any password value works"*
(`/developers/overview/accounts-and-conventions`).

### Does the exchange need the consumer secret? Yes.

**This is the answer that decides the issue's design, so here is the whole chain
of evidence rather than a conclusion.**

Current docs, same page:

> Instapaper's API uses an OAuth 1.0a implementation, which requires signatures
> on requests. We recommend using an OAuth 1.0a library to sign requests.

> Only the HMAC-SHA1 signature method is supported.

> OAuth parameters go in the `Authorization:` header.

The current page does not add "including this one" to the access-token section.
The **archived 2012 docs say it explicitly**, in the paragraph that introduces
xAuth:

> Instapaper's OAuth implementation is different from what you may be accustomed
> to: there is no request-token/authorize workflow. This makes it much simpler.
> Instead, Instapaper uses an implementation of xAuth very similar to Twitter's.
> **So you still need to sign your requests, but getting tokens is simple.**
> — [snapshot 2012-06-16](https://web.archive.org/web/20120616055910/http://www.instapaper.com/api/full)

"You still need to sign your requests" is stated *about the token-getting flow*,
as the concession that survives dropping the authorize step. Two structural facts
agree with it:

- **HMAC-SHA1 signing has no consumer-secret-free form.** The OAuth 1.0a signing
  key is `consumer_secret & token_secret`; at token-acquisition time the token
  secret is empty and the consumer secret is the entire key. A request signed
  without it is not signed.
- **The server must be able to tell which application is calling.** Applications
  have per-app state — Owner Only, Approved, Suspended — and `1042: Application is
  suspended` is a documented response. OAuth 1.0a conveys app identity as
  `oauth_consumer_key` *plus a signature proving possession of the secret*; an
  unsigned call would carry a consumer key anyone could copy.

**So the issue's premise — "xAuth is what lets the browser page exchange a
username and password for a token without us holding the password" — is FALSE**,
in its load-bearing half. xAuth does remove the *redirect*, which is presumably
what made it look like a browser-side flow. It does not remove the *signature*.
A browser page can only do the exchange by shipping the consumer secret to the
browser, and the Terms make that a revocation trigger:

> Instapaper reserves the right to deny and/or revoke an application's API access
> for any reason. Such reasons may include that the app's keys have been
> compromised, **published, or shared**
> — `/developers/overview/api-terms`

The same applies to embedding the secret in firmware, which is equally
extractable. Whatever performs the exchange has to be something that can keep a
secret. Note that this does not make the password problem worse than the issue
assumed — the Terms require the password to be discarded either way:

> Apps must not store users' passwords. Passwords may only be collected for
> xAuth token acquisition and must be discarded afterward. Apps must make
> reasonable efforts to prevent passwords from being compromised, and must not
> disclose passwords to any other services or individuals.

That last clause is worth reading before designing the exchange, because a
browser page posting the password to a helper we run is a question it plainly
bears on and does not plainly answer.

### There is no three-legged flow

Confirmed, not inferred, and from both eras:

> xAuth is the only way to get an Instapaper access token.
> — current docs, `/developers/v1/full-api/authentication`

> there is no request-token/authorize workflow
> — archived 2012 docs

There is no authorize URL, no redirect URI field on the registration form, and no
`request_token` endpoint in the current Account API (which lists exactly two
methods: `oauth/access_token` and `account/verify_credentials`). **Collecting the
user's actual password is the only option Instapaper offers.**

### Rate limits

**Not published.** `1040: Rate-limit exceeded` is in the error table, in both the
current and the 2023 docs. Neither states a request-per-period figure, a window,
a per-app-versus-per-user distinction, or any `X-RateLimit` response header. The
only published numbers are Instaparser's (§2), and those govern `get_text` only.

## 4. Other things worth knowing before planning

- **HTTPS is mandatory.** *"HTTPS is required on all endpoints"*, for both APIs
  (`/developers/overview/introduction`). Relevant to a firmware target: there is
  no plaintext fallback. HTTP was deprecated 2014-04-01 and dropped 2014-06-01.
- **Everything is POST with parameters in the body**, not the query string, and
  OAuth parameters go in the `Authorization` header rather than the body or the
  query string.
- **UTF-8 throughout**, in both directions.
- **`get_text` returns HTML**, not plain text: *"the bookmark's processed
  text-view HTML, always `text/html` encoded as UTF-8"*, with HTTP 200, *"not the
  standard array"* — or HTTP 400 plus a normal error object. **No size bound is
  documented.** For a device with a 42 KB heap floor that is the number to
  measure rather than to look up.
- **`bookmarks/list` is one POST for the whole screen.** `limit` is 1–500,
  default 25; `folder_id` accepts `unread` (default), `starred`, `archive` or a
  numeric folder. It returns a JSON *object* (`user`, `bookmarks`, `highlights`,
  `delete_ids`), not the standard array. The `have` parameter takes
  `id:hash:progress:timestamp` tuples and both suppresses unchanged bookmarks and
  reports reading progress in the same call — a good fit for a device that syncs
  rarely.
- **Store `user_id`, not `username`.** Usernames change; the docs say so twice.
- **Non-JSON means 503.** *"If a response is not valid JSON, interpret it as an
  HTTP 503 … and retry the request later."*
- **`archive` is not `delete`.** Both exist; `delete` is permanent and the docs
  ask that apps be explicit with users about it. We need `archive`.
- **Naming.** Apps may not use Instapaper's name or logo in their own title or
  logo, but *"apps may describe themselves as 'for Instapaper'"*. So "Encre" is
  fine and "Encre for Instapaper" is fine.
- **Terms clauses that bind an implementation**, beyond the password and key rules
  already quoted: no adding bookmarks the user did not explicitly ask for; no
  circumventing Premium requirements; no working around `1221` opt-out domains;
  no scraping the website for anything the API does not expose; no destroying or
  altering user data without an explicit user action.
- **The API is "as is"**, with all warranties disclaimed and *"zero aggregate
  liability"*. There is no SLA and no deprecation notice period. The docs
  rewrite found here is itself the evidence for what that means in practice.
- **Everything is version 1** (`/api/1/...`). The 2023 docs referenced `/api/1.1/`
  for highlights and some folder methods; the current docs use `/api/1/`
  throughout and describe `qline` as deprecated as of "version 1.1". The
  versioning is not coherent enough to rely on.

## 5. What changed since this issue was written

Recorded because anyone re-checking this will hit the old URLs first, and because
two of these change the plan rather than the prose.

| | Then (archived 2023-12-05 and 2012-06-16) | Now (2026-09-11) |
|---|---|---|
| Docs URL | `/api`, `/api/full` | `/developers`, `/developers/v1/full-api` |
| Getting a key | a form, `/main/request_oauth_consumer_token`, human-reviewed before activation | self-serve registration, immediate Owner Only key, review only to go beyond it |
| `get_text` | `bookmark_id` only | **also an Instaparser API key for non-personal use** |
| Premium and the API | 2012: most Full API methods needed a Subscription. 2015: opened to all users | not required; listed as a Free feature |
| Error codes | no 1044–1047 | 1044–1047, all Instaparser |
| Tags | absent | `tags` on bookmarks, `tag` on list |

## 6. What this means for Encre

### The accurate sentence existed here, and was deleted

`ad0e70e` (2026-08-20) wrote the flow into the spec, and got it right:

> **OAuth 1.0a xAuth signing (HMAC-SHA1)** […] xAuth login form via the web setup
> page — never typed on-device […] username and password never touch the device
> **UI**; the device performs xAuth, stores only the resulting token in NVS

`c2468df` ("Cut Instapaper from V1") deleted every line of that with the rest of
the V1 scope. Nothing in the repo carries the precise claim any more, and the
five things that replaced it are looser in the same direction:

| site | says | |
|---|---|---|
| `design/ArticlesSetup.dc.html:42` | `SIGN-IN HAPPENS IN YOUR BROWSER — NOTHING TO TYPE ON THE DEVICE.` | matches the spec |
| `design/InstapaperConnect.dc.html:63` | `PASSWORD STAYS IN THE BROWSER.` | stronger |
| #110 | "the alternative […] puts the credential through our firmware" (implying this one does not) | stronger |
| #111 | "the sign-in happens in the browser so the password never reaches the firmware" | stronger |
| #115 | "xAuth is what lets the browser page exchange a username and password for a token without us holding the password" | stronger |

Four of the five are stronger than the sentence they came from, and the one that
matches it is the only one that says nothing about where the password goes. The
repo's own precedent is one board over: `design/WebSetup.dc.html` already posts a
**Wi-Fi password** to a page the device serves, and claims nothing about where it
stays.

### The exchange is the device's, and the setup page was buying nothing

Half the design survives unchanged: only something that holds the consumer secret
can perform the exchange, so the device performs it, keeps the token in NVS and
discards the password — which is what the boards assumed and what spec `ad0e70e`
said.

**The other half does not.** `PASSWORD STAYS IN THE BROWSER.` is false — the
password leaves the browser the moment the form is submitted, and the device is
what it is submitted *to*. That is the same false-claim shape this project
already refuses for an unread gauge (`-1`, never `0%`) and for a badge promising
a wake charging cannot deliver, and it is on a board, so it reaches glass.

**And it was the whole argument for the setup page.** A page the device serves
exists here to keep the credential off the six-button keyboard; with the safety
claim gone, what remains is convenience, and it is bought with an HTTP server in
firmware (#110), a plain-HTTP hop across the LAN, and static RAM beside a TLS
client whose heap #112 already flags as unmeasured. **So V1.1 signs in on the
keyboard**, and the four facts that decide it are below.

### Why the keyboard wins, and what it costs

**1. The device already has one, shipped and on glass.** `WifiPasswordScreen`
(`04cd890`, V1.1) is a 46-cell `GridFocus` grid over three layers whose union is
all 95 printable ASCII, with a caret, a one-shot shift, a latching `#+=` and a
held Back to leave. The HTTP server is unwritten.

**2. The promise was already broken one screen earlier.** In V1.1 the only route
onto Wi-Fi is `WifiPassword.dc.html` — `SetupHotspot.dc.html`, the no-typing
alternative, is parked in V2 — so a user reaching Instapaper setup **has already
typed a password on this keyboard**. "Nothing to type on the device" is not a
promise this device keeps.

**3. Sign-in is #110's only V1.1 consumer.** Its own body parks the upload page,
the streaming write, `Transfer`, `WebUpload` and the AP in V2. Take sign-in away
and the card has nothing left to serve in V1.1.

**4. The cost is one-time and measured rather than argued.** Simulated over the
shipped layout — both grid axes wrap, and the screen declares no auto-repeat, so
every move is a discrete press and a ~520 ms repaint:

| | presses | waveform |
|---|--:|--:|
| `lucas@example.com` | 92 | ~48 s |
| a 12-character mixed password | 56 | ~29 s |
| **both fields** | **~150** | **~78 s** |

A floor: no typos, no backtracking, optimal navigation. Against it, the web flow
still needs the WPA2 passphrase typed on the same keyboard first, plus a second
device and a URL typed into that.

**What has to change, and it is not the keyboard.** `WifiPasswordScreen` is
Wi-Fi-shaped — an SSID in the band, a `JOIN` cell, `Action::wifi()`, and 802.11's
own 8-to-63 bounds — so this is a text-entry extraction, which is *"the second
copy is the extraction point"* arriving on schedule rather than five copies late.
Three traps in it:

- **`kMinPassphrase = 8` must not come along.** *"Passwords are not required, and
  many users do not have one… you cannot treat an empty password as a user
  error."* That floor blanks the Confirm slot, which is this firmware's
  vocabulary for a button with no action — a dead button on a legal state.
- **Two fields, not one**, and the docs bind their labels: *"Email or username"*,
  never "Email" alone, because many usernames are not addresses; and for the
  password, *"clarify that it's only required if the user actually has one"* —
  Instapaper's own form says `Password, if you have one.`
- **`SHOWN WHILE TYPING`** is right for a passphrase you own and a different
  question for an account password on a train. `WifiPasswordViewModel::visibility`
  is already a string, so a masked mode is cheap; whether to have one is a design
  call, not a derivation.

### Whatever holds the consumer secret can be extracted

There is no fourth option, and each of the three costs something:

- **The firmware holds it.** Extractable from flash by anyone with the device, and
  the Terms name keys "compromised, **published, or shared**" as revocation
  grounds. Encre is open source, so the secret is in the repository as well as in
  the flash unless it is injected at build time. Whether Instapaper treats a
  shipped-in-the-binary key as "published" is §7.9 and has not been asked.
- **A helper service we run holds it.** Keeps the secret, and sends the user's
  Instapaper password to a third party — which the Terms bear on directly: *"must
  not disclose passwords to any other services or individuals."* It also gives a
  card-transfer-only e-reader a server to keep running.
- **Owner Only mode.** No secret to protect and no third party, because there is
  nobody but the developer. Enough to build and test the whole feature; not a
  release.

### The setup page would have put the password on the LAN in the clear

`InstapaperConnect.dc.html` shows `http://192.168.1.42/setup` and
`SetupHotspot.dc.html` shows `http://192.168.4.1` — plain HTTP, which is the only
practical choice for a device with no certificate anyone's phone will trust. So
that flow sends the user's Instapaper password across the local network
unencrypted, against a Terms clause asking for *"reasonable efforts to prevent
passwords from being compromised"*.

It is recorded here rather than dropped with the decision, for two reasons. It is
the sharpest form of the point above — the page was **worse** than the keyboard
on the axis it was chosen for, not merely no better — and **it comes back with the
upload page**, which is still V2 and still posts to a device-served form over the
same plain HTTP. Nothing typed into `WebSetup.dc.html` is an account credential
today; the day one is, this is the finding.

### What it changes, card by card

- **#114 (reading an article) is the card that moves.** `get_text` is not covered
  by the credentials at all — it needs an Instaparser key, whose free tier is
  1,000 articles per month **across every Encre user**, with no overage. That is
  a fleet-wide ceiling on the one endpoint that supplies article text, and the
  step above it is $150/month. The question for the owner is §7.5: whether Encre
  fetches and parses article HTML itself and uses Instapaper only for the list
  and the state changes.
- **#112, #111, #113 are unaffected by that.** `bookmarks/list`,
  `bookmarks/archive` and `bookmarks/star` are plain Full API calls with no second
  key and no Premium requirement.
- **#110 (the HTTP server) loses its only V1.1 consumer and is closed as
  superseded.** Its scope was never wrong; its *reason* was, and the reason was
  the sentence on the board. Parking it at V2 was the first answer and left a
  server with nothing to serve, beside the draft it had been split out of — so
  the split is reversed instead, and `HTTP upload server and Transfer screen`
  carries its scope again. That draft's body had said outright *"The HTTP server
  itself is NOT in this card any more"*, which is why closing #110 alone would
  have left the server homeless in a way only the two bodies together showed.
- **#111 becomes a keyboard flow**, and `InstapaperConnect.dc.html` — the panel
  that shows a URL and waits — has nothing left to draw.
  `ArticlesSetup.dc.html`'s `SIGN-IN HAPPENS IN YOUR BROWSER` goes with it.
- **An OAuth 1.0a HMAC-SHA1 signer is a card nobody has filed.** It is not one
  screen's: the exchange needs it and so does every Full API call after it, so it
  belongs to the client module, beside the injected HTTP transport spec §8
  already describes. Instapaper's own advice is to use a library. On a target
  with no exceptions and a 42 KB reading floor, sizing that is planning work, not
  a line of code.
- **Nothing is blocked on a form any more.** Registration is self-serve and the
  key is immediate, so #111 and #112 can be unblocked from #115 as soon as an
  application exists. What still needs a human and has no stated turnaround is
  the review that takes an app out of Owner Only — which gates *shipping*, not
  *building*.

### If `get_text` is unaffordable, what replaces it — and what that costs

Sized 2026-09-11 against the tree, because "Encre parses the article itself" is
the fallback §7.5 names and it was a sentence rather than an estimate.

**Most of the pipeline already exists.** The 252-name HTML entity table
(`tools/entities.py`) is generated and tested; `document` / `layout` / `chapter`
take blocks to glyphs unchanged, because an article *is* a chapter;
`inflate_stream.h` already decodes DEFLATE, which is what sits behind HTTP's
`Content-Encoding: gzip`; `openRead` is random-access, built for a zip's central
directory and the thing that makes a two-pass approach possible at all; and
`css.h`'s `collectItalicClasses` — a forward scan answering one question with no
tree and no cascade — is the right *shape* for the heuristic.

**Three things do not exist, and one of them is architectural.**

1. **An HTML tokenizer.** `xml.h` states the gap itself: *"EPUB content is
   well-formed XML by specification, so none of the tag-soup recovery that makes
   an HTML parser large is needed here."* Void elements, implied end tags, raw
   text elements where `<` is not markup, unquoted attributes, bogus comments.
   `xml.cpp` is 502 lines strict; **900–1,400** for tag soup.
2. **A TREE. `Document` is `std::vector<Block>` and `BlockKind` has four
   values.** Readability scores a node from its CHILDREN — text density, link
   density, comma counts — so it cannot know which subtree is the article until
   it has seen them all. **That inverts this project's whole memory model**,
   which is stream-and-never-hold.
3. **The scoring pass**, ~400–600 lines, between `css.cpp` (224) and `toc.cpp`
   (242) in size.

**The shape that fits is one this repo already uses twice: the card is the
scratch space.** Fetch, write the raw HTML under `/.reader/`, then **pass one**
builds a compact CANDIDATE TABLE rather than a DOM — per block-level element,
`(offset, depth, tag, textBytes, linkBytes, commas)`, on the order of 16 bytes
each — and **pass two** seeks to the winner and streams it through the existing
`BlockReader`. That is the zip EOCD scan and the page-index-of-cursors reused,
and it keeps the memory model rather than arguing with it.

**So: roughly 2,000–2,500 new lines in `core/`, against a reader stack that is
~4,700 across nine layers** — a 40–50% increase in the portable layer for one
feature, which is the same order as the entire EPUB pipeline. Shell-side HTTP is
on top, though #112 owns the TLS heap question either way; arbitrary domains is
the harder version of it, since a certificate chain for a host nobody has seen
before is not the same problem as one pinned endpoint.

**AND THE PART THAT IS NOT A LINE COUNT IS QUALITY.** Instaparser sells *"15
years of parsing refinement… refined across billions of articles since 2008"*. A
first-pass extractor gets most pages right and **fails silently** — the nav
sidebar instead of the article — which reads as a broken device rather than a
broken parser, and is the false-claim shape this file refuses everywhere else.

**DO NOT BUILD IT TO AVOID A COST NOBODY HAS CONFIRMED.** §7.5 and §7.10 are
free to ask and one email; this estimate exists so the answer can be judged
against a number rather than a feeling.

## 7. What is still unknown

Everything here is either absent from the docs or behind a sign-in, and needs to
be asked of Instapaper (`support@instapaper.com`) or established by trying it.

1. **How long does review take, and what are the criteria?** No turnaround is
   stated, and nothing says what a reviewer looks for. This is the schedule risk
   for shipping.
2. **What exactly does Owner Only mode permit?** The phrase is never defined. The
   strong implication — from `get_text`'s "personal use, when the application
   developer is the authenticated user" — is that only the registering account
   can authenticate. Whether a second account can obtain a token from an
   unapproved app is not stated, and it decides whether we can beta-test with
   anyone but ourselves.
3. **Which endpoints can actually return `1041: Premium account required`?** The
   error exists; no current page maps it to a method. The 2012 list is obsolete
   and the pricing page implies none of ours. Unresolved, and worth a direct
   question, because a Premium requirement on `bookmarks/list` would end the
   feature.
4. **Does one `get_text` call consume exactly one Instaparser credit?** Never
   stated. Instaparser's own Article API doc says 1 credit per call and error
   `1046` is *"Instaparser free credit limit exceeded"*, so the pool is plainly
   shared — but the mapping is an inference, and the free tier's viability turns
   on it. Related and equally unstated: whether Instapaper caches the text so a
   re-read of the same article costs a second credit, and whether Instaparser's
   1 req/sec free-tier limit applies through Instapaper's endpoint (error `1047`
   suggests it does).
5. **Is there any route to `get_text` at scale that is not $150/month?** No page
   discusses non-commercial, open-source or low-volume terms. **Ask §7.10 first**
   — a key the reader supplies is a far cheaper answer than either of the others.
   If both come back no, the alternative is to build Encre's reader on the
   article text it can fetch itself and use Instapaper only for the list and the
   state changes, which is **sized in §6** rather than left as a sentence:
   ~2,000–2,500 lines of `core/`, and a tree where this project holds none.
6. **The published rate limits.** None, for any Instapaper endpoint. Ask for the
   figure and the window before designing a sync cadence.
7. **Is `bookmarks/get_text` output bounded?** No maximum size is documented. It
   matters here more than it would elsewhere.
8. **One-line confirmation that `/oauth/access_token` must be OAuth-signed.**
   §3 establishes this from an archived first-party sentence plus the structure
   of HMAC-SHA1 and of per-app suspension, and the conclusion is not in real
   doubt — but no current page says it in one sentence. It is settled in a minute
   once a key exists, by sending the xAuth call unsigned and reading the
   rejection; that test was not run here because it needs credentials.
9. **Where Instapaper expects the consumer secret to live, for an open-source
   device.** Two halves of one question, and the Terms answer neither: (a) is a
   secret compiled into a public firmware "published, or shared"? (b) is a helper
   service we run, holding the secret and receiving the user's password, a
   disclosure "to any other services"? Every flow on offer needs one party to
   hold both the secret and the password, so one of these has to be acceptable.
   Worth asking explicitly rather than assuming the reading that suits us.
10. **MAY EACH USER SUPPLY THEIR OWN INSTAPARSER KEY?** `instaparser_api_key` is
    a REQUEST parameter rather than a build-time constant, and Instaparser's free
    tier is *"$0 forever"* per account — so a key entered by the reader makes the
    1,000-a-month ceiling PER READER instead of fleet-wide, and Encre never pays.
    The docs say a key is required for non-personal use and **never say whose**.
    This is the cheapest answer to §7.5 by a wide margin and the one to ask first;
    it does not address the dependency itself, only the cost. Related and also
    unstated: whether the reader's own Instapaper account then counts as
    "personal use" for their own key.
11. **Whether the old docs' `jsonp` parameter still works.** Dropped from the
    current docs without a deprecation note. Irrelevant if the exchange is
    server-side, and the only thing that would have made a browser-side read path
    possible at all, so it is listed for completeness.

---

**Verified:** no source file changed by this investigation; this note is the only
addition. Nothing was submitted, registered or sent. Live pages read 2026-09-11;
archived pages carry their snapshot dates inline. The four quotes the verdict
rests on — `get_text`'s Instaparser requirement, "requires signatures on
requests", "xAuth is the only way to get an Instapaper access token", and the
Terms' password clause — were re-read from the rendered pages a second time,
because the docs are a client-rendered app and a plain fetch returns only the
shell. The Instaparser price table and Premium's free-column checkmark were
re-read the same way. The registration form's four fields are the one thing here
read from the site's published source map rather than from a rendered page, since
that form is behind the sign-in.
