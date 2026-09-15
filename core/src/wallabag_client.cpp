#include "reader/wallabag_client.h"

#include "reader/xml.h"  // BufferSource

namespace reader {
namespace {

// Percent-encode for an `application/x-www-form-urlencoded` body. A client
// secret is random ASCII, but a PASSWORD is whatever the reader typed -- and an
// `&` in one would otherwise end the field and start a new one.
std::string formEncode(std::string_view v) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  out.reserve(v.size());
  for (const unsigned char c : v) {
    const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
    if (safe) {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('%');
      out.push_back(hex[c >> 4]);
      out.push_back(hex[c & 0x0F]);
    }
  }
  return out;
}

// A minimal reader for the one flat field a grant answer needs. The scanner
// rather than json.h, because the answer nests (`{"access_token":...}` is flat
// today and wallabag is entitled to add to it) and because this file already
// has the scanner for the listing.
bool grantField(const std::string& body, const char* want, std::string& out) {
  BufferSource src(body);
  JsonScanner s(src);
  for (;;) {
    const JsonScanner::Token t = s.next();
    if (t == JsonScanner::Token::End || t == JsonScanner::Token::Error) return false;
    if (t != JsonScanner::Token::Key) continue;
    if (s.key() != want) {
      if (!s.skipValue()) return false;
      continue;
    }
    if (s.next() != JsonScanner::Token::String) return false;
    out = s.text();
    return true;
  }
}

bool bodyNamesWallabag(const std::string& body) {
  BufferSource src(body);
  JsonScanner s(src);
  for (;;) {
    const JsonScanner::Token t = s.next();
    if (t == JsonScanner::Token::End || t == JsonScanner::Token::Error) return false;
    if (t == JsonScanner::Token::Key && s.key() == "appname") return true;
  }
}

int digits(std::string_view s, size_t at, size_t n) {
  int v = 0;
  for (size_t i = 0; i < n; ++i) {
    if (at + i >= s.size()) return -1;
    const char c = s[at + i];
    if (c < '0' || c > '9') return -1;
    v = v * 10 + (c - '0');
  }
  return v;
}

}  // namespace

int64_t unixTimeFromIso8601(std::string_view stamp) {
  // `2026-09-03T10:00:00+0000`. The offset is read where it is present; wallabag
  // sends +0000, and a stamp from a differently-configured server must not shift
  // the watermark by hours.
  const int y = digits(stamp, 0, 4);
  const int mo = digits(stamp, 5, 2);
  const int d = digits(stamp, 8, 2);
  const int h = digits(stamp, 11, 2);
  const int mi = digits(stamp, 14, 2);
  const int se = digits(stamp, 17, 2);
  if (y < 0 || mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || mi < 0 || se < 0) return 0;
  if (stamp.size() < 19 || stamp[4] != '-' || stamp[7] != '-' || stamp[10] != 'T') return 0;

  // DAYS FROM CIVIL, a closed form -- no clock, no table, and exact for every
  // date this will ever see. (Howard Hinnant's algorithm, as every C++ date
  // library uses.)
  int64_t yy = y;
  yy -= mo <= 2;
  const int64_t era = (yy >= 0 ? yy : yy - 399) / 400;
  const int64_t yoe = yy - era * 400;
  const int64_t doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const int64_t days = era * 146097 + doe - 719468;

  int64_t t = days * 86400 + h * 3600 + mi * 60 + se;

  // An offset, if one is there: `+HHMM`, `-HH:MM` or `Z`.
  for (size_t i = 19; i < stamp.size(); ++i) {
    const char c = stamp[i];
    if (c == 'Z') break;
    if (c != '+' && c != '-') continue;
    const int oh = digits(stamp, i + 1, 2);
    if (oh < 0) break;
    size_t mAt = i + 3;
    if (mAt < stamp.size() && stamp[mAt] == ':') ++mAt;
    const int om = digits(stamp, mAt, 2);
    const int64_t off = oh * 3600 + (om < 0 ? 0 : om) * 60;
    t += (c == '+') ? -off : off;
    break;
  }
  return t;
}

