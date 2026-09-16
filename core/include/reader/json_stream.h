#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

#include "reader/inflate_stream.h"  // ByteSource

namespace reader {

// A BOUNDED PULL SCANNER FOR NESTED JSON, over the same ByteSource xml.h reads.
//
// WHY IT EXISTS: json.h reads ONE FLAT OBJECT. A wallabag listing page is an
// object holding `_embedded.items`, an ARRAY of objects each holding a `tags`
// array. Nothing in this repo can read that, and vendoring a parser is refused
// here for the reason the DEFLATE decoder and the two XML layers were written
// rather than vendored: what arrives is bytes off somebody's server, and every
// layer on that path has to refuse bad input with a reason rather than abort.
//
// PULL, NOT A TREE. A tree of a 32 KB page would be the thing the whole reader
// exists to avoid -- a document held whole -- and the consumer wants five fields
// out of each of twenty entries. One bounded string buffer is the only
// allocation.
//
// BOUNDS ARE THE GRAMMAR, and they answer differently on purpose:
//
//   a string past kJsonMaxStringBytes is TRUNCATED at a UTF-8 boundary and
//     flagged, never an error -- a long title is CONTENT, and cutting it is the
//     Library's own ellipsis one layer up. Refusing the page over a title would
//     lose nineteen articles to one.
//   nesting past kJsonMaxDepth is an ERROR -- that is structure rather than
//     content, and a document that deep is not the shape this reads.
class JsonScanner {
 public:
  enum class Token {
    ObjectStart,
    ObjectEnd,
    ArrayStart,
    ArrayEnd,
    Key,     // `key()` is the name; the VALUE follows as the next token
    String,  // `text()`
    Number,  // `text()` raw, and `number()` as an integer
    Bool,    // `boolean()`
    Null,
    End,    // the input ended cleanly
    Error,  // and nothing may be read after this
  };

  explicit JsonScanner(ByteSource& src) : src_(src) {}

  Token next();

  const std::string& key() const { return key_; }
  const std::string& text() const { return text_; }
  bool boolean() const { return bool_; }
  int64_t number() const { return num_; }

  // Whether the LAST string handed back was cut. Per token, so a consumer can
  // flag one field without the page being suspect.
  bool truncated() const { return truncated_; }
  // How many strings have been cut over this scan, so a caller can say so once
  // rather than per field. Counted rather than silent: a hole in what the
  // firmware knows must not read as a server that sent less.
  int truncations() const { return truncations_; }

  // Skip the value that STARTS at the next token, nested or not. What lets a
  // consumer ignore `tags` and `preview_picture` without knowing their shape.
  // False on Error.
  bool skipValue();

  const std::string& error() const { return error_; }

 private:
  int get();          // one byte, or -1
  int peekNonSpace();
  bool expect(char c);
  bool readString(std::string& out);
  bool readAtom(const char* word);
  bool readNumber(int c);
  Token fail(const char* why);

  ByteSource& src_;
  std::string key_;
  std::string text_;
  std::string error_;
  int64_t num_ = 0;
  bool bool_ = false;
  bool truncated_ = false;
  int truncations_ = 0;
  int depth_ = 0;
  // ONE BYTE OF PUSHBACK, which is all a JSON scanner needs: every token but a
  // number ends on a delimiter it consumes, and a number ends on one it does not.
  int pushed_ = -1;
  bool ended_ = false;
  bool failed_ = false;
  // Whether the next string at this depth is a KEY. An object alternates key and
  // value; an array does not, which is the whole of the difference.
  bool inObject_[9] = {false};
  bool wantKey_ = false;
  // A KEY MUST BE FOLLOWED BY A VALUE. Without this `{"a":}` parsed as a clean
  // object -- the separator was skipped, the `}` closed the object, and a
  // document missing a value read as one that never had the key. A listing whose
  // `id` went that way would parse to an entry with id 0.
  bool expectValue_ = false;
};

inline constexpr size_t kJsonStreamMaxStringBytes = 256;  // json.h's own bound
inline constexpr int kJsonMaxDepth = 8;

}  // namespace reader
