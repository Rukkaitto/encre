#include "reader/http_transport.h"

namespace reader {

bool BufferSink::write(const uint8_t* data, size_t n) {
  // REFUSED, NOT TRUNCATED. A listing that overran would parse as a valid prefix
  // and silently drop entries -- which is the reports-on-less-than-it-claims
  // shape this project keeps paying for -- where a refusal is one failed page
  // the sync can name.
  if (body_.size() + n > cap_) return false;
  body_.append(reinterpret_cast<const char*>(data), n);
  return true;
}

}  // namespace reader
