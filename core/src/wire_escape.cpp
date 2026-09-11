#include "reader/wire_escape.h"

namespace reader {
namespace {

bool isHexDigit(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return c - 'A' + 10;
}

}  // namespace

std::string escapeWireField(std::string_view raw) {
  static const char* const kHex = "0123456789ABCDEF";
  std::string out;
  out.reserve(raw.size());
  for (const char ch : raw) {
    const unsigned char c = static_cast<unsigned char>(ch);
    if (c == '%' || c == ';' || c == ':' || c < 0x20 || c == 0x7F) {
      out += '%';
      out += kHex[c >> 4];
      out += kHex[c & 0x0F];
    } else {
      out += static_cast<char>(c);
    }
  }
  return out;
}

// EVERY FALSE PATH CLEARS `out`, and that is not decoration. This function
// decoded in place and returned false part-way, leaving the prefix it had
// managed behind -- so `decode("good%ZZ", out)` came back false with "good" in
// `out` while this header promised it empty. Both callers happen to clear their
// own output on a refusal, so nothing ever escaped; the contract was simply a
// claim the code did not keep, which is the defect class this project records
// most often. Found by giving the primitive its own test, which is the argument
// for having one.
bool decodeWireField(const char* start, size_t len, std::string& out) {
  out.clear();
  if (len == 0) return false;
  out.reserve(len);
  for (size_t i = 0; i < len; ++i) {
    const char c = start[i];
    if (c == '%') {
      if (i + 2 >= len || !isHexDigit(start[i + 1]) || !isHexDigit(start[i + 2])) {
        out.clear();
        return false;
      }
      const int v = hexValue(start[i + 1]) * 16 + hexValue(start[i + 2]);
      // A NUL would truncate the record's own string the next time it is
      // written, so it is refused rather than carried. See the header: this is
      // reachable from an SSID, which a path could not produce.
      if (v == 0) {
        out.clear();
        return false;
      }
      out += static_cast<char>(v);
      i += 2;
      continue;
    }
    // A RAW SEPARATOR HERE IS AN EXTRA FIELD, and there is no extra field: the
    // escape puts both beyond the parser's reach, so `library:7:/books:extra`
    // is malformed rather than a path with a colon in it.
    if (c == ':' || c == ';') {
      out.clear();
      return false;
    }
    out += c;
  }
  return true;
}

}  // namespace reader
