// THE SAVED-NETWORK RECORD. A persisted format with invariants, which is the
// shape this project tests hardest -- a record that parses and is wrong is
// worse than one that refuses, and the only thing standing between the two is
// here. shell/ has no harness, so nothing else can.
#include <initializer_list>
#include <string>
#include <vector>

#include "doctest.h"
#include "reader/wifi_store.h"

using namespace reader;

namespace {

SavedNetwork net(std::string ssid, bool locked, bool automatic) {
  SavedNetwork n;
  n.ssid = std::move(ssid);
  n.locked = locked;
  n.automatic = automatic;
  return n;
}

std::vector<SavedNetwork> roundTrip(const std::vector<SavedNetwork>& in) {
  const std::string wire = encodeSavedNetworks(in);
  std::vector<SavedNetwork> out;
  REQUIRE(decodeSavedNetworks(wire.c_str(), out));
  return out;
}

struct Secrets : SavedNetworks::SecretProbe {
  std::vector<std::string> have;
  bool hasSecret(std::string_view ssid) const override {
    for (const std::string& s : have) {
      if (s == ssid) return true;
    }
    return false;
  }
};

}  // namespace

// ---------------------------------------------------------------- wire format

TEST_CASE("the record is readable, which is the reason it is not base64") {
  const std::vector<SavedNetwork> nets{
      net("HOME", true, true), net("BUREAU", true, false), net("CAFE-BIBLIO", false, false)};
  CHECK(encodeSavedNetworks(nets) == "HOME:LA;BUREAU:L;CAFE-BIBLIO:-");
}

TEST_CASE("an empty list is an empty string, and an empty string is an empty list") {
  // A device with nothing saved: the state the first-run screen is built on,
  // and NOT a refusal.
  CHECK(encodeSavedNetworks({}).empty());
  std::vector<SavedNetwork> out{net("STALE", true, true)};
  CHECK(decodeSavedNetworks("", out));
  CHECK(out.empty());
}

TEST_CASE("every flag combination round-trips") {
  for (const bool locked : {false, true}) {
    for (const bool automatic : {false, true}) {
      const std::vector<SavedNetwork> in{net("N", locked, automatic)};
      const std::vector<SavedNetwork> out = roundTrip(in);
      REQUIRE(out.size() == 1);
      CHECK(out[0].ssid == "N");
      CHECK(out[0].locked == locked);
      CHECK(out[0].automatic == automatic);
    }
  }
}

TEST_CASE("AN SSID MAY LEGALLY CONTAIN EVERY BYTE THIS FORMAT USES") {
  // 802.11 says an SSID is 0-32 arbitrary octets. A wire format a legal SSID
  // can break is not a format -- this is why the field is escaped at all.
  for (const char* ssid : {"a;b", "a:b", "a%b", "%3B", "a;b:c%d", "Le Fléau", "\x01\x02"}) {
    const std::vector<SavedNetwork> out = roundTrip({net(ssid, true, false)});
    REQUIRE(out.size() == 1);
    CHECK(out[0].ssid == ssid);
  }
}

TEST_CASE("UTF-8 passes through unescaped, so the record stays readable") {
  const std::string wire = encodeSavedNetworks({net("Café", false, false)});
  CHECK(wire == "Café:-");
}

TEST_CASE("a separator inside an SSID is escaped and does not become a field") {
  const std::string wire = encodeSavedNetworks({net("a;b", false, false)});
  CHECK(wire == "a%3Bb:-");
  const std::string colon = encodeSavedNetworks({net("a:b", false, false)});
  CHECK(colon == "a%3Ab:-");
}

// ------------------------------------------------------------------- refusals

