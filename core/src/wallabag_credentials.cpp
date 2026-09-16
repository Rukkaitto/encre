#include "reader/wallabag_credentials.h"

#include "reader/json.h"

namespace reader {
namespace {

constexpr const char* kKeys[] = {"server", "clientId", "clientSecret", "username", "password"};

// THE THREE FACTS A READER CANNOT GET ANYWHERE ELSE, in the one place they are
// certain to look: the file they have just been told to open.
//
// ArticlesSetup.dc.html says "fill in your Wallabag details in its
// /.reader/wallabag.json file" and has no room to say more, the device has no
// keyboard and so can never show a URL worth typing, and `clientId` and
// `clientSecret` are the two fields nobody can GUESS -- they come from a page
// called /developer/client/create, and nothing on the glass says so. Without
// these lines the seeded file is five empty strings and the reader is stuck at
// step one with a correct device.
//
// THEY SORT TO THE TOP BECAUSE dump() ORDERS BY KEY, not by insertion: values_
// is a std::map, so an uppercase README lands ahead of every lowercase field
// name. This is also why the comment that used to sit above kKeys -- "the order
// is the file's" -- was wrong about the file it described.
//
// EACH ONE IS UNDER kJsonMaxStringBytes, AND THAT IS THE TRAP THIS GUARDS: a
// value over 256 decoded bytes makes parse() refuse the WHOLE object, so a help
// line one word too long would make the file read `is not a flat JSON object`
// and the screen would blame the reader's typing for our copy. A static_assert
// rather than a test, because the cost of being wrong lands on a card.
constexpr char kHelp1[] =
    "server is your Wallabag's address. No Wallabag yet? wallabag.it is about 11 euros a year "
    "and has a 14-day trial.";
constexpr char kHelp2[] =
    "clientId and clientSecret come from an API client you make at "
    "<server>/developer/client/create. Leave the redirect URI blank, and name it, so you can "
    "revoke this reader on its own.";
constexpr char kHelp3[] =
    "Lost the secret? It is still at <server>/developer. username and password are the ones you "
    "sign in with.";
static_assert(sizeof(kHelp1) - 1 < kJsonMaxStringBytes, "a help line over the cap breaks the file");
static_assert(sizeof(kHelp2) - 1 < kJsonMaxStringBytes, "a help line over the cap breaks the file");
static_assert(sizeof(kHelp3) - 1 < kJsonMaxStringBytes, "a help line over the cap breaks the file");
constexpr const char* kHelp[][2] = {
    {"README 1", kHelp1}, {"README 2", kHelp2}, {"README 3", kHelp3}};

std::string normaliseServer(std::string v) {
  while (!v.empty() && v.back() == '/') v.pop_back();
  if (v.empty()) return v;
  // A SCHEME IS ADDED ONLY WHEN THERE IS NONE. `https://` is kept -- the probe
  // in #140 measures whether TLS fits at all, and until it answers, a reader who
  // typed https has said something this code must not quietly undo.
  const bool hasScheme = v.find("://") != std::string::npos;
  if (!hasScheme) v = "http://" + v;
  return v;
}

}  // namespace

CredentialsResult loadWallabagCredentials(FileSystem& fs, WallabagCredentials& out,
                                          std::string& why) {
  out = WallabagCredentials{};
  if (!fs.exists(kWallabagCredentialsPath)) return CredentialsResult::Absent;

  std::string text;
  if (!fs.readAll(kWallabagCredentialsPath, text)) {
    why = "could not be read";
    return CredentialsResult::Malformed;
  }
  JsonObject obj;
  if (!obj.parse(text)) {
    why = "is not a flat JSON object";
    return CredentialsResult::Malformed;
  }

  std::string* fields[] = {&out.server, &out.clientId, &out.clientSecret, &out.username,
                           &out.password};
  for (int i = 0; i < 5; ++i) {
    // A MISSING KEY IS NOT AN ERROR, it is an empty value -- which makes it
    // Unconfigured below. A half-edited file and a seeded one are the same state
    // to a reader: they have not finished, and the screen says so rather than
    // raising a failure about a file they are in the middle of.
    std::string v;
    if (obj.getString(kKeys[i], v)) *fields[i] = std::move(v);
  }
  out.server = normaliseServer(std::move(out.server));
  return out.configured() ? CredentialsResult::Ok : CredentialsResult::Unconfigured;
}

bool seedWallabagCredentials(FileSystem& fs) {
  if (fs.exists(kWallabagCredentialsPath)) return false;
  JsonObject obj;
  for (const char* k : kKeys) obj.setString(k, "");
  // The help goes in the SEED alone. A file the reader has already edited is
  // never rewritten, so nothing here can appear in, or disappear from, a file
  // somebody is in the middle of.
  for (const auto& h : kHelp) obj.setString(h[0], h[1]);
  // mkdirs first: on a fresh card /.reader does not exist, and writeAll does not
  // create the path to its own file. The settings seed does the same.
  fs.mkdirs("/.reader");
  return fs.writeAll(kWallabagCredentialsPath, obj.dump());
}

}  // namespace reader