bool parseListing(ByteSource& src, ListingPage& out) {
  out = ListingPage{};
  JsonScanner s(src);
  // NAMED KEYS ONLY, EVERYTHING ELSE SKIPPED. `tags` is an array of objects and
  // `preview_picture` can be null or a string; skipValue walks either without
  // this function knowing their shape, which is what keeps it from breaking the
  // day wallabag adds a field.
  //
  // DEPTH IS COUNTED so an `id` inside a tag is not read as an article's -- the
  // defect a naive walk has, and one this had to be written around rather than
  // assumed away.
  int depth = 0;
  int itemsAt = -1;   // the depth the items ARRAY sits at
  bool inEmbedded = false;
  ListingEntry cur;
  bool building = false;

  for (;;) {
    const JsonScanner::Token t = s.next();
    switch (t) {
      case JsonScanner::Token::Error:
        return false;
      case JsonScanner::Token::End:
        // A PAGE THAT ENDED MID-STRUCTURE HANDS BACK NOTHING. Partial entries
        // are worse than none: the engine would advance its watermark past
        // articles it never saw.
        return depth == 0;
      case JsonScanner::Token::ObjectStart:
        ++depth;
        if (itemsAt >= 0 && depth == itemsAt + 1) {
          cur = ListingEntry{};
          building = true;
        }
        break;
      case JsonScanner::Token::ObjectEnd:
        if (building && depth == itemsAt + 1) {
          if (cur.id != 0) out.entries.push_back(cur);
          building = false;
        }
        --depth;
        break;
      case JsonScanner::Token::ArrayStart:
        ++depth;
        break;
      case JsonScanner::Token::ArrayEnd:
        if (itemsAt >= 0 && depth == itemsAt) itemsAt = -1;
        --depth;
        break;
      case JsonScanner::Token::Key: {
        const std::string& k = s.key();
        if (!building) {
          if (k == "_embedded") {
            inEmbedded = true;
            break;
          }
          if (inEmbedded && k == "items") {
            // The array opens on the NEXT token; its contents are one deeper.
            itemsAt = depth + 1;
            break;
          }
          if (k == "page" && s.next() == JsonScanner::Token::Number)
            out.page = static_cast<int>(s.number());
          else if (k == "pages" && s.next() == JsonScanner::Token::Number)
            out.pages = static_cast<int>(s.number());
          else if (k == "total" && s.next() == JsonScanner::Token::Number)
            out.total = static_cast<int>(s.number());
          break;
        }
        if (k == "id") {
          if (s.next() != JsonScanner::Token::Number) return false;
          cur.id = static_cast<int>(s.number());
        } else if (k == "title") {
          if (s.next() != JsonScanner::Token::String) return false;
          cur.title = s.text();
          cur.titleTruncated = s.truncated();
        } else if (k == "domain_name") {
          // NULLABLE: wallabag sends null for an entry with no host, and a
          // refusal there would lose the page over a field nothing needs.
          const JsonScanner::Token v = s.next();
          if (v == JsonScanner::Token::String) cur.domain = s.text();
          else if (v != JsonScanner::Token::Null) return false;
        } else if (k == "reading_time") {
          if (s.next() != JsonScanner::Token::Number) return false;
          cur.readingTime = static_cast<int>(s.number());
        } else if (k == "is_archived") {
          const JsonScanner::Token v = s.next();
          // wallabag has sent BOTH a bool and a 0/1 number here across versions.
          if (v == JsonScanner::Token::Bool) cur.archived = s.boolean();
          else if (v == JsonScanner::Token::Number) cur.archived = s.number() != 0;
          else return false;
        } else if (k == "is_starred") {
          const JsonScanner::Token v = s.next();
          if (v == JsonScanner::Token::Bool) cur.starred = s.boolean();
          else if (v == JsonScanner::Token::Number) cur.starred = s.number() != 0;
          else return false;
        } else if (k == "updated_at") {
          if (s.next() != JsonScanner::Token::String) return false;
          cur.updatedAt = s.text();
        } else if (!s.skipValue()) {
          return false;
        }
        break;
      }
      default:
        break;
    }
  }
}

bool WallabagClient::haveToken() {
  if (!tokensLoaded_) {
    tokensLoaded_ = true;
    if (!tokens_.load(access_, refresh_)) {
      access_.clear();
      refresh_.clear();
    }
  }
  return !access_.empty();
}

