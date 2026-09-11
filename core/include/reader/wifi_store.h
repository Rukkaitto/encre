#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace reader {

// THE SAVED-NETWORK RECORD, and the rules about the list it holds.
//
// IT LIVES IN core/ SO IT CAN BE TESTED, which is session_record.h's reason
// verbatim: shell/ has no test harness, and this is the one piece of the
// credential path that is pure logic. shell/ does the Preferences calls around
// it and nothing else.
//
// THE PASSPHRASE IS NOT HERE, AND THAT IS THE DESIGN RATHER THAN AN OMISSION.
// Spec 4.1b puts Wi-Fi credentials in NVS and NVS is not encrypted on this
// build, so the passphrase is plaintext either way -- what the split buys is
// that it is not TRIVIALLY PRINTABLE. `logToCard` tees everything logf() writes
// to a file on a REMOVABLE CARD, so one payload key holding the whole record
// would put a user's Wi-Fi password into any capture that dumps it. Nothing in
// this file has ever seen a passphrase: it handles the readable half, and the
// secrets sit under keys of their own that nothing logs.
//
// THE READABLE HALF IS session_record's IDIOM, for its reasons. One payload key
// rather than several, because two keys leave a valid-looking mixed record when
// a write is cut; a version key written AFTER it, so the version is a real
// commit record; and percent-escaped through reader/wire_escape.h, because an
// SSID is 0-32 ARBITRARY OCTETS and may legally contain `%`, `;` and `:` --
// every one of the bytes this format uses. A wire format a legal SSID can break
// is not a format.
//
// `HOME:LA;BUREAU:L;CAFE-BIBLIO:-` -- flags are letters rather than a bitmask
// because the point of not using base64 is that `nvs_get encre_wifi list str`
// prints something a person can read, and `HOME:3` needs the reader to know the
// mask. `-` is "neither", so every entry has a second field and an empty one
// stays a malformed record.
struct SavedNetwork {
  std::string ssid;
  // Whether joining it needs a passphrase. AN EXPLICIT FLAG RATHER THAN THE
  // ABSENCE OF A SECRET: a secret write that failed and a genuinely open
  // network would otherwise be indistinguishable, and mistaking the first for
  // the second joins a locked network with no password and fails confusingly.
  // The two are cross-checked on load -- see dropLockedWithoutSecret.
  bool locked = false;
  // Tried first when the radio comes up. AT MOST ONE network holds it, and zero
  // is legal -- see SavedNetworks.
  bool automatic = false;
};

// Eight is generous and cheap: a reader has home, work and perhaps a cafe, and
// the whole list at this cap is under a kilobyte of a 20 KB nvs partition. The
// ninth is REFUSED rather than evicting the oldest, because evicting silently
// loses a password the user typed and has no way to say so.
inline constexpr int kMaxSavedNetworks = 8;

// 802.11's own limit. An SSID is octets, not text, so this is a byte count.
inline constexpr size_t kSsidMaxBytes = 32;

// Bumped when a field's MEANING changes, not when one is added -- settings.h's
// rule.
//
// NOTHING READS THIS YET, and the sentence that used to follow it -- "an
// unrecognised version is a record this build cannot trust and is discarded
// whole" -- described behaviour no code implements. `grep -rn
// kWifiRecordVersion` finds this line and nothing else: not the encoder, not
// the decoder, not shell/. It is a header asserting a guarantee, which is the
// class of claim this project pays for most often.
//
// IT IS KEPT RATHER THAN DELETED because the number is the SHELL's to use and
// the shell has no Wi-Fi code at all yet: the session record's own version is
// a separate NVS key written beside the payload (see session_record.h, and
// the note there about why one payload key makes the version key a real
// commit record), and this record will be stored the same way. Deleting it
// would lose the number rather than the claim.
//
// So the reader to write is `shell/`'s, alongside the `encre_wifi` namespace
// it will keep the payload in -- and NAMING it is deliberate, because
// CLAUDE.md's own rule is that "nothing uses it today" is a claim with an
// expiry date and no owner, where a named caller goes stale loudly.
inline constexpr int kWifiRecordVersion = 1;

// The readable list, root-order preserved. Networks whose SSID is empty or over
// kSsidMaxBytes are skipped rather than encoded -- they cannot have come from a
// scan and would produce a record this file's own decoder refuses.
std::string encodeSavedNetworks(const std::vector<SavedNetwork>& nets);

