#include "doctest.h"
#include "reader/refresh.h"

using namespace reader;

TEST_CASE("a transition is full and resets the cadence when the policy says so") {
  RefreshPolicy p(15, true);
  CHECK(p.fullOnTransition());
  CHECK(p.next(true) == RefreshMode::Full);
  CHECK(p.sinceFull() == 0);
  for (int i = 0; i < 5; ++i) CHECK(p.next(false) == RefreshMode::Fast);
  CHECK(p.sinceFull() == 5);
  CHECK(p.next(true) == RefreshMode::Full);
  CHECK(p.sinceFull() == 0);
}

TEST_CASE("with fullOnTransition clear, a transition is an ordinary fast refresh") {
  // What chrome constructs. The reference firmware does not full-refresh a screen
  // change on this panel, and the flash was what the user objected to.
  RefreshPolicy p(4, false);
  CHECK_FALSE(p.fullOnTransition());
  CHECK(p.next(true) == RefreshMode::Fast);
  // ...and it did NOT reset the cadence. A FAST screen change leaves the same
  // residue behind as any other FAST refresh, so it cannot count as a clean
  // slate; it counts as one of the four.
  CHECK(p.sinceFull() == 1);
  CHECK(p.next(true) == RefreshMode::Fast);
  CHECK(p.sinceFull() == 2);
  CHECK(p.next(false) == RefreshMode::Fast);
  CHECK(p.sinceFull() == 3);
  // The fourth refresh is the cadence's FULL whether or not it is a transition,
  // which is what proves the transitions above were counted rather than skipped.
  CHECK(p.next(true) == RefreshMode::Full);
  CHECK(p.sinceFull() == 0);
}

TEST_CASE("fullOnTransition clear does not disable the cadence") {
  // Nothing but transitions, forever: the periodic FULL must still arrive, or a
  // long run of navigation would never clear its accumulated ghosting. This
  // project has already had ink build up on this panel once.
  RefreshPolicy p(15, false);
  int fulls = 0;
  for (int i = 0; i < 45; ++i)
    if (p.next(true) == RefreshMode::Full) ++fulls;
  CHECK(fulls == 3);
}

TEST_CASE("fullOnTransition defaults to true") {
  // The flag is opt-out, so a policy built the old one-argument way behaves
  // exactly as it did before the flag existed.
  RefreshPolicy p(15);
  CHECK(p.fullOnTransition());
  CHECK(p.next(true) == RefreshMode::Full);
  CHECK(p.sinceFull() == 0);
}

TEST_CASE("a cadence of one or less is full even with fullOnTransition clear") {
  // The two settings are independent: switching the transition FULL off must not
  // smuggle a FAST refresh into a policy that asked for every refresh to be FULL.
  RefreshPolicy p(1, false);
  CHECK(p.next(true) == RefreshMode::Full);
  CHECK(p.next(false) == RefreshMode::Full);
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

TEST_CASE("a cadence of exactly one makes every refresh full") {
  RefreshPolicy p(1);
  CHECK(p.next(false) == RefreshMode::Full);
  CHECK(p.next(false) == RefreshMode::Full);
}

TEST_CASE("a cadence of zero or below never schedules a periodic full") {
  // 0 and negatives used to fold into "always FULL", which put the two ends of
  // the range on the same value and left no way to say "never". They are now the
  // NEVER end: kNever exists so a caller that wants no periodic flash can say so
  // instead of picking an implausibly large number.
  for (const int c : {RefreshPolicy::kNever, -3}) {
    RefreshPolicy p(c);
    for (int i = 0; i < 50; ++i) CHECK(p.next(false) == RefreshMode::Fast);
    // Still counting, so the log can report how long it has been.
    CHECK(p.sinceFull() == 50);
  }
}

TEST_CASE("a disabled cadence still honours fullOnTransition") {
  // The two settings stay independent in both directions: turning the periodic
  // full off must not also silence a transition that asked to be FULL.
  RefreshPolicy p(RefreshPolicy::kNever, true);
  CHECK(p.next(true) == RefreshMode::Full);
  CHECK(p.next(false) == RefreshMode::Fast);
}

TEST_CASE("the default cadence is the spec's fifteen") {
  RefreshPolicy p;
  CHECK(p.cadence() == 15);
  for (int i = 0; i < 14; ++i) CHECK(p.next(false) == RefreshMode::Fast);
  CHECK(p.next(false) == RefreshMode::Full);
}