bool WallabagClient::start(HttpRequest req, BodySink& sink, bool authenticated) {
  if (state_ == WallabagState::Running) return false;
  pending_ = std::move(req);
  sink_ = &sink;
  authenticated_ = authenticated;
  result_ = WallabagResult::None;
  status_ = 0;
  state_ = WallabagState::Running;

  if (authenticated_ && !haveToken()) {
    // NO TOKEN AT ALL: go straight to the password grant rather than sending a
    // call that can only 401. One round trip saved on every first sync.
    beginGrant();
    return true;
  }
  step_ = Step::Call;
  sendPending();
  return true;
}

void WallabagClient::sendPending() {
  HttpRequest r = pending_;
  if (authenticated_) r.headers.push_back({"Authorization", "Bearer " + access_});
  if (!http_.begin(r, *sink_)) {
    result_ = WallabagResult::Unreachable;
    state_ = WallabagState::Done;
    step_ = Step::Idle;
  }
}

void WallabagClient::beginRefresh() {
  step_ = Step::Refreshing;
  grantSink_.reset();
  HttpRequest r;
  r.method = "POST";
  r.path = "/oauth/v2/token";
  r.headers.push_back({"Content-Type", "application/x-www-form-urlencoded"});
  r.body = "grant_type=refresh_token&refresh_token=" + formEncode(refresh_) +
           "&client_id=" + formEncode(creds_.clientId) +
           "&client_secret=" + formEncode(creds_.clientSecret);
  if (!http_.begin(r, grantSink_)) {
    result_ = WallabagResult::Unreachable;
    state_ = WallabagState::Done;
    step_ = Step::Idle;
  }
}

void WallabagClient::beginGrant() {
  step_ = Step::Granting;
  grantSink_.reset();
  HttpRequest r;
  r.method = "POST";
  r.path = "/oauth/v2/token";
  r.headers.push_back({"Content-Type", "application/x-www-form-urlencoded"});
  r.body = "grant_type=password&client_id=" + formEncode(creds_.clientId) +
           "&client_secret=" + formEncode(creds_.clientSecret) +
           "&username=" + formEncode(creds_.username) +
           "&password=" + formEncode(creds_.password);
  if (!http_.begin(r, grantSink_)) {
    result_ = WallabagResult::Unreachable;
    state_ = WallabagState::Done;
    step_ = Step::Idle;
  }
}

void WallabagClient::poll() {
  if (state_ != WallabagState::Running) return;
  http_.poll();
  const HttpState hs = http_.state();
  if (hs == HttpState::Idle || hs == HttpState::Running) return;

  if (hs == HttpState::Failed) {
    result_ = WallabagResult::Unreachable;
    state_ = WallabagState::Done;
    step_ = Step::Idle;
    return;
  }

  status_ = http_.status();
  const int code = status_;

  switch (step_) {
    case Step::Call:
    case Step::Retrying: {
      if (code == 401 && authenticated_ && step_ == Step::Call) {
        // ONE refresh and ONE retry -- and the grant path is taken directly when
        // there is no refresh token to spend.
        if (!refresh_.empty()) {
          beginRefresh();
        } else {
          beginGrant();
        }
        return;
      }
      if (code == 401 && authenticated_) {
        // A 401 on the RETRY, after a fresh token. The server is refusing the
        // account rather than the token.
        tokens_.clear();
        access_.clear();
        refresh_.clear();
        result_ = WallabagResult::CredentialsRefused;
        state_ = WallabagState::Done;
        step_ = Step::Idle;
        return;
      }
      if (checkingInfo_) {
        // NotAWallabag IS ANSWERED FROM THE BODY, not from the status: a proxy,
        // a router's captive portal and a different application all answer 200
        // to `/api/info`, and only the body says which.
        const bool ok = code == 200 && infoSink_ != nullptr &&
                        bodyNamesWallabag(infoSink_->body());
        result_ = ok ? WallabagResult::Ok : WallabagResult::NotAWallabag;
        state_ = WallabagState::Done;
        step_ = Step::Idle;
        checkingInfo_ = false;
        return;
      }
      // EVERY OTHER STATUS IS THE CALLER'S. A 404 on a PATCH acks (the desired
      // state is already true) and a 500 is a failed page the sync reports --
      // neither is this layer's to interpret.
      result_ = WallabagResult::Ok;
      state_ = WallabagState::Done;
      step_ = Step::Idle;
      return;
    }
    case Step::Refreshing: {
      if (code == 401 || code >= 400) {
        // The refresh token is spent. One password grant, which is the second
        // and last rung.
        refresh_.clear();
        beginGrant();
        return;
      }
      std::string a;
      std::string r;
      if (!grantField(grantSink_.body(), "access_token", a)) {
        result_ = WallabagResult::CredentialsRefused;
        state_ = WallabagState::Done;
        step_ = Step::Idle;
        return;
      }
      grantField(grantSink_.body(), "refresh_token", r);
      access_ = a;
      if (!r.empty()) refresh_ = r;
      tokens_.save(access_, refresh_);
      step_ = Step::Retrying;
      sendPending();
      return;
    }
    case Step::Granting: {
      if (code >= 400) {
        // A 401 ON THE PASSWORD GRANT IS THE END OF THE LADDER. The server has
        // judged the file and said no, and nothing the reader can press changes
        // the file -- which is why the dialog offers no retry.
        tokens_.clear();
        access_.clear();
        refresh_.clear();
        result_ = WallabagResult::CredentialsRefused;
        state_ = WallabagState::Done;
        step_ = Step::Idle;
        return;
      }
      std::string a;
      std::string r;
      if (!grantField(grantSink_.body(), "access_token", a)) {
        result_ = WallabagResult::CredentialsRefused;
        state_ = WallabagState::Done;
        step_ = Step::Idle;
        return;
      }
      grantField(grantSink_.body(), "refresh_token", r);
      access_ = a;
      refresh_ = r;
      tokens_.save(access_, refresh_);
      step_ = Step::Retrying;
      sendPending();
      return;
    }
    case Step::Idle:
      return;
  }
}

