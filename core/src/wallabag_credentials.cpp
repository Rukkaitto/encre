#include "reader/wallabag_credentials.h"

#include "reader/json.h"

namespace reader {
namespace {

// THE ORDER IS THE FILE'S, so a seeded file reads the way the docs write it.
constexpr const char* kKeys[] = {"server", "clientId", "clientSecret", "username", "password"};

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
  // mkdirs first: on a fresh card /.reader does not exist, and writeAll does not
  // create the path to its own file. The settings seed does the same.
  fs.mkdirs("/.reader");
  return fs.writeAll(kWallabagCredentialsPath, obj.dump());
}

}  // namespace reader
