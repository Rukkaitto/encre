#pragma once
#include <cstddef>
#include <string>
#include <string_view>

namespace reader {

// PERCENT-ESCAPING FOR THE `a:b;c:d` RECORDS THIS FIRMWARE KEEPS IN NVS, in one
// place because there are now two of them.
//
// The session record escapes a Library path; the Wi-Fi store escapes an SSID.
// Both are bytes somebody else chose, both are carried inside a format whose
// separators are `:` and `;`, and both need a record a person can read off a
// device with `nvs_get`. It was written for the first and this is the second,
// which is where this project extracts rather than copies -- the alternative is
// the shape that gave it five copies of a clamp and four comments restating one
// rule to keep it one rule.
//
// THE THREE REFUSALS ARE THE SAME FOR BOTH, checked rather than assumed:
//
//   - EMPTY is refused, because both encoders write no field at all rather than
//     an empty one, so an empty field means a trailing separator and a
//     malformed record.
//   - AN ESCAPED NUL is refused. It cannot occur in a path; it CAN occur in an
//     SSID, which is 0-32 arbitrary octets -- and carrying one would truncate
//     our own C-string payload the next time it is written, so it is refused at
//     the boundary rather than smuggled in.
//   - A RAW `:` OR `;` is refused: the escape puts both beyond the parser's
//     reach, so one arriving unescaped is an extra field rather than content.
//
// EVERYTHING ELSE PASSES THROUGH, UTF-8 INCLUDED. That is deliberate and is the
// whole reason the format is not base64: `nvs_get encre_sess stack str`
// printing `library:7:/books/Le Fléau` is what makes a record diagnosable, and
// a record a person can read is one they can report.

// `%`, `;`, `:` and any control byte as `%XX`; everything else verbatim.
std::string escapeWireField(std::string_view raw);

// Decodes `len` bytes at `start`. False leaves `out` empty and means the record
// is malformed -- see the three refusals above. Callers treat that as "refuse
// the whole record", because a field written by something that is not this
// format says nothing good about the fields around it.
bool decodeWireField(const char* start, size_t len, std::string& out);

}  // namespace reader
