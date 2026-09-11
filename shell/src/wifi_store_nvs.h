#pragma once
#include <string>
#include <string_view>

#include "reader/screen_wifi_settings.h"  // reader::WifiSink
#include "reader/wifi_store.h"

// THE SAVED-NETWORK RECORD AND ITS SECRETS, IN NVS.
//
// NOT NAMED `wifi.h`, AND THAT IS NOT TASTE. macOS's filesystem is
// case-INSENSITIVE by default, so a `shell/src/wifi.h` beside Arduino's
// `<WiFi.h>` shadows it: `#include <WiFi.h>` in the sibling translation unit
// resolved to THIS file, and the build failed with `'WiFi' was not declared`
// against a header the compiler had happily opened. It would also have built
// correctly on a case-sensitive volume, which is the worse half -- a
// collision that depends on which machine you are on.
//
// WHY NVS AND NOT THE CARD, which is the opposite of where reading progress
// lives: a passphrase is not about a book, and the card can be pulled and put
// in a computer. The session pointer is in NVS for the "a wake must work with
// no card" reason; this is in NVS because the card is REMOVABLE and a
// passphrase in a plain file on a FAT volume is a passphrase anybody can read.
//
// THE FORMAT IS core/'s AND THE I/O IS OURS, which is session_record.h's split
// exactly and for its stated reason: `shell/` has no test harness, so the
// parsing, the escaping, the caps and the at-most-one-AUTO rule are all in
// `reader/wifi_store.h` where the desktop suite drives them, and what is left
// here is bytes in and bytes out.
//
// ON-DEVICE FORENSICS. Two namespaces, because they are cleared on different
// occasions -- forgetting one network deletes one secret and rewrites the
// list, and NVS has no "delete by prefix":
//
//   namespace: "encre_wifi"   the list
//   keys:      "ver"   uint8  record version, currently 1
//              "nets"  str    the whole list, as
//                             `HOME:LA;BUREAU:L;CAFE-BIBLIO:-`
//
//   namespace: "encre_wpsk"   the passphrases, one key per network
//   keys:      "p_<8 hex>"    str, the key reader::wifiSecretKey() derives
//                             from the SSID -- NOT an index, so forgetting
//                             network 2 does not renumber 3 through 8. See
//                             wifi_store.h, which has the whole argument.
//
// e.g. `nvs_get encre_wifi nets str`, and note the secrets namespace is
// deliberately dumpable by the same means: this is a device with no secure
// element and NVS encryption is not enabled, so the passphrase is recoverable
// by anyone holding the board. That is stated rather than implied -- it is the
// same exposure as the stock firmware and it is not a claim this code should
// pretend otherwise about.
namespace shellwifi {

// The list, or an empty one. False means there was nothing to read or it was
// not a record this build understands -- both of which are "no saved
// networks", and neither of which is an error worth a screen.
bool load(reader::SavedNetworks& out);

// Replaces the list. The secrets are NOT touched: `remember` adds a row before
// a passphrase exists and `forget` removes the row, so the two move
// independently and only forget() knows which secret is now unreachable.
bool save(const reader::SavedNetworks& nets);

// The passphrase for a network, or empty.
std::string secret(std::string_view ssid);
bool putSecret(std::string_view ssid, std::string_view psk);
void dropSecret(std::string_view ssid);

// WHAT dropLockedWithoutSecret ASKS. A locked network whose passphrase is gone
// can only fail, and the two halves live in different namespaces, so only a
// read can see both -- which is why this is an interface in core/ rather than
// a flag on the record.
class NvsSecretProbe : public reader::SavedNetworks::SecretProbe {
 public:
  bool hasSecret(std::string_view ssid) const override;
};

// Applies AND persists, which is SettingsSink's contract verbatim -- see its
// header for why they are one call and why a failed write still shows the new
// value.
class NvsWifiSink : public reader::WifiSink {
 public:
  bool commit(const reader::SavedNetworks& nets) override { return save(nets); }
};

}  // namespace shellwifi
