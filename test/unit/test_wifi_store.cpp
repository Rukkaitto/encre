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
  // AND IT IS TIGHT, which is the half that was missing. Named "for the worst
  // record that can exist", the fixture above is not quite it: one byte of
  // each SSID is a non-escaping letter and only the first entry carries `A`,
  // so it came in 23 bytes under the bound -- enough slack that dropping the
  // `+ 1 /*NUL*/` from the derivation failed nothing, and a caller sizing a
  // read buffer from this number could have been one byte short with the
  // suite green.
  //
  // The gap is stated rather than closed by making the fixture worse: the
  // distinguishing letter is what keeps the duplicate rule from firing, and
  // one `A` is what the exclusivity rule permits. So the bound is asserted
  // against what it is DERIVED from instead, which no fixture can slacken.
  // `(escaped SSID + ':' + "LA") * 8`, seven separators, and the NUL -- the
  // derivation spelled out, so the `+ 1` has something asserting it.
  const size_t entry = static_cast<size_t>(kSsidMaxBytes) * 3 + 1 + 2;
  CHECK(savedNetworksMaxBytes() ==
        entry * kMaxSavedNetworks + (kMaxSavedNetworks - 1) + 1);
  CHECK(savedNetworksMaxBytes() == 800);
  std::vector<SavedNetwork> out;
  REQUIRE(decodeSavedNetworks(wire.c_str(), out));
  CHECK(out.size() == static_cast<size_t>(kMaxSavedNetworks));
}

// ----------------------------------------------------------------- secret key

TEST_CASE("a secret key's VALUE is pinned, not just its shape") {
  // THE FOUR MUTATIONS THAT SURVIVED EVERY OTHER ASSERTION HERE: emitting the
  // nibbles in reverse, changing the FNV offset basis, changing the FNV prime,
  // and swapping the xor with the multiply. Each keeps the key deterministic,
  // ten characters and per-SSID -- and each RENAMES EVERY PASSPHRASE KEY IN
  // NVS. A firmware carrying one orphans every stored secret, after which
  // dropLockedWithoutSecret removes every locked network and the reader loses
  // the lot, silently, on one boot.
  //
  // A literal is the only thing that catches that, and it is cheap: this is a
  // STORAGE format, so it may not move without a version bump anyway -- the
  // same argument that makes the session record's screen names literals
  // rather than whatever screenName() happens to return.
  CHECK(wifiSecretKey("HOME") == "p_10b8c84e");
  CHECK(wifiSecretKey("BUREAU") == "p_53847247");
  CHECK(wifiSecretKey("") == "p_811c9dc5");
}

TEST_CASE("a secret key is stable, short enough for NVS, and per SSID") {
  // `wifiSecretKey("HOME") == wifiSecretKey("HOME")` USED TO STAND HERE and
  // was `f() == f()` -- failable only by non-determinism, which nothing in a
  // pure hash of a string can produce. Determinism is worth asserting and
  // this is how: the same input through two separately-built strings, where
  // a hash that read anything but its argument would differ.
  const std::string name = "HO" + std::string("ME");
  CHECK(wifiSecretKey(name) == wifiSecretKey("HOME"));
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
  //
  // THIS CASE USED TO BUILD A SavedNetworks, remember three networks, forget
  // one, and then check `wifiSecretKey("CAFE")` was unchanged -- and
  // wifiSecretKey is a FREE FUNCTION OF A STRING that cannot observe a list,
  // so NO mutation to SavedNetworks could ever fail it. The whole
  // remember/forget sequence was decorative.
  //
  // What it is really about is that the key does not depend on POSITION, and
  // the way to say that is to ask for the same name from two lists that
  // disagree about where it sits.
  SavedNetworks first;
  REQUIRE(first.remember("CAFE", true) == Remembered::Yes);
  REQUIRE(first.remember("HOME", true) == Remembered::Yes);
  REQUIRE(first.remember("BUREAU", true) == Remembered::Yes);

  SavedNetworks second;
  REQUIRE(second.remember("HOME", true) == Remembered::Yes);
  REQUIRE(second.remember("BUREAU", true) == Remembered::Yes);
  REQUIRE(second.remember("CAFE", true) == Remembered::Yes);
  REQUIRE(second.forget("BUREAU"));

  // Position 0 in one list and position 1 in the other, and the key is the
  // same because it is a function of the name alone.
  REQUIRE(first.all()[0].ssid == "CAFE");
  REQUIRE(second.all()[1].ssid == "CAFE");
  CHECK(wifiSecretKey(first.all()[0].ssid) == wifiSecretKey(second.all()[1].ssid));
}

// ------------------------------------------------------------- the list rules

