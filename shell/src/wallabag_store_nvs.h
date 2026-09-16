#pragma once
#include <string>
#include <string_view>

#include "reader/token_store.h"

// THE WALLABAG BEARER AND REFRESH TOKENS, IN NVS.
//
// WHY NVS AND NOT THE CARD, when the CREDENTIALS this device syncs with are on
// the card: they are different things with different lifetimes. The five values
// in `/.reader/wallabag.json` are a reader's own, hand-edited on a computer, and
// deliberately readable so a person can fix them; a token is a SECRET this
// firmware minted, valid for about a fortnight, and there is nothing for anybody
// to edit. `wifi_store_nvs.h`'s reason applies unchanged -- the card is
// removable, so a token in a plain file on a FAT volume is one anybody holding
// the card can replay.
//
// THAT BUYS LESS THAN IT LOOKS LIKE, AND THE HONEST VERSION IS WORTH STATING:
// the same card carries the account's USERNAME and PASSWORD in plain text, so
// whoever holds it can mint a fresh token whenever they like. Putting the token
// in NVS therefore does not protect the account -- it keeps a minted secret out
// of a file nothing else needed to touch, and it survives a card swap, which is
// the practical half. The exposure is `wallabag_credentials.h`'s, undiminished.
//
// AND NVS IS NOT ENCRYPTED HERE. There is no secure element on this board and
// NVS encryption is not enabled, so `nvs_get encre_wbg access str` reads it back
// to anybody holding the device -- exactly as the Wi-Fi passphrases do, stated
// for the same reason rather than implied.
//
// ON-DEVICE FORENSICS:
//
//   namespace: "encre_wbg"
//   keys:      "ver"      uint8  record version, currently 1
//              "access"   str    the bearer, ~40 characters
//              "refresh"  str    the refresh token, ~40 characters
//
// THE VERSION IS CHECKED BEFORE THE PAYLOAD, `wifi_store_nvs.cpp`'s rule and
// `session.cpp`'s: asking for a string in a record of another shape is a
// question about a record we have already decided not to trust. A bump costs the
// reader one `grant_type=password`, which is a round trip and not a re-setup --
// the credentials that mint it never left the card.
namespace shellwallabag {

// The record version. Bump when the KEYS change, never for a token that simply
// stopped working -- that is `clear()`'s job and costs no re-grant.
inline constexpr int kTokenRecordVersion = 1;

// NVS caps a string value at 4000 bytes. A wallabag bearer is ~40 characters, so
// this is three orders of magnitude of room -- and it is asserted rather than
// trusted, because the one thing that would make a longer token arrive is a
// wallabag release nobody here will be watching for.
inline constexpr size_t kTokenMaxBytes = 512;

class NvsTokenStore : public reader::TokenStore {
 public:
  bool load(std::string& access, std::string& refresh) override;
  bool save(std::string_view access, std::string_view refresh) override;
  void clear() override;
};

}  // namespace shellwallabag