void WallabagClient::cancel() {
  http_.cancel();
  state_ = WallabagState::Done;
  result_ = WallabagResult::Unreachable;
  step_ = Step::Idle;
}

bool WallabagClient::beginInfo(BodySink& sink) {
  HttpRequest r;
  r.path = "/api/info";
  checkingInfo_ = true;
  infoSink_ = dynamic_cast<BufferSink*>(&sink);
  // NO Authorization HEADER, which is the whole point of this call: reachability
  // is answerable before any credential is used, so "that URL is not a wallabag"
  // is a different message from "those credentials were refused".
  return start(std::move(r), sink, /*authenticated=*/false);
}

bool WallabagClient::beginListing(const std::string& sinceStamp, int page, BodySink& sink) {
  HttpRequest r;
  r.path = "/api/entries?detail=metadata&perPage=" + std::to_string(kPerPage) +
           "&page=" + std::to_string(page);
  if (sinceStamp.empty()) {
    // A FIRST SYNC ASKS FOR THE UNREAD ONLY. There is nothing on the card, so
    // everything archived is irrelevant and fetching it would be the slowest
    // possible way to learn nothing.
    r.path += "&archive=0";
  } else {
    // A LATER SYNC DROPS `archive=0`, AND THAT IS THE LOAD-BEARING HALF. An
    // entry archived on the SERVER has to come back so its local file can be
    // removed -- and `archive=0` would hide exactly those, leaving a reader with
    // articles on the card that their phone archived last week and no way for
    // this device ever to learn it.
    r.path += "&since=" + std::to_string(unixTimeFromIso8601(sinceStamp));
  }
  checkingInfo_ = false;
  return start(std::move(r), sink, /*authenticated=*/true);
}

bool WallabagClient::beginDownload(int id, BodySink& sink) {
  HttpRequest r;
  r.path = "/api/entries/" + std::to_string(id) + "/export.epub";
  checkingInfo_ = false;
  return start(std::move(r), sink, /*authenticated=*/true);
}

bool WallabagClient::beginArchive(int id, BodySink& sink) {
  HttpRequest r;
  r.method = "PATCH";
  r.path = "/api/entries/" + std::to_string(id) + "?archive=1";
  checkingInfo_ = false;
  return start(std::move(r), sink, /*authenticated=*/true);
}

bool WallabagClient::beginStar(int id, bool starred, BodySink& sink) {
  HttpRequest r;
  r.method = "PATCH";
  r.path = "/api/entries/" + std::to_string(id) + (starred ? "?starred=1" : "?starred=0");
  checkingInfo_ = false;
  return start(std::move(r), sink, /*authenticated=*/true);
}

}  // namespace reader
