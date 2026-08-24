#include "reader/dir_cache.h"

#include <cstring>
#include <new>
#include <utility>

namespace reader {

void DirListingCache::Slot::release() {
  held = false;
  names.reset();
  rows.reset();
  nameBytes = 0;
  rowCount = 0;
  // Assigned rather than cleared: a std::string's clear() keeps its buffer, and
  // "" is what a released slot is supposed to cost.
  path = std::string();
}

void DirListingCache::clear() {
  for (Slot& s : slots_) s.release();
}

const DirListingCache::Slot* DirListingCache::find(std::string_view path) const {
  for (const Slot& s : slots_)
    if (s.held && s.path == path) return &s;
  return nullptr;
}

DirListingCache::Slot* DirListingCache::find(std::string_view path) {
  for (Slot& s : slots_)
    if (s.held && s.path == path) return &s;
  return nullptr;
}

DirListingCache::Slot* DirListingCache::freeSlot() {
  for (Slot& s : slots_)
    if (!s.held) return &s;
  return nullptr;
}

// EVICT THE SMALLEST, not the least recently used. The cost this class exists to
// avoid is proportional to the entry count -- 2.90 ms each -- so the listing
// worth keeping is the long one, and LRU would happily throw /books out for the
// sidecar directory rescan() reads immediately after it. Evicting by size is
// also wedge-free in a way "refuse anything smaller than what is held" is not: a
// new listing always gets a slot, so no directory can be locked out of the cache
// for good by one big neighbour.
DirListingCache::Slot* DirListingCache::smallestHeld() {
  Slot* worst = nullptr;
  for (Slot& s : slots_) {
    if (!s.held) continue;
    if (worst == nullptr || s.rowCount < worst->rowCount) worst = &s;
  }
  return worst;
}

bool DirListingCache::holds(std::string_view path) const { return find(path) != nullptr; }

size_t DirListingCache::entriesFor(std::string_view path) const {
  const Slot* s = find(path);
  return s == nullptr ? 0 : s->rowCount;
}

size_t DirListingCache::slotsHeld() const {
  size_t n = 0;
  for (const Slot& s : slots_)
    if (s.held) ++n;
  return n;
}

size_t DirListingCache::residentBytes() const {
  size_t n = 0;
  for (const Slot& s : slots_)
    if (s.held) n += s.bytes();
  return n;
}

bool DirListingCache::store(std::string_view path, const std::vector<DirEntry>& entries) {
  // FIRST, and whatever happens below. Whoever is calling has just re-read this
  // directory off the card, so whatever was held for it is by definition the old
  // answer -- and every refusal below then leaves this path UNCACHED rather than
  // leaving the previous rows to answer for it. This is the one function that
  // can make the cache wrong, and this is the line that stops it.
  if (Slot* mine = find(path)) mine->release();

  if (entries.size() < minEntries_) return false;

  // Two passes: measure, then fill. The sizes have to be exact before anything
  // is allocated, because these buffers are never grown -- growing is what turns
  // one 10 KB allocation into a sequence of them and fragments a heap whose
  // largest FREE BLOCK is what decides whether the next chapter opens.
  size_t nameBytes = 0;
  for (const DirEntry& e : entries) {
    if (e.name.size() > kMaxNameBytes) return false;
    nameBytes += e.name.size();
  }
  const size_t bytes = nameBytes + entries.size() * sizeof(Row);
  if (bytes > maxBytes_) return false;  // too big even with everything else gone

  // Make room. The ceiling is across ALL slots, so a big listing may cost a held
  // one its place -- which is right: the alternative is refusing to hold an
  // expensive listing because a cheap one got there first.
  //
  // The loop always terminates with room, and `bytes > maxBytes_` above is why:
  // in the worst case every slot is released and residentBytes() reaches 0,
  // where the listing is known to fit. So there is no "still does not fit"
  // branch below -- an unreachable one nothing exercises would be worse than
  // none.
  Slot* target = freeSlot();
  if (target == nullptr) target = smallestHeld();
  target->release();
  while (residentBytes() + bytes > maxBytes_) {
    Slot* other = smallestHeld();
    if (other == nullptr) break;
    other->release();
  }

  // nothrow, and it is not decoration: the firmware is -fno-exceptions, so an
  // ordinary new that cannot be served calls std::terminate -- an abort() with
  // no message and no stack, which this project has already spent a debugging
  // session mistaking for a navigation bug. A cache must be able to be told no.
  //
  // nameBytes can be 0 for a directory of files with empty names, which no real
  // filesystem produces but the contract does not forbid. new char[0] is legal
  // and yields a usable unique pointer, so there is no special case.
  std::unique_ptr<char[]> names(new (std::nothrow) char[nameBytes]);
  if (!names) return false;
  std::unique_ptr<Row[]> rows(new (std::nothrow) Row[entries.size()]);
  if (!rows) return false;

  size_t at = 0;
  for (size_t i = 0; i < entries.size(); ++i) {
    const DirEntry& e = entries[i];
    if (!e.name.empty()) std::memcpy(names.get() + at, e.name.data(), e.name.size());
    rows[i].nameBegin = static_cast<uint32_t>(at);
    rows[i].nameLength = static_cast<uint16_t>(e.name.size());
    rows[i].isDir = e.isDir;
    rows[i].size = e.size;
    at += e.name.size();
  }

  // `held` is set LAST, after everything that can fail has succeeded, so a slot
  // is never findable in a half-built state.
  target->names = std::move(names);
  target->rows = std::move(rows);
  target->nameBytes = nameBytes;
  target->rowCount = entries.size();
  target->path = std::string(path);
  target->held = true;
  return true;
}

bool DirListingCache::appendTo(std::string_view path, std::vector<DirEntry>& out) {
  const Slot* s = find(path);
  if (s == nullptr) {
    ++misses_;
    return false;
  }
  ++hits_;

  // Reserve for what is about to be appended, not for the total -- `out` may
  // already hold entries, because list() APPENDS and a caller is entitled to
  // gather several directories into one vector.
  out.reserve(out.size() + s->rowCount);
  for (size_t i = 0; i < s->rowCount; ++i) {
    const Row& r = s->rows[i];
    DirEntry e;
    e.name.assign(s->names.get() + r.nameBegin, r.nameLength);
    e.isDir = r.isDir;
    e.size = r.size;
    out.push_back(std::move(e));
  }
  return true;
}

}  // namespace reader
