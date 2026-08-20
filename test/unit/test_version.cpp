#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "reader/version.h"

TEST_CASE("core reports its version") {
  CHECK(std::string(reader::kVersion) == "0.1.0");
}