TEST_CASE("a malformed record is refused WHOLE, never best-effort") {
  // The all-or-nothing rule decodeSessionStack already has: a field written by
  // something that is not this format says nothing good about its neighbours.
  for (const char* bad : {
           "HOME",            // no flag field
           "HOME:",           // empty flag field
           ":L",              // empty ssid
           "HOME:X",          // unknown flag letter
           "HOME:LL",         // a letter twice
           "HOME:LA;",        // trailing separator
           ";HOME:LA",        // leading separator
           "HOME:L;;BUREAU:L",// an empty entry
           "HOME:L;HOME:-",   // the same network twice
           "HOME:A;BUREAU:A", // two preferred networks name no order at all
           "a%:L",            // malformed escape
           "a%ZZb:L",         // malformed escape
           "a%00b:L",         // an escaped NUL would truncate our own payload
       }) {
    std::vector<SavedNetwork> out{net("STALE", true, true)};
    CAPTURE(bad);
    CHECK_FALSE(decodeSavedNetworks(bad, out));
    CHECK(out.empty());  // false leaves nothing behind
  }
}

TEST_CASE("null is refused rather than dereferenced") {
  std::vector<SavedNetwork> out;
  CHECK_FALSE(decodeSavedNetworks(nullptr, out));
  CHECK(out.empty());
}

TEST_CASE("a list longer than the cap is refused") {
  std::string wire;
  for (int i = 0; i < kMaxSavedNetworks; ++i) {
    if (!wire.empty()) wire += ';';
    wire += "N" + std::to_string(i) + ":-";
  }
  std::vector<SavedNetwork> out;
  REQUIRE(decodeSavedNetworks(wire.c_str(), out));
  CHECK(out.size() == static_cast<size_t>(kMaxSavedNetworks));

  wire += ";ONE-TOO-MANY:-";
  CHECK_FALSE(decodeSavedNetworks(wire.c_str(), out));
  CHECK(out.empty());
}

TEST_CASE("an over-long SSID is refused on the way in and skipped on the way out") {
  const std::string tooLong(kSsidMaxBytes + 1, 'x');
  // Out: encoding one would produce a record this decoder discards WHOLE,
  // taking the good entries with it, so it is left out instead.
  const std::string wire = encodeSavedNetworks({net("GOOD", false, true), net(tooLong, false, false)});
  CHECK(wire == "GOOD:A");
  // In: a record from somewhere else carrying one is refused.
  std::vector<SavedNetwork> out;
  CHECK_FALSE(decodeSavedNetworks((tooLong + ":-").c_str(), out));
}

TEST_CASE("an SSID of exactly the maximum is fine") {
  const std::string atLimit(kSsidMaxBytes, 'x');
  const std::vector<SavedNetwork> out = roundTrip({net(atLimit, true, true)});
  REQUIRE(out.size() == 1);
  CHECK(out[0].ssid == atLimit);
}

TEST_CASE("the derived buffer bound holds for the worst record that can exist") {
  // Every SSID the maximum length, every byte one that escapes to three
  // characters, every flag set, the list full.
  std::vector<SavedNetwork> nets;
  for (int i = 0; i < kMaxSavedNetworks; ++i) {
    // Distinct, so the duplicate rule does not fire, and still all-escaping.
    std::string ssid(kSsidMaxBytes - 1, ';');
    ssid += static_cast<char>('a' + i);
    nets.push_back(net(ssid, true, i == 0));
  }
  const std::string wire = encodeSavedNetworks(nets);
  CHECK(wire.size() + 1 <= savedNetworksMaxBytes());
  std::vector<SavedNetwork> out;
  REQUIRE(decodeSavedNetworks(wire.c_str(), out));
  CHECK(out.size() == static_cast<size_t>(kMaxSavedNetworks));
}

// ----------------------------------------------------------------- secret key

TEST_CASE("a secret key is stable, short enough for NVS, and per SSID") {
  CHECK(wifiSecretKey("HOME") == wifiSecretKey("HOME"));
  CHECK(wifiSecretKey("HOME") != wifiSecretKey("BUREAU"));
  // NVS caps a key at 15 characters; this is `p_` plus eight hex.
  CHECK(wifiSecretKey("HOME").size() == 10);
  CHECK(wifiSecretKey(std::string(kSsidMaxBytes, 'x')).size() == 10);
  CHECK(wifiSecretKey("").size() == 10);
  // Keyed by the SSID's BYTES, so case matters -- two APs differing only in
  // case are two networks.
  CHECK(wifiSecretKey("home") != wifiSecretKey("HOME"));
}

