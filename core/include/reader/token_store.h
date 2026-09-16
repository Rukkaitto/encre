#pragma once
#include <string>
#include <string_view>

namespace reader {

// WHERE THE TOKENS LIVE, which is NVS on the device and a struct in the tests.
//
// AN INTERFACE FOR ScreenFactory's AND SettingsSink's REASON: -fno-exceptions
// makes a std::function's allocation an abort() with no diagnostic, and this is
// on the path a failed sync takes.
//
// NOT ON THE CARD, and that is the one design decision here. The credentials are
// a hand-edited file because a person has to write them; a token is machinery,
// it changes on its own, and writing it to the card would put a rotating secret
// in a file the reader is invited to edit -- and cost an SD write on the
// display's own SPI bus every hour. NVS is where the session record already
// lives for the same reason.
class TokenStore {
 public:
  virtual ~TokenStore() = default;
  // False when there is nothing stored, which is the state a first sync is in.
  virtual bool load(std::string& access, std::string& refresh) = 0;
  virtual bool save(std::string_view access, std::string_view refresh) = 0;
  // After a refusal. A stored token that the server has stopped honouring is
  // worse than none: it costs a round trip and a refresh before every call.
  virtual void clear() = 0;
};

}  // namespace reader
