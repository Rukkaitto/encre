#include "wifi_store_nvs.h"

#include <Arduino.h>
#include <Preferences.h>

#include <vector>

namespace shellwifi {
namespace {

// NVS caps a namespace at 15 characters and a key at 15, so both are short.
// See wifi.h for how to dump them off a device.
constexpr const char* kNsList = "encre_wifi";
constexpr const char* kNsSecrets = "encre_wpsk";
constexpr const char* kKeyVersion = "ver";
constexpr const char* kKeyNets = "nets";

// 802.11's own maximum, plus room for the terminator. A longer value is not a
// passphrase and is refused rather than truncated -- a cut passphrase is a
// join that fails for a reason nobody can see.
constexpr size_t kPskMaxBytes = 63;

}  // namespace

bool load(reader::SavedNetworks& out) {
  out = reader::SavedNetworks{};

  Preferences prefs;
  // readOnly = true fails when the namespace has never been written, which on
  // a device that has never joined anything is the ORDINARY case -- so this is
  // a quiet return and not a complaint, unlike the session record's, where the
  // same silence once cost a diagnosis. The difference is that a missing
  // session record is indistinguishable from a broken write, and a missing
  // Wi-Fi record is indistinguishable from nothing, which is what the empty
  // hub is boarded for.
  if (!prefs.begin(kNsList, true)) return false;

  // THE VERSION IS CHECKED BEFORE THE PAYLOAD IS TOUCHED, session.cpp's rule:
  // asking for a string under `nets` in a record of another shape is a
  // question about a record we have already decided not to trust.
  //
  // AND THIS IS kWifiRecordVersion's FIRST READER. Its header said "an
  // unrecognised version is a record this build cannot trust and is discarded
  // whole" while nothing in the repo read it at all; that sentence is true
  // here for the first time.
  const uint8_t version = prefs.getUChar(kKeyVersion, 0);
  if (version != static_cast<uint8_t>(reader::kWifiRecordVersion)) {
    prefs.end();
    if (version != 0) {
      Serial.printf("[wifi] record version %u is not %d; discarding the saved list whole. "
                    "That costs the user their networks exactly once, which is what the "
                    "bump is for\n",
                    version, reader::kWifiRecordVersion);
      Serial.flush();
    }
    return false;
  }

  // Sized by the format itself rather than by a number kept in step by hand.
  std::vector<char> raw(reader::savedNetworksMaxBytes(), '\0');
  prefs.getString(kKeyNets, raw.data(), raw.size());
  prefs.end();

  std::vector<reader::SavedNetwork> nets;
  if (!reader::decodeSavedNetworks(raw.data(), nets)) {
    // REFUSED WHOLE, never best-effort -- decodeSavedNetworks' own rule. A
    // half-decoded list would silently drop the network the reader actually
    // uses and leave no trace of having done so.
    Serial.printf("[wifi] the saved list did not decode; treating the device as having "
                  "none: %s\n",
                  raw.data());
    Serial.flush();
    return false;
  }
  out = reader::SavedNetworks(std::move(nets));
  return true;
}

bool save(const reader::SavedNetworks& nets) {
  const std::string wire = reader::encodeSavedNetworks(nets.all());

  Preferences prefs;
  if (!prefs.begin(kNsList, false)) {
    Serial.printf("[wifi] cannot open NVS namespace %s for write\n", kNsList);
    Serial.flush();
    return false;
  }
  // THE VERSION KEY GOES LAST, session.cpp's rule and for its reason: it is
  // the only key load() validates, so a write that dies half way reads back as
  // "no saved networks" rather than as a valid pointer to half a list.
  bool ok = prefs.putString(kKeyNets, wire.c_str()) == wire.size() &&
            prefs.putUChar(kKeyVersion,
                           static_cast<uint8_t>(reader::kWifiRecordVersion)) == sizeof(uint8_t);
  if (!ok) prefs.remove(kKeyVersion);
  prefs.end();

  if (!ok) {
    Serial.printf("[wifi] NVS write failed for list %s\n", wire.c_str());
    Serial.flush();
    return false;
  }
  Serial.printf("[wifi] stored %d network(s): %s\n", nets.size(), wire.c_str());
  Serial.flush();
  return true;
}

std::string secret(std::string_view ssid) {
  const std::string key = reader::wifiSecretKey(ssid);
  Preferences prefs;
  if (!prefs.begin(kNsSecrets, true)) return {};
  std::vector<char> raw(kPskMaxBytes + 1, '\0');
  prefs.getString(key.c_str(), raw.data(), raw.size());
  prefs.end();
  return std::string(raw.data());
}

bool putSecret(std::string_view ssid, std::string_view psk) {
  if (psk.size() > kPskMaxBytes) return false;
  const std::string key = reader::wifiSecretKey(ssid);
  const std::string value(psk);
  Preferences prefs;
  if (!prefs.begin(kNsSecrets, false)) {
    Serial.printf("[wifi] cannot open NVS namespace %s for write\n", kNsSecrets);
    Serial.flush();
    return false;
  }
  const bool ok = prefs.putString(key.c_str(), value.c_str()) == value.size();
  prefs.end();
  // THE PASSPHRASE IS NOT LOGGED, and the key is: a device log is read over a
  // cable and pasted into issues. The key is a hash of the SSID and gives
  // nothing away, which is what makes the failure diagnosable without the
  // secret being in the transcript.
  if (!ok) {
    Serial.printf("[wifi] could not store the passphrase under %s\n", key.c_str());
    Serial.flush();
  }
  return ok;
}

void dropSecret(std::string_view ssid) {
  const std::string key = reader::wifiSecretKey(ssid);
  Preferences prefs;
  if (!prefs.begin(kNsSecrets, false)) return;
  prefs.remove(key.c_str());
  prefs.end();
}

bool NvsSecretProbe::hasSecret(std::string_view ssid) const { return !secret(ssid).empty(); }

}  // namespace shellwifi