// True when `raw` is a list this build can use, and `out` then holds it. False
// leaves `out` EMPTY and means "no saved networks" -- the same all-or-nothing
// contract decodeSessionStack has, and for its reason: a field written by
// something that is not this format says nothing good about the fields around
// it. Refused: null, an unknown flag letter, a missing or empty field, a
// trailing separator, a duplicate SSID, more than kMaxSavedNetworks entries,
// and more than one `A`.
//
// AN EMPTY STRING IS A VALID EMPTY LIST, not a refusal: it is what a device
// with no saved networks writes, and the first-run screen is built on it.
bool decodeSavedNetworks(const char* raw, std::vector<SavedNetwork>& out);

// The longest string encodeSavedNetworks can produce, plus its terminator, so a
// caller's read buffer is derived rather than a number kept in step by hand.
size_t savedNetworksMaxBytes();

// THE NVS KEY A NETWORK'S PASSPHRASE LIVES UNDER, keyed by a hash of the SSID.
//
// NOT BY INDEX, and the difference is a correctness one rather than a tidiness
// one: with index naming, forgetting network 2 renumbers 3 through 8 and every
// secret has to be rewritten -- eight writes, with a window in which a crash
// leaves passphrases attached to the wrong networks. A hash is stable and a
// forget is one delete.
//
// This is /.reader/state/<hash>.json's mechanism, collision handling included:
// the SSID is stored beside the secret and a mismatch means DROP, so a
// collision costs one network its passphrase and can never hand another network
// the wrong one. FNV-1a, 8 hex, `p_` prefix -- 10 characters against NVS's
// 15-character key cap.
std::string wifiSecretKey(std::string_view ssid);

// THE LIST AND ITS INVARIANTS. Both are enforced here rather than at the four
// call sites that would otherwise each have to remember them.
class SavedNetworks {
 public:
  // Whether a passphrase is actually stored for `ssid`. An interface rather
  // than a callable for Focus::Gate's reason: no <functional>, no allocation,
  // and the one implementer is the shell, which owns NVS.
  class SecretProbe {
   public:
    virtual bool hasSecret(std::string_view ssid) const = 0;

   protected:
    ~SecretProbe() = default;  // never owned, never deleted through this
  };

  SavedNetworks() = default;
  explicit SavedNetworks(std::vector<SavedNetwork> nets);

  const std::vector<SavedNetwork>& all() const { return nets_; }
  int size() const { return static_cast<int>(nets_.size()); }
  bool full() const { return size() >= kMaxSavedNetworks; }

  // -1 when absent. SSIDs are compared as BYTES, not case-folded: 802.11 says
  // they are octets, and two APs differing only in case are two networks.
  int indexOf(std::string_view ssid) const;

  // The preferred network, or null when there is none -- which is a legal
  // state, not an error. See forget().
  const SavedNetwork* automatic() const;

  // Adds, or updates the `locked` flag of one already present. False means the
  // list is full or the SSID is unusable; a SSID already present never fails,
  // so re-joining a known network cannot be refused by the cap.
  //
  // THE FIRST NETWORK SAVED BECOMES AUTOMATIC, because otherwise a one-network
  // device shows a flag that reads as broken.
  bool remember(std::string_view ssid, bool locked);

  // False when absent. FORGETTING THE AUTOMATIC NETWORK LEAVES ZERO AUTOMATIC:
  // nothing is silently promoted. A preference belongs to the user, and
  // changing it behind their back with nothing on the glass to say so is the
  // class of claim this project refuses for the battery gauge and the sleep
  // badge. Zero is not a dead end -- bring-up then tries every saved network in
  // the order the last scan ranked them.
  bool forget(std::string_view ssid);

  // Makes `ssid` the preferred network, clearing the flag from whichever held
  // it. False when absent or already automatic.
  bool makeAutomatic(std::string_view ssid);

  // What SELECT on a saved row does: automatic becomes saved, saved becomes
  // automatic. False when absent.
  bool toggleAutomatic(std::string_view ssid);

  // ENFORCES `locked IMPLIES a secret exists`, dropping the records that
  // contradict it and returning how many went. A locked network with no
  // passphrase cannot be joined and is not a network the user can fix from this
  // screen -- showing it is offering a row that can only fail.
  //
  // Called on load rather than on write, because the two halves live in
  // different NVS keys and only a read can see both.
  int dropLockedWithoutSecret(const SecretProbe& probe);

 private:
  // Re-establishes "at most one automatic" after any mutation. One function,
  // for ScrollWindow::clamp's reason.
  void clampAutomatic();

  std::vector<SavedNetwork> nets_;
};

}  // namespace reader
