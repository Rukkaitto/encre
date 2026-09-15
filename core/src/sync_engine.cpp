#include "reader/sync_engine.h"

#include "reader/xml.h"  // BufferSource

namespace reader {

bool SyncEngine::begin() {
  if (state_ == SyncState::Running) return false;
  state_ = SyncState::Running;
  outcome_ = SyncOutcome::None;
  cancelled_ = false;
  fetched_ = 0;
  fetchAt_ = 0;
  queueAt_ = 0;
  page_ = 1;
  pages_ = 1;
  wanted_.clear();
  highWater_.clear();
  queue_ = store_.pending();

  SyncWatermark w;
  store_.loadWatermark(w);
  since_ = w.since;

  step_ = Step::Info;
  info_.reset();
  if (!client_.beginInfo(info_)) return finish(SyncOutcome::Failed), false;
  return true;
}

void SyncEngine::finish(SyncOutcome o) {
  outcome_ = o;
  state_ = SyncState::Done;
  step_ = Step::Done;

  // THE WATERMARK IS WRITTEN ON EVERY TERMINAL PATH, but only its OUTCOME
  // changes on a failure: `since` advances solely when every download landed.
  // Both screens read that outcome, so a sync that failed must say so rather
  // than leaving the last success on the glass.
  SyncWatermark w;
  store_.loadWatermark(w);
  switch (o) {
    case SyncOutcome::UpToDate:
      w.since = highWater_.empty() ? w.since : highWater_;
      w.lastOutcome = "upToDate";
      break;
    case SyncOutcome::New:
      w.since = highWater_.empty() ? w.since : highWater_;
      w.lastOutcome = "new:" + std::to_string(fetched_);
      break;
    case SyncOutcome::Cancelled:
      // NOT `failed`. A reader who cancelled knows why the sync stopped, and
      // telling them it failed would be the device inventing a fault. The
      // watermark keeps whatever the last real sync left.
      break;
    case SyncOutcome::Failed:
    case SyncOutcome::NotAWallabag:
    case SyncOutcome::CredentialsRefused:
      w.lastOutcome = "failed";
      break;
    case SyncOutcome::None:
      break;
  }
  store_.saveWatermark(w);

  if (o == SyncOutcome::New || o == SyncOutcome::UpToDate) store_.prune(keep_);
}

void SyncEngine::nextPush() {
  // OLDEST FIRST, which store_.pending() has already ordered by id -- so a star
  // and a later archive of the same article arrive the way the reader did them.
  while (queueAt_ < queue_.size()) {
    const PendingAction& a = queue_[queueAt_];
    patch_ = NullSink{};
    const bool ok = a.kind == PendingKind::Archive
                        ? client_.beginArchive(a.id, patch_)
                        : client_.beginStar(a.id, a.kind == PendingKind::Star, patch_);
    if (ok) return;
    ++queueAt_;
  }
  step_ = Step::List;
  nextPage();
}

void SyncEngine::nextPage() {
  listing_.reset();
  if (!client_.beginListing(since_, page_, listing_)) finish(SyncOutcome::Failed);
}

void SyncEngine::nextFetch() {
  while (fetchAt_ < wanted_.size()) {
    const int id = wanted_[fetchAt_];
    epub_ = sinks_.forArticle(id);
    if (epub_ == nullptr) {
      // NO ROOM FOR THIS ONE. A failed download rather than a failed sync: one
      // article missing beats nineteen not fetched, and the watermark still does
      // not advance, so the next sync tries again.
      ++fetchAt_;
      continue;
    }
    if (client_.beginDownload(id, *epub_)) return;
    sinks_.discard(id);
    epub_.reset();
    ++fetchAt_;
  }
  // EVERY DOWNLOAD THAT WAS GOING TO LAND HAS LANDED, which is the only moment
  // the watermark may move.
  finish(fetched_ > 0 ? SyncOutcome::New : SyncOutcome::UpToDate);
}

bool SyncEngine::applyPage(const ListingPage& page) {
  for (const ListingEntry& e : page.entries) {
    if (e.updatedAt > highWater_) highWater_ = e.updatedAt;
    const bool have = store_.hasEpub(e.id);
    if (e.archived) {
      // ARCHIVED ON THE SERVER: the local files go. This is what the LATER
      // sync's dropped `archive=0` exists to make possible -- without it the
      // entry would never come back and the file would sit there for ever.
      if (have) {
        store_.queueArchive(e.id);
        // ...and the marker it just wrote is acked at once: the server already
        // knows, so pushing it back would be telling it what it told us.
        store_.ack(e.id, PendingKind::Archive);
      }
      continue;
    }
    ArticleMeta m;
    m.id = e.id;
    m.title = e.title;
    m.domain = e.domain;
    m.readingTime = e.readingTime;
    m.starred = e.starred;
    m.archived = false;
    m.updatedAt = e.updatedAt;
    if (!store_.writeMeta(m)) return false;
    if (!have) wanted_.push_back(e.id);
  }
  return true;
}

void SyncEngine::poll() {
  if (state_ != SyncState::Running) return;
  client_.poll();
  if (client_.state() != WallabagState::Done) return;

  const WallabagResult r = client_.result();
  if (r == WallabagResult::NotAWallabag) return finish(SyncOutcome::NotAWallabag);
  if (r == WallabagResult::CredentialsRefused) return finish(SyncOutcome::CredentialsRefused);
  if (r == WallabagResult::Unreachable) {
    if (cancelled_) {
      if (epub_ != nullptr) {
        sinks_.discard(wanted_[fetchAt_]);
        epub_.reset();
      }
      return finish(SyncOutcome::Cancelled);
    }
    if (step_ == Step::Fetch && epub_ != nullptr) {
      // THE PARTIAL FILE GOES AND EVERYTHING ALREADY FETCHED STAYS. The
      // watermark is not advanced, so the next sync asks for what this one did
      // not get -- which is what makes an interrupted sync safe to retry.
      sinks_.discard(wanted_[fetchAt_]);
      epub_.reset();
    }
    return finish(SyncOutcome::Failed);
  }

  switch (step_) {
    case Step::Info:
      step_ = Step::Push;
      nextPush();
      return;

    case Step::Push: {
      const PendingAction& a = queue_[queueAt_];
      const int code = client_.status();
      // A 404 ACKS TOO, because the desired state is already true: the server no
      // longer has the entry, so there is nothing left to tell it.
      if ((code >= 200 && code < 300) || code == 404) store_.ack(a.id, a.kind);
      ++queueAt_;
      nextPush();
      return;
    }

    case Step::List: {
      BufferSource src(listing_.body());
      ListingPage page;
      if (!parseListing(src, page)) return finish(SyncOutcome::Failed);
      if (!applyPage(page)) return finish(SyncOutcome::Failed);
      pages_ = page.pages > 0 ? page.pages : 1;
      if (page_ < pages_) {
        ++page_;
        nextPage();
        return;
      }
      step_ = Step::Fetch;
      nextFetch();
      return;
    }

    case Step::Fetch: {
      const int id = wanted_[fetchAt_];
      if (client_.status() >= 200 && client_.status() < 300) {
        ++fetched_;
      } else {
        // A per-article refusal, not a failed sync. The sidecar stays and the
        // `.epub` does not, so `list()` reports it as a stray rather than
        // offering a row that will not open.
        sinks_.discard(id);
      }
      epub_.reset();
      ++fetchAt_;
      if (cancelled_) return finish(SyncOutcome::Cancelled);
      nextFetch();
      return;
    }

    case Step::Idle:
    case Step::Done:
      return;
  }
}

void SyncEngine::cancel() {
  if (state_ != SyncState::Running) return;
  cancelled_ = true;
  client_.cancel();
}

}  // namespace reader