TEST_CASE("the key is a hash of the NAME, so forgetting one network moves no other") {
  // The whole reason it is not an index: with index naming, forgetting the
  // second of eight renumbers six keys and a crash mid-rewrite attaches
  // passphrases to the wrong networks.
  const std::string before = wifiSecretKey("CAFE");
  SavedNetworks s;
  REQUIRE(s.remember("HOME", true));
  REQUIRE(s.remember("BUREAU", true));
  REQUIRE(s.remember("CAFE", true));
  REQUIRE(s.forget("BUREAU"));
  CHECK(wifiSecretKey("CAFE") == before);
}

// ------------------------------------------------------------- the list rules

TEST_CASE("THE FIRST NETWORK SAVED BECOMES THE PREFERRED ONE") {
  // Otherwise a one-network device shows a flag that reads as broken.
  SavedNetworks s;
  REQUIRE(s.remember("HOME", true));
  REQUIRE(s.automatic() != nullptr);
  CHECK(s.automatic()->ssid == "HOME");
  // A later one does not take it without being asked.
  REQUIRE(s.remember("BUREAU", true));
  CHECK(s.automatic()->ssid == "HOME");
}

TEST_CASE("at most one network is ever preferred") {
  SavedNetworks s;
  REQUIRE(s.remember("HOME", true));
  REQUIRE(s.remember("BUREAU", true));
  REQUIRE(s.makeAutomatic("BUREAU"));
  int autos = 0;
  for (const SavedNetwork& n : s.all()) autos += n.automatic ? 1 : 0;
  CHECK(autos == 1);
  CHECK(s.automatic()->ssid == "BUREAU");
  // Already preferred: nothing to do, and it says so.
  CHECK_FALSE(s.makeAutomatic("BUREAU"));
  CHECK_FALSE(s.makeAutomatic("ABSENT"));
}

TEST_CASE("the constructor clamps a record that somehow carries two") {
  // Belt to decodeSavedNetworks' braces: the invariant is the class's, so it
  // holds however the vector was built.
  SavedNetworks s({net("A", false, true), net("B", false, true), net("C", false, true)});
  int autos = 0;
  for (const SavedNetwork& n : s.all()) autos += n.automatic ? 1 : 0;
  CHECK(autos == 1);
  CHECK(s.automatic()->ssid == "A");
}

TEST_CASE("SELECT toggles preferred on and off, and off is a legal resting state") {
  SavedNetworks s;
  REQUIRE(s.remember("HOME", true));
  REQUIRE(s.remember("BUREAU", true));
  CHECK(s.toggleAutomatic("HOME"));           // HOME was preferred
  CHECK(s.automatic() == nullptr);            // zero is legal
  CHECK(s.toggleAutomatic("BUREAU"));
  CHECK(s.automatic()->ssid == "BUREAU");
  CHECK(s.toggleAutomatic("HOME"));           // takes it from BUREAU
  CHECK(s.automatic()->ssid == "HOME");
  CHECK_FALSE(s.toggleAutomatic("ABSENT"));
}

TEST_CASE("FORGETTING THE PREFERRED NETWORK PROMOTES NOTHING") {
  // A preference belongs to the user. Changing it behind their back with
  // nothing on the glass to say so is the false-claim shape this project
  // refuses for the battery gauge and the sleep badge.
  SavedNetworks s;
  REQUIRE(s.remember("HOME", true));
  REQUIRE(s.remember("BUREAU", true));
  REQUIRE(s.forget("HOME"));
  CHECK(s.size() == 1);
  CHECK(s.automatic() == nullptr);
  CHECK_FALSE(s.forget("HOME"));
}

