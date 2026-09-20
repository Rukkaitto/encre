#pragma once
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "harness_state.h"

namespace harness {
// NVS SURVIVES THE PROCESS, not just the object -- which is load-bearing. The whole
// session-restore path is only exercisable if a record written before a simulated
// sleep is still there when setup() runs again, and the sleep is a thrown exception
// rather than a new process.
using Blob = std::vector<uint8_t>;
inline std::map<std::string, std::map<std::string, Blob>>& nvs() {
  static std::map<std::string, std::map<std::string, Blob>> store;
  return store;
}
}  // namespace harness

// THIS FAKE IS STRICTER THAN THE REAL THING, DELIBERATELY. NVS caps a key at 15
// characters and a longer one fails SILENTLY on the device; here it refuses and
// says so. That is the opposite direction from fake_fs.h's standing warning -- a
// fake more FORGIVING than the real thing makes every test above it meaningless --
// and it is right here, because the failure being modelled is silent. The next
// reader will want to "fix" this; do not.
class Preferences {
 public:
  bool begin(const char* name, bool readOnly = false, const char* partition = nullptr) {
    (void)readOnly;
    (void)partition;
    ns_ = name != nullptr ? name : "";
    open_ = true;
    return true;
  }
  void end() { open_ = false; }

  size_t putBytes(const char* key, const void* value, size_t len) {
    if (!keyOk(key)) return 0;
    const uint8_t* p = static_cast<const uint8_t*>(value);
    harness::nvs()[ns_][key] = harness::Blob(p, p + len);
    harness::record("<nvs> put %s/%s %zuB", ns_.c_str(), key, len);
    return len;
  }
  size_t getBytesLength(const char* key) {
    auto ns = harness::nvs().find(ns_);
    if (ns == harness::nvs().end()) return 0;
    auto it = ns->second.find(key);
    return it == ns->second.end() ? 0 : it->second.size();
  }
  size_t getBytes(const char* key, void* out, size_t maxLen) {
    auto ns = harness::nvs().find(ns_);
    if (ns == harness::nvs().end()) return 0;
    auto it = ns->second.find(key);
    if (it == ns->second.end()) return 0;
    const size_t n = it->second.size() < maxLen ? it->second.size() : maxLen;
    std::memcpy(out, it->second.data(), n);
    return n;
  }

  size_t putString(const char* key, const char* value) {
    return putBytes(key, value, std::strlen(value));
  }
  // THE READ-INTO-A-BUFFER OVERLOAD, which is the one every NVS adapter in shell/
  // actually calls -- session.cpp, wifi_store_nvs.cpp and wallabag_store_nvs.cpp all
  // read into a sized std::string rather than taking a copy. The first draft of this
  // fake had only the returning form and none of the three compiled against it.
  //
  // NUL-TERMINATES AND RETURNS THE LENGTH WRITTEN, as the real one does: the callers
  // size their buffer from sessionStackMaxBytes() and then trust the return.
  size_t getString(const char* key, char* out, size_t maxLen) {
    const size_t have = getBytesLength(key);
    if (out == nullptr || maxLen == 0) return 0;
    const size_t n = have < maxLen - 1 ? have : maxLen - 1;
    getBytes(key, out, n);
    out[n] = '\0';
    return n;
  }

  std::string getString(const char* key, const char* def = "") {
    const size_t n = getBytesLength(key);
    if (n == 0) return def;
    std::string s(n, '\0');
    getBytes(key, s.data(), n);
    return s;
  }
  size_t putUChar(const char* key, uint8_t v) { return putBytes(key, &v, 1); }
  uint8_t getUChar(const char* key, uint8_t def = 0) {
    uint8_t v = def;
    return getBytes(key, &v, 1) == 1 ? v : def;
  }

  bool remove(const char* key) {
    auto ns = harness::nvs().find(ns_);
    if (ns == harness::nvs().end()) return false;
    const bool had = ns->second.erase(key) > 0;
    if (had) harness::record("<nvs> remove %s/%s", ns_.c_str(), key);
    return had;
  }
  bool clear() {
    harness::nvs()[ns_].clear();
    harness::record("<nvs> clear %s", ns_.c_str());
    return true;
  }

 private:
  bool keyOk(const char* key) const {
    if (key != nullptr && std::strlen(key) <= 15) return true;
    harness::record("<nvs> REFUSED key %s -- over NVS's 15-character cap, which the real "
                    "API fails silently on",
                    key != nullptr ? key : "(null)");
    return false;
  }
  std::string ns_;
  bool open_ = false;
};