TEST_CASE("THE FIRST NETWORK SAVED BECOMES THE PREFERRED ONE") {
  // Otherwise a one-network device shows a flag that reads as broken.
  SavedNetworks s;
  REQUIRE(s.remember("HOME", true) == Remembered::Yes);
  REQUIRE(s.automatic() != nullptr);
  CHECK(s.automatic()->ssid == "HOME");
  // A later one does not take it without being asked.
  REQUIRE(s.remember("BUREAU", true) == Remembered::Yes);
  CHECK(s.automatic()->ssid == "HOME");
}

TEST_CASE("at most one network is ever preferred") {
  SavedNetworks s;
  REQUIRE(s.remember("HOME", true) == Remembered::Yes);
  REQUIRE(s.remember("BUREAU", true) == Remembered::Yes);
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
  // COUNTED, NOT JUST NAMED. This asserted `automatic()->ssid == "HOME"` at
  // the end, and automatic() returns the FIRST flagged entry -- which is HOME
  // whether or not BUREAU kept its flag. So deleting toggleAutomatic's
  // clearing loop failed nothing, and the state it produces is not merely
  // untidy: the record such a device writes is `HOME:LA;BUREAU:LA`, which its
  // OWN DECODER refuses whole, so the next boot reads zero saved networks and
  // the reader has lost every one of them.
  //
  // makeAutomatic's identical clear IS caught, because the case above it
  // counts the flags. This one never counted.
  auto autos = [](const SavedNetworks& n) {
    int k = 0;
    for (const SavedNetwork& e : n.all()) k += e.automatic ? 1 : 0;
    return k;
  };
  SavedNetworks s;
  REQUIRE(s.remember("HOME", true) == Remembered::Yes);
  REQUIRE(s.remember("BUREAU", true) == Remembered::Yes);
  CHECK(s.toggleAutomatic("HOME"));           // HOME was preferred
  CHECK(s.automatic() == nullptr);            // zero is legal
  CHECK(autos(s) == 0);
  CHECK(s.toggleAutomatic("BUREAU"));
  CHECK(s.automatic()->ssid == "BUREAU");
  CHECK(autos(s) == 1);
  CHECK(s.toggleAutomatic("HOME"));           // takes it from BUREAU
  CHECK(s.automatic()->ssid == "HOME");
  CHECK(autos(s) == 1);
  CHECK_FALSE(s.toggleAutomatic("ABSENT"));

  // AND THE WIRE IS THE PROOF THAT MATTERS, because the consequence is not a
  // wrong flag on a screen, it is a record the decoder throws away.
  std::vector<SavedNetwork> back;
  CHECK(decodeSavedNetworks(encodeSavedNetworks(s.all()).c_str(), back));
  CHECK(back.size() == 2);
}

TEST_CASE("FORGETTING THE PREFERRED NETWORK PROMOTES NOTHING") {
  // A preference belongs to the user. Changing it behind their back with
  // nothing on the glass to say so is the false-claim shape this project
  // refuses for the battery gauge and the sleep badge.
  SavedNetworks s;
  REQUIRE(s.remember("HOME", true) == Remembered::Yes);
  REQUIRE(s.remember("BUREAU", true) == Remembered::Yes);
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
    REQUIRE(s.remember("N" + std::to_string(i), true) == Remembered::Yes);
  }
  CHECK(s.full());
  CHECK(s.remember("ONE-TOO-MANY", true) == Remembered::ListFull);
  CHECK(s.size() == kMaxSavedNetworks);
  CHECK(s.indexOf("N0") == 0);  // the oldest is still here
}

TEST_CASE("the refusal NAMES ITSELF, because `false` was two different facts (#162)") {
  // The ninth join that SUCCEEDED was silently not saved: the one caller read
  // remember() as a statement, wrote the passphrase anyway and saved the
  // unchanged eight. The fix has to reach the glass, and a dialog saying the
  // list is full is a LIE about an SSID no list would have taken -- so the two
  // refusals cannot arrive as the same answer.
  SavedNetworks s;
  for (int i = 0; i < kMaxSavedNetworks; ++i) {
    REQUIRE(s.remember("N" + std::to_string(i), true) == Remembered::Yes);
  }
  REQUIRE(s.full());

  // THE TWO REFUSALS ARE DISTINGUISHABLE, which is the whole point of the enum.
  CHECK(s.remember("ONE-TOO-MANY", true) == Remembered::ListFull);
  CHECK(s.remember("", true) == Remembered::BadSsid);
  CHECK(s.remember(std::string(kSsidMaxBytes + 1, 'x'), true) == Remembered::BadSsid);

  // AND A REFUSAL CHANGES NOTHING AT ALL. The caller's passphrase write is
  // conditional on the answer, so a refusal that half-mutated would leave a
  // network the next boot's dropLockedWithoutSecret has to clean up.
  CHECK(s.size() == kMaxSavedNetworks);
  CHECK(s.indexOf("ONE-TOO-MANY") == -1);
  CHECK(s.indexOf("N0") == 0);  // the oldest is still here
  REQUIRE(s.automatic() != nullptr);
  CHECK(s.automatic()->ssid == "N0");  // and still the preferred one
}

