#include "reader/wifi_store.h"

#include <cstdint>

#include "reader/wire_escape.h"

namespace reader {
namespace {

bool usableSsid(std::string_view ssid) {
  return !ssid.empty() && ssid.size() <= kSsidMaxBytes;
}

// The flag field. `-` for neither, so every entry has a second field and an
// empty one stays malformed rather than meaning "no flags".
std::string encodeFlags(const SavedNetwork& n) {
  std::string f;
  if (n.locked) f += 'L';
  if (n.automatic) f += 'A';
  if (f.empty()) f = "-";
  return f;
}

// False on an unknown letter, an empty field, or a letter twice -- all of which
// are records this build did not write.
bool decodeFlags(const char* start, size_t len, bool& locked, bool& automatic) {
  locked = false;
  automatic = false;
  if (len == 0) return false;
  if (len == 1 && start[0] == '-') return true;
  for (size_t i = 0; i < len; ++i) {
    if (start[i] == 'L') {
      if (locked) return false;
      locked = true;
    } else if (start[i] == 'A') {
      if (automatic) return false;
      automatic = true;
    } else {
      return false;
    }
  }
  return true;
}

}  // namespace

std::string encodeSavedNetworks(const std::vector<SavedNetwork>& nets) {
  std::string out;
  int written = 0;
  for (const SavedNetwork& n : nets) {
    if (written >= kMaxSavedNetworks) break;
    // A network the decoder would refuse is not written: an unusable SSID
    // cannot have come from a scan, and encoding one would produce a record
    // this file's own reader discards WHOLE, taking the good entries with it.
    if (!usableSsid(n.ssid)) continue;
    if (!out.empty()) out += ';';
    out += escapeWireField(n.ssid);
    out += ':';
    out += encodeFlags(n);
    ++written;
  }
  return out;
}

bool decodeSavedNetworks(const char* raw, std::vector<SavedNetwork>& out) {
  out.clear();
  if (raw == nullptr) return false;
  // An empty string is a device with nothing saved, which is a state the
  // first-run screen is built on rather than a refusal.
  if (*raw == '\0') return true;

  const char* p = raw;
  bool seenAutomatic = false;
  while (*p != '\0') {
    const char* entryEnd = p;
    while (*entryEnd != '\0' && *entryEnd != ';') ++entryEnd;
    if (entryEnd == p) {  // `;;` or a leading/trailing separator
      out.clear();
      return false;
    }

    const char* sep = p;
    while (sep < entryEnd && *sep != ':') ++sep;
    if (sep == entryEnd) {  // no flag field at all
      out.clear();
      return false;
    }

    SavedNetwork n;
    if (!decodeWireField(p, static_cast<size_t>(sep - p), n.ssid) ||
        !decodeFlags(sep + 1, static_cast<size_t>(entryEnd - sep - 1), n.locked, n.automatic) ||
        n.ssid.size() > kSsidMaxBytes) {
      out.clear();
      return false;
    }
    // TWO ENTRIES FOR ONE NETWORK is a record this build never writes, and
    // honouring it would mean two rows the user cannot tell apart, one of which
    // holds the passphrase.
    for (const SavedNetwork& seen : out) {
      if (seen.ssid == n.ssid) {
        out.clear();
        return false;
      }
    }
    // More than one preferred network is the same kind of contradiction: the
    // flag means "tried FIRST", and two of them name no order at all.
    if (n.automatic) {
      if (seenAutomatic) {
        out.clear();
        return false;
      }
      seenAutomatic = true;
    }
    out.push_back(std::move(n));
    if (static_cast<int>(out.size()) > kMaxSavedNetworks) {
      out.clear();
      return false;
    }

    p = (*entryEnd == ';') ? entryEnd + 1 : entryEnd;
    // A trailing `;` leaves p on the terminator, which ends the loop with the
    // entry count already correct -- but `a:L;` is a record with an empty
    // entry, so it is refused by the entryEnd == p test on the next pass.
    if (*entryEnd == ';' && *p == '\0') {
      out.clear();
      return false;
    }
  }
  return true;
}

size_t savedNetworksMaxBytes() {
  // Worst case: every SSID is kSsidMaxBytes of bytes that all escape to three
  // characters, every entry carries both flags, and the list is full.
  const size_t entry = kSsidMaxBytes * 3 + 1 /*:*/ + 2 /*LA*/;
  return entry * kMaxSavedNetworks + (kMaxSavedNetworks - 1) /*;*/ + 1 /*NUL*/;
}

std::string wifiSecretKey(std::string_view ssid) {
  // FNV-1a, the hash /.reader/state/<hash>.json already uses.
  uint32_t h = 2166136261u;
  for (const char ch : ssid) {
    h ^= static_cast<unsigned char>(ch);
    h *= 16777619u;
  }
  static const char* const kHex = "0123456789abcdef";
  std::string key = "p_";
  for (int shift = 28; shift >= 0; shift -= 4) key += kHex[(h >> shift) & 0xF];
  return key;
}

SavedNetworks::SavedNetworks(std::vector<SavedNetwork> nets) : nets_(std::move(nets)) {
  clampAutomatic();
}

int SavedNetworks::indexOf(std::string_view ssid) const {
  for (int i = 0; i < size(); ++i) {
    if (nets_[static_cast<size_t>(i)].ssid == ssid) return i;
  }
  return -1;
}

const SavedNetwork* SavedNetworks::automatic() const {
  for (const SavedNetwork& n : nets_) {
    if (n.automatic) return &n;
  }
  return nullptr;
}

Remembered SavedNetworks::remember(std::string_view ssid, bool locked) {
  if (!usableSsid(ssid)) return Remembered::BadSsid;
  const int at = indexOf(ssid);
  if (at >= 0) {
    // Already known: re-joining it may have changed whether it needs a
    // passphrase (an open network that has since been secured), and the cap
    // must not refuse a network that is not being added.
    nets_[static_cast<size_t>(at)].locked = locked;
    return Remembered::Yes;
  }
  // THE REFUSAL, AND IT RETURNS BEFORE ANYTHING IS TOUCHED. The caller's
  // passphrase write is conditional on this answer, so a refusal that mutated
  // anything would leave a half-saved network -- see #162, which is the
  // mirror: the answer was dropped and the write happened regardless.
  if (full()) return Remembered::ListFull;
  SavedNetwork n;
  n.ssid = std::string(ssid);
  n.locked = locked;
  // The first network saved becomes the preferred one; a later one does not
  // take the flag from it without being asked.
  n.automatic = (automatic() == nullptr);
  nets_.push_back(std::move(n));
  return Remembered::Yes;
}

bool SavedNetworks::forget(std::string_view ssid) {
  const int at = indexOf(ssid);
  if (at < 0) return false;
  // Nothing is promoted. See the header: a preference is the user's.
  nets_.erase(nets_.begin() + at);
  return true;
}

bool SavedNetworks::makeAutomatic(std::string_view ssid) {
  const int at = indexOf(ssid);
  if (at < 0) return false;
  if (nets_[static_cast<size_t>(at)].automatic) return false;
  for (SavedNetwork& n : nets_) n.automatic = false;
  nets_[static_cast<size_t>(at)].automatic = true;
  return true;
}

bool SavedNetworks::toggleAutomatic(std::string_view ssid) {
  const int at = indexOf(ssid);
  if (at < 0) return false;
  if (nets_[static_cast<size_t>(at)].automatic) {
    nets_[static_cast<size_t>(at)].automatic = false;
    return true;
  }
  for (SavedNetwork& n : nets_) n.automatic = false;
  nets_[static_cast<size_t>(at)].automatic = true;
  return true;
}

int SavedNetworks::dropLockedWithoutSecret(const SecretProbe& probe) {
  int dropped = 0;
  for (size_t i = nets_.size(); i > 0; --i) {
    const SavedNetwork& n = nets_[i - 1];
    if (n.locked && !probe.hasSecret(n.ssid)) {
      nets_.erase(nets_.begin() + static_cast<long>(i - 1));
      ++dropped;
    }
  }
  // Dropping the preferred network leaves zero preferred, which forget()'s
  // rule already makes legal -- and promoting one here would be choosing on
  // the user's behalf at the one moment the device knows least.
  return dropped;
}

void SavedNetworks::clampAutomatic() {
  bool seen = false;
  for (SavedNetwork& n : nets_) {
    if (!n.automatic) continue;
    if (seen) {
      n.automatic = false;
    } else {
      seen = true;
    }
  }
}

}  // namespace reader