TEST_CASE("the cap refuses the ninth rather than evicting the oldest") {
  // Evicting silently loses a passphrase the user typed and has no way to say
  // so.
  SavedNetworks s;
  for (int i = 0; i < kMaxSavedNetworks; ++i) {
    REQUIRE(s.remember("N" + std::to_string(i), true));
  }
  CHECK(s.full());
  CHECK_FALSE(s.remember("ONE-TOO-MANY", true));
  CHECK(s.size() == kMaxSavedNetworks);
  CHECK(s.indexOf("N0") == 0);  // the oldest is still here
}

TEST_CASE("a full list still accepts a network it already knows") {
  // Re-joining a known network is not an add, so the cap must not refuse it --
  // and it may have been secured since, which is the flag that updates.
  SavedNetworks s;
  for (int i = 0; i < kMaxSavedNetworks; ++i) {
    REQUIRE(s.remember("N" + std::to_string(i), false));
  }
  REQUIRE(s.full());
  CHECK(s.remember("N3", true));
  CHECK(s.size() == kMaxSavedNetworks);
  CHECK(s.all()[3].locked);
}

TEST_CASE("an unusable SSID is refused rather than stored") {
  SavedNetworks s;
  CHECK_FALSE(s.remember("", true));
  CHECK_FALSE(s.remember(std::string(kSsidMaxBytes + 1, 'x'), true));
  CHECK(s.size() == 0);
}

TEST_CASE("LOCKED IMPLIES A SECRET EXISTS, and the records that contradict it go") {
  // A locked network with no passphrase cannot be joined and is not something
  // the user can fix from this screen -- showing it is offering a row that can
  // only fail. The two halves live in different NVS keys, so only a read can
  // see both.
  Secrets probe;
  probe.have = {"HOME", "CAFE"};
  SavedNetworks s({net("HOME", true, true),     // locked, has one -- stays
                   net("BUREAU", true, false),  // locked, has none -- goes
                   net("OPEN", false, false),   // open, needs none -- stays
                   net("CAFE", true, false)});
  CHECK(s.dropLockedWithoutSecret(probe) == 1);
  CHECK(s.size() == 3);
  CHECK(s.indexOf("BUREAU") == -1);
  CHECK(s.indexOf("HOME") == 0);
  CHECK(s.indexOf("OPEN") == 1);
  CHECK(s.indexOf("CAFE") == 2);
}

TEST_CASE("dropping the preferred network leaves zero preferred, not a promotion") {
  Secrets probe;  // nothing has a secret
  SavedNetworks s({net("HOME", true, true), net("OPEN", false, false)});
  CHECK(s.dropLockedWithoutSecret(probe) == 1);
  CHECK(s.size() == 1);
  CHECK(s.automatic() == nullptr);
}

TEST_CASE("an open network is never dropped for want of a secret") {
  Secrets probe;
  SavedNetworks s({net("A", false, true), net("B", false, false)});
  CHECK(s.dropLockedWithoutSecret(probe) == 0);
  CHECK(s.size() == 2);
}

TEST_CASE("the whole list survives a trip through the wire and back") {
  // The property that matters on a device: what is saved is what comes back.
  SavedNetworks s;
  REQUIRE(s.remember("HOME", true));
  REQUIRE(s.remember("a;b:c%d", false));
  REQUIRE(s.remember("Le Fléau", true));
  REQUIRE(s.makeAutomatic("Le Fléau"));

  std::vector<SavedNetwork> decoded;
  REQUIRE(decodeSavedNetworks(encodeSavedNetworks(s.all()).c_str(), decoded));
  const SavedNetworks back(std::move(decoded));
  REQUIRE(back.size() == s.size());
  for (int i = 0; i < s.size(); ++i) {
    CHECK(back.all()[i].ssid == s.all()[i].ssid);
    CHECK(back.all()[i].locked == s.all()[i].locked);
    CHECK(back.all()[i].automatic == s.all()[i].automatic);
  }
  REQUIRE(back.automatic() != nullptr);
  CHECK(back.automatic()->ssid == "Le Fléau");
}
