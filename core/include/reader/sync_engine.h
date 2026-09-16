#pragma once
#include <memory>
#include <string>
#include <vector>

#include "reader/article_store.h"
#include "reader/wallabag_client.h"

namespace reader {

// WHERE AN ARTICLE'S BYTES GO. The engine is GIVEN one rather than opening a
// file, because opening a file is the shell's -- `core/` has no card. The test
// hands over a buffer and the device hands over a streaming card sink.
class SinkFactory {
 public:
  virtual ~SinkFactory() = default;
  // Null means "cannot take this one" -- no room, or the file would not open.
  // The engine treats that as a failed download rather than a failed sync: one
  // article missing beats nineteen not fetched.
  virtual std::unique_ptr<BodySink> forArticle(int id) = 0;
  // Throw away what a failed or cancelled download wrote. On the device that is
  // removing a `.part`, which is why a cancelled sync leaves no half file.
  virtual void discard(int id) = 0;
};

enum class SyncState { Idle, Running, Done };

enum class SyncOutcome {
  None,
  UpToDate,           // nothing new came back
  New,                // `fetched()` articles arrived
  Failed,             // a round trip did not complete
  NotAWallabag,
  CredentialsRefused,
  Cancelled,
};

// THE SYNC, AS A STATE MACHINE THE SHELL POLLS, for the transport's reason: the
// loop drives everything on this device and a blocking sync would stop the panel
// and the buttons for a minute or more.
//
// IT OWNS NO CLOCK AND NO RADIO. It is handed a transport that is already
// connected, which is what makes the whole of it testable on a desktop -- and
// what keeps the decision about WHEN the radio comes up in the shell, where the
// heap argument lives.
//
// THE ORDER IS PUSH BEFORE PULL, and that is the one sequencing decision worth
// stating: what the reader did on the device is the newer fact. Pulling first
// would hand back an entry as unarchived, write its sidecar, and then push the
// archive the reader made an hour ago -- leaving the list showing an article
// they have already dealt with until the sync after next.
class SyncEngine {
 public:
  SyncEngine(WallabagClient& client, ArticleStore& store, SinkFactory& sinks, int keepOffline)
      : client_(client), store_(store), sinks_(sinks), keep_(keepOffline) {}

  bool begin();
  void poll();

  SyncState state() const { return state_; }
  SyncOutcome outcome() const { return outcome_; }

  // WHY, IN THE ENGINE'S OWN WORDS. `SyncOutcome::Failed` has SIX call sites
  // here -- a refused info request, a refused listing request, a round trip that
  // did not complete, a listing that would not parse, a listing that would not
  // be written to the card, and a download the sinks refused -- and on glass
  // they arrive as one number. Three separate flash cycles were spent
  // discovering which layer had stopped a sync, each time because the layer that
  // knew did not say; this is that answer, kept as a literal so it costs no
  // allocation on the path where the heap is already scarce.
  //
  // Empty until something terminal happens, and empty on success.
  const char* note() const { return note_; }

  // THE LISTING AS IT ARRIVED, FOR THE SHELL TO PUT ON THE CARD WHEN IT WILL NOT
  // PARSE. A 5,368-byte listing off a real server failed on glass while every
  // desktop fixture passed -- including one written to be a REAL wallabag item,
  // thirty fields, HAL links, 0/1 integers, nulls, a `\u00e9` escape, driven at
  // three grains. So the bytes are the only thing left that can say why, and
  // reconstructing them from a log is not possible: the body is 5 KB and the log
  // buffer is 4.
  //
  // It is a DIAGNOSTIC and not a feature, which is why it hands back a reference
  // to a buffer the next request will clear rather than a copy: the one caller
  // reads it immediately, on the failure path, where nothing else is competing
  // for the heap.
  const std::string& listingBody() const { return listing_.body(); }

  // For the dialog's second stage. `toFetch` is 0 until the listing has been
  // walked, which is exactly when the caption may still say CONNECTING...
  int fetched() const { return fetched_; }
  int toFetch() const { return static_cast<int>(wanted_.size()); }

  // Stops after the file in flight; that file is discarded, everything already
  // fetched stays, and THE WATERMARK IS NOT ADVANCED -- so the next sync asks
  // for what this one did not get. That last clause is what makes cancelling
  // free rather than merely cheap.
  void cancel();

 private:
  enum class Step { Idle, Info, Push, List, Fetch, Done };

  void finish(SyncOutcome o, const char* why = "");
  void nextPush();
  void nextPage();
  void nextFetch();
  bool applyPage(const ListingPage& page);

  WallabagClient& client_;
  ArticleStore& store_;
  SinkFactory& sinks_;
  int keep_ = 50;

  Step step_ = Step::Idle;
  SyncState state_ = SyncState::Idle;
  SyncOutcome outcome_ = SyncOutcome::None;
  const char* note_ = "";

  BufferSink info_{4096};
  BufferSink listing_{kListingSinkCap};
  NullSink patch_;
  std::unique_ptr<BodySink> epub_;

  std::vector<PendingAction> queue_;
  size_t queueAt_ = 0;

  std::string since_;      // what we asked for
  std::string highWater_;  // the largest updated_at seen this run
  int page_ = 1;
  int pages_ = 1;

  // ONLY THE IDS, never the entries. A page's metadata is applied as it arrives,
  // so the engine holds four bytes per article it still has to fetch rather than
  // ~150 -- which at a hundred articles is the difference between 400 bytes and
  // 15 KB on a part whose reading floor is 42,152.
  std::vector<int> wanted_;
  size_t fetchAt_ = 0;
  int fetched_ = 0;
  bool cancelled_ = false;
};

}  // namespace reader
