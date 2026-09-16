#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "reader/http_transport.h"
#include "reader/json_stream.h"  // ByteSource
#include "reader/token_store.h"
#include "reader/wallabag_credentials.h"

namespace reader {

// THE THREE ANSWERS, and they are three because the reader can do three
// different things about them. BookError's argument at the API layer: one
// sentence would be a lie.
enum class WallabagResult {
  None,  // nothing has been asked yet
  Ok,
  // `/api/info` did not answer 200 with a body carrying `appname`. The URL is
  // not a wallabag -- answerable BEFORE any credential is used, which is what
  // makes it a different message from a refusal.
  NotAWallabag,
  // The server judged the credentials and said no. Deterministic: the same file
  // gives the same answer, which is why the dialog offers no retry.
  CredentialsRefused,
  // Any transport failure. The round trip never produced an answer.
  Unreachable,
};

enum class WallabagState { Idle, Running, Done };

// One entry of a listing page, as much of it as this device draws.
struct ListingEntry {
  int id = 0;
  std::string title;
  std::string domain;
  int readingTime = 0;
  bool archived = false;
  bool starred = false;
  std::string updatedAt;
  // Whether the title came back cut. Carried per entry so a log can say which,
  // and so the store can record what it actually holds.
  bool titleTruncated = false;
};

struct ListingPage {
  std::vector<ListingEntry> entries;
  int page = 0;
  int pages = 0;
  int total = 0;
};

// Parse one listing page. A free function rather than a method, because it needs
// nothing the client holds -- and because a test can drive it over a
// byte-at-a-time source without a transport at all.
bool parseListing(ByteSource& src, ListingPage& out);

// WALLABAG'S `since` IS A UNIX TIMESTAMP AND THE WATERMARK STORES THE SERVER'S
// ISO-8601 STRING, so one of them has to convert -- and it is this one, because
// the string is what the two screens draw and what `list()` sorts by.
//
// PURE ARITHMETIC, NO CLOCK (#132). Days-from-civil is a closed form; nothing
// here asks what time it is, only what the server said. An unparseable stamp
// answers 0, which the caller reads as "ask for everything" -- the safe
// direction, since a sync that re-offers what we have is slower and a sync that
// skips is a lost article.
int64_t unixTimeFromIso8601(std::string_view stamp);

// THE CLIENT, poll-shaped for the transport's reason: the loop drives it.
//
// REFRESH ON 401, NEVER ON A PREDICTED EXPIRY, and that is not a preference.
// `expires_in: 3600` is relative to a grant that may have happened before a deep
// sleep, which resets millis() -- so the device CANNOT predict expiry and must
// not try. One extra round trip on the first call after a long sleep, and no
// state to get wrong.
//
// THE LADDER IS ONE STEP AT EACH LEVEL: a 401 on a call triggers one refresh and
// one retry; a 401 on the refresh triggers one password grant and one retry; a
// 401 on the password grant is CredentialsRefused. More would be a device
// hammering a server that has already answered.
class WallabagClient {
 public:
  WallabagClient(HttpTransport& http, TokenStore& tokens, const WallabagCredentials& creds)
      : http_(http), tokens_(tokens), creds_(creds) {}

  // `/api/info`, which needs no token -- so "that URL is not a wallabag" is
  // answerable before any credential is used.
  //
  // IT TAKES A BufferSink, NOT A BodySink, AND THE FIRMWARE BUILD IS WHY. This
  // answer is decided from the BODY (a proxy and a captive portal both answer
  // 200), so the sink has to be readable -- and the first version got there with
  // a `dynamic_cast`, which compiles on the desktop and fails outright under the
  // firmware's `-fno-rtti`. A typed parameter says the same thing at the call
  // site and costs nothing: every caller already holds one.
  bool beginInfo(BufferSink& sink);
  // A listing page. `sinceStamp` empty means a FIRST sync.
  bool beginListing(const std::string& sinceStamp, int page, BodySink& sink);
  bool beginDownload(int id, BodySink& sink);
  bool beginArchive(int id, BodySink& sink);
  bool beginStar(int id, bool starred, BodySink& sink);

  void poll();
  WallabagState state() const { return state_; }
  WallabagResult result() const { return result_; }
  // The HTTP status of the request that finished, for the log and for the one
  // caller that branches on it: a 404 on a PATCH acks, because the desired state
  // is already true.
  int status() const { return status_; }
  void cancel();

  // How many pages the listing said there were; 0 until one has been read.
  static constexpr int kPerPage = 20;

 private:
  enum class Step { Idle, Call, Refreshing, Granting, Retrying };

  bool start(HttpRequest req, BodySink& sink, bool authenticated);
  void sendPending();
  void beginRefresh();
  void beginGrant();
  bool haveToken();

  HttpTransport& http_;
  TokenStore& tokens_;
  WallabagCredentials creds_;

  HttpRequest pending_;      // the caller's request, replayed after a refresh
  BodySink* sink_ = nullptr;
  BufferSink grantSink_{4096};

  std::string access_;
  std::string refresh_;
  bool tokensLoaded_ = false;

  Step step_ = Step::Idle;
  WallabagState state_ = WallabagState::Idle;
  WallabagResult result_ = WallabagResult::None;
  int status_ = 0;
  bool authenticated_ = false;
  bool checkingInfo_ = false;
  BufferSink* infoSink_ = nullptr;  // set only by beginInfo, and typed there
};

}  // namespace reader
