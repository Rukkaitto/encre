#include "wallabag_store_nvs.h"

#include <Arduino.h>
#include <Preferences.h>

#include <vector>

namespace shellwallabag {
namespace {

// NVS caps a namespace at 15 characters and a key at 15, so both are short.
constexpr const char* kNs = "encre_wbg";
constexpr const char* kKeyVersion = "ver";
constexpr const char* kKeyAccess = "access";
constexpr const char* kKeyRefresh = "refresh";

// NVS's own cap on a string value. The bound below is checked against it here
// rather than in the header so the header stays free of the platform.
constexpr size_t kNvsStringCap = 4000;
static_assert(kTokenMaxBytes < kNvsStringCap,
              "a token longer than NVS will hold would be silently truncated, and a "
              "truncated bearer is a 401 with no cause anybody can read");

}  // namespace

bool NvsTokenStore::load(std::string& access, std::string& refresh) {
  access.clear();
  refresh.clear();

  Preferences prefs;
  // readOnly = true fails when the namespace has never been written, which on a
  // device that has never synced is the ORDINARY case -- so this is a quiet
  // false and not a complaint. `TokenStore::load`'s own header says false means
  // "nothing stored, which is the state a first sync is in".
  if (!prefs.begin(kNs, true)) return false;

  const uint8_t version = prefs.getUChar(kKeyVersion, 0);
  if (version != static_cast<uint8_t>(kTokenRecordVersion)) {
    prefs.end();
    if (version != 0) {
      Serial.printf("[wallabag] token record version %u is not %d; discarding it whole. "
                    "That costs one password grant and nothing else -- the credentials "
                    "that mint it never left the card\n",
                    version, kTokenRecordVersion);
      Serial.flush();
    }
    return false;
  }

  std::vector<char> raw(kTokenMaxBytes + 1, '\0');
  prefs.getString(kKeyAccess, raw.data(), raw.size());
  access = raw.data();
  raw.assign(kTokenMaxBytes + 1, '\0');
  prefs.getString(kKeyRefresh, raw.data(), raw.size());
  refresh = raw.data();
  prefs.end();

  // BOTH OR NEITHER. A record with an access token and no refresh is one that
  // can make exactly one more call and then has to fall back to the password
  // grant anyway -- and the ladder would spend a 401 discovering that. Half a
  // record is treated as none, which is `decodeSavedNetworks`' refuse-whole rule
  // on a two-field record.
  if (access.empty() || refresh.empty()) {
    access.clear();
    refresh.clear();
    return false;
  }
  return true;
}

bool NvsTokenStore::save(std::string_view access, std::string_view refresh) {
  // REFUSED RATHER THAN TRUNCATED, which is `kPskMaxBytes`' rule one store over:
  // a cut bearer is a 401 whose cause is invisible, where a refusal is one
  // failed sync that says so.
  if (access.size() > kTokenMaxBytes || refresh.size() > kTokenMaxBytes) {
    Serial.printf("[wallabag] a token is %u bytes, over the %u this store holds; not saving. "
                  "The sync still works -- it will grant a fresh one each time\n",
                  (unsigned)(access.size() > refresh.size() ? access.size() : refresh.size()),
                  (unsigned)kTokenMaxBytes);
    Serial.flush();
    return false;
  }
  if (access.empty() || refresh.empty()) return false;

  Preferences prefs;
  if (!prefs.begin(kNs, false)) return false;
  // THE PAYLOAD BEFORE THE VERSION, so a write cut in the middle leaves a record
  // that reads as ABSENT rather than as a valid record with a stale token in it.
  // `session.cpp`'s own ordering, and the reason its payload is one key.
  const size_t a = prefs.putString(kKeyAccess, std::string(access).c_str());
  const size_t r = prefs.putString(kKeyRefresh, std::string(refresh).c_str());
  bool ok = a == access.size() && r == refresh.size();
  if (ok) ok = prefs.putUChar(kKeyVersion, static_cast<uint8_t>(kTokenRecordVersion)) == 1;
  if (!ok) {
    // A HALF-WRITTEN RECORD IS REMOVED RATHER THAN LEFT, because the version is
    // what makes it readable and a payload without one is a namespace that
    // `load` will quietly ignore for ever while NVS keeps the bytes.
    prefs.remove(kKeyVersion);
    Serial.printf("[wallabag] the token record did not write; the next sync grants a fresh "
                  "one\n");
    Serial.flush();
  }
  prefs.end();
  return ok;
}

void NvsTokenStore::clear() {
  Preferences prefs;
  // A namespace that has never been written is nothing to clear, and saying so
  // would be a line about a device that is simply new.
  if (!prefs.begin(kNs, false)) return;
  // THE VERSION FIRST, which is save()'s ordering reversed and for its reason:
  // whatever happens after this, the record reads as absent rather than as a
  // valid one holding a token the server has stopped honouring.
  prefs.remove(kKeyVersion);
  prefs.remove(kKeyAccess);
  prefs.remove(kKeyRefresh);
  prefs.end();
}

}  // namespace shellwallabag