TEST_CASE("a full list still accepts a network it already knows") {
  // Re-joining a known network is not an add, so the cap must not refuse it --
  // and it may have been secured since, which is the flag that updates.
  SavedNetworks s;
  for (int i = 0; i < kMaxSavedNetworks; ++i) {
    REQUIRE(s.remember("N" + std::to_string(i), false) == Remembered::Yes);
  }
  REQUIRE(s.full());
  CHECK(s.remember("N3", true) == Remembered::Yes);
  CHECK(s.size() == kMaxSavedNetworks);
  CHECK(s.all()[3].locked);
}

TEST_CASE("an unusable SSID is refused rather than stored") {
  SavedNetworks s;
  CHECK(s.remember("", true) == Remembered::BadSsid);
  CHECK(s.remember(std::string(kSsidMaxBytes + 1, 'x'), true) == Remembered::BadSsid);
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

TEST_CASE("two ADJACENT droppable networks both go") {
  // THE FIXTURE ABOVE DROPS EXACTLY ONE, so the classic forward-erase skip is
  // unreachable and rewriting the loop to walk forward survived the whole
  // suite. What it leaves behind is precisely the row the function exists to
  // remove: a locked network with no passphrase, which cannot be joined and
  // which the user cannot fix from this screen.
  Secrets probe;  // nothing has a secret
  SavedNetworks s({net("A", true, true), net("B", true, false), net("C", true, false),
                   net("D", true, false)});
  CHECK(s.dropLockedWithoutSecret(probe) == 4);
  CHECK(s.size() == 0);

  // And the interleaved case, where a survivor sits between two casualties --
  // a forward walk skips the SECOND of each adjacent pair, so a run of three
  // separates "walks backward" from "erases carefully".
  Secrets some;
  some.have = {"KEEP"};
  SavedNetworks t({net("X", true, false), net("Y", true, false), net("KEEP", true, true),
                   net("Z", true, false), net("W", true, false)});
  CHECK(t.dropLockedWithoutSecret(some) == 4);
  REQUIRE(t.size() == 1);
  CHECK(t.all()[0].ssid == "KEEP");
}

TEST_CASE("a network is matched by its WHOLE name, never by a prefix") {
  // `indexOf` relaxed to a prefix match survived: remembering HOME while
  // HOMEOFFICE is saved would update the wrong network, and forgetting HOME
  // would forget it.
  SavedNetworks s;
  REQUIRE(s.remember("HOMEOFFICE", true) == Remembered::Yes);
  CHECK(s.indexOf("HOME") == -1);
  CHECK(s.indexOf("HOMEOFFICE") == 0);
  CHECK_FALSE(s.forget("HOME"));
  CHECK(s.size() == 1);
  // The other direction too: a saved name that is a prefix of the query.
  CHECK(s.indexOf("HOMEOFFICES") == -1);
}

TEST_CASE("the encoder's cap is the encoder's, because the constructor has none") {
  // `encodeSavedNetworks` is a free function taking a raw vector, and
  // SavedNetworks(std::vector) does NOT cap -- so this break is the only
  // guard, and deleting it survived. A ninth entry produces a record the
  // decoder refuses WHOLE, which is the toggle defect's failure mode again:
  // the next boot reads zero saved networks.
  std::vector<SavedNetwork> nine;
  for (int i = 0; i < kMaxSavedNetworks + 1; ++i)
    nine.push_back(net(std::string("N") + static_cast<char>('a' + i), true, i == 0));
  REQUIRE(nine.size() == static_cast<size_t>(kMaxSavedNetworks) + 1);

  const std::string wire = encodeSavedNetworks(nine);
  std::vector<SavedNetwork> back;
  REQUIRE(decodeSavedNetworks(wire.c_str(), back));
  CHECK(back.size() == static_cast<size_t>(kMaxSavedNetworks));
  // The NINTH is the one dropped -- the cap refuses the last rather than
  // evicting the first, which is `remember`'s own rule.
  CHECK(back.back().ssid == nine[kMaxSavedNetworks - 1].ssid);
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
  REQUIRE(s.remember("HOME", true) == Remembered::Yes);
  REQUIRE(s.remember("a;b:c%d", false) == Remembered::Yes);
  REQUIRE(s.remember("Le Fléau", true) == Remembered::Yes);
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
