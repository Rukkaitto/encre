#pragma once
#include <string>

#include "reader/filesystem.h"

namespace reader {

// /.reader/wallabag.json -- five hand-edited strings, and the whole of how an
// account reaches this device.
//
// IT IS `logToCard`'s PRECEDENT: a hand-edited card key with no Settings row,
// needing no mechanism this firmware does not have -- `readAll` plus the flat
// one-object parser settings.json already uses. It closes the alternatives
// rather than deferring them: no HTTP server, no keyboard for this, no password
// on glass and none over plain HTTP on a LAN.
//
// THE PASSWORD STAYS IN THE FILE, and that is evidenced rather than assumed.
// wallabag's own browser extension documents its token as expiring "once in two
// weeks", so the refresh token is good for about a fortnight -- and this device
// sleeps for days at a time. A reader who does not sync for three weeks needs a
// fresh `grant_type=password`, and there is nobody standing in front of the
// panel to ask. Deleting the password after first use would strand exactly that
// reader.
//
// THE STATED COST IS PLAINTEXT ON A REMOVABLE CARD, AND IT IS NOT BOUNDED BY
// ANYTHING CLEVER. What a lost card gives up is one wallabag account: the file
// carries the username and the password, so the `password` grant is available to
// whoever holds it and no grant-type narrowing changes that. What bounds it is
// the deployment -- the reader's own card, their own server, and an account
// whose blast radius is their reading list. wallabag's own ecosystem makes the
// same trade and says so: Wallabagger's documentation carries a "Security
// warning -- your password is stored in the browser local storage as a plain
// text". A precedent, not a defence.
struct WallabagCredentials {
  std::string server;
  std::string clientId;
  std::string clientSecret;
  std::string username;
  std::string password;

  // ALL FIVE, NON-EMPTY. A seeded file with blank values is the NORMAL state of
  // a device nobody has set up, so it is `unconfigured` and never an error --
  // which is what lets the Articles list draw its not-set-up variant rather than
  // a failure dialog.
  bool configured() const {
    return !server.empty() && !clientId.empty() && !clientSecret.empty() && !username.empty() &&
           !password.empty();
  }
};

// WHY A LOAD DID NOT PRODUCE CREDENTIALS, and the three answers are three
// different things the device must say differently:
//
//   Absent      no file. The card has never been set up, or this is a fresh one.
//   Unconfigured  a file with a value missing. The seed, or a half-finished edit.
//   Malformed   the bytes are not the object this expects.
//
// `Absent` and `Unconfigured` are the SAME thing to a screen -- both draw the
// not-set-up variant -- and different things to the SEED, which writes over
// neither. Malformed is the one the log has to name, because the reader's edit
// is the only copy of it and a silent overwrite would destroy it.
enum class CredentialsResult { Ok, Absent, Unconfigured, Malformed };

inline constexpr const char* kWallabagCredentialsPath = "/.reader/wallabag.json";

// `why` takes a reason for the log on Malformed, and is left alone otherwise.
//
// THE SERVER IS NORMALISED and the other four are taken verbatim. A trailing
// slash is dropped (every path this client builds begins with one, and `//api`
// is a different URL on some servers) and a value with no scheme gets `http://`
// -- which is the LAN case #139 says is the common one. An explicit `https://`
// is kept.
//
// A `\uXXXX` ESCAPE READS AS MALFORMED, because the flat parser does not decode
// one -- so an accented password must be typed as UTF-8 rather than escaped.
// That is stated here because the file is hand-edited and nothing else will say
// it.
CredentialsResult loadWallabagCredentials(FileSystem& fs, WallabagCredentials& out,
                                          std::string& why);

// Write the five keys with EMPTY values, ONLY when the file is absent.
//
// Returns whether it wrote. It never touches an existing file however malformed,
// which is `loadAndApplySettings`' rule and for its reason: the reader's edit is
// the only copy of itself, and a device that rewrites a file it could not parse
// destroys the thing it was meant to help fix.
bool seedWallabagCredentials(FileSystem& fs);

}  // namespace reader
