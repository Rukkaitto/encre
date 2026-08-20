#include "doctest.h"
#include "reader/refresh.h"

using namespace reader;

TEST_CASE("a transition is always full and resets the cadence") {
  RefreshPolicy p(15);
  CHECK(p.next(true) == RefreshMode::Full);
  CHECK(p.sinceFull() == 0);
  for (int i = 0; i < 5; ++i) CHECK(p.next(false) == RefreshMode::Fast);
  CHECK(p.sinceFull() == 5);
  CHECK(p.next(true) == RefreshMode::Full);
  CHECK(p.sinceFull() == 0);
}

TEST_CASE("cadence N gives N-1 fast refreshes then a full one") {
  RefreshPolicy p(4);
  CHECK(p.next(false) == RefreshMode::Fast);
  CHECK(p.next(false) == RefreshMode::Fast);
  CHECK(p.next(false) == RefreshMode::Fast);
  CHECK(p.next(false) == RefreshMode::Full);
  CHECK(p.sinceFull() == 0);
  // and the pattern repeats, rather than sticking on Full
  CHECK(p.next(false) == RefreshMode::Fast);
}

TEST_CASE("a cadence of one or less makes every refresh full") {
  for (const int c : {1, 0, -3}) {
    RefreshPolicy p(c);
    CHECK(p.next(false) == RefreshMode::Full);
    CHECK(p.next(false) == RefreshMode::Full);
  }
}

TEST_CASE("the default cadence is the spec's fifteen") {
  RefreshPolicy p;
  CHECK(p.cadence() == 15);
  for (int i = 0; i < 14; ++i) CHECK(p.next(false) == RefreshMode::Fast);
  CHECK(p.next(false) == RefreshMode::Full);
}
