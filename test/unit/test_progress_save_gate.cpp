// SAVING THE READING POSITION ON EVERY PAGE TURN, WITHOUT HAMMERING THE CARD.
//
// The position used to be written on three edges -- leaving the book, crossing a
// chapter, sleeping -- so a power cut could cost a chapter of reading. Saving
// opportunistically in the shell's quiet window fixes that, and introduces two ways to
// get it wrong that `shell/` has no harness to catch:
//
//   * the quiet window fires every loop iteration, so an ungated save would re-read
//     both sidecars off the SPI bus hundreds of times a second to be told `Unchanged`;
//   * a card that is readable but refuses writes trips noteCardGone() on every failed
//     write, which routes to SdMissingScreen -- so multiplying the save rate by a
//     hundred multiplies the chance of throwing the reader out of a readable book.
//
// Both answers live in ProgressSaveGate, which is why they are testable at all.
#include "doctest.h"
#include "reader/progress_save_gate.h"

using reader::Cursor;
using reader::ProgressSaveGate;
using reader::SavePoint;

TEST_CASE("a gate with nothing stored wants the position written") {
  ProgressSaveGate g;
  CHECK(g.wants(SavePoint{0, 0, 0}, 1000));
  CHECK_FALSE(g.givenUp());
  CHECK(g.failures() == 0);
}

TEST_CASE("a stored position is not written again") {
  ProgressSaveGate g;
  const SavePoint here{3, 9, 2};
  REQUIRE(g.wants(here, 1000));
  g.noteStored(here);
  CHECK_FALSE(g.wants(here, 1000));
  // ...and not one second later either. The quiet window fires on every loop
  // iteration; nothing but the reader moving may re-arm this.
  CHECK_FALSE(g.wants(here, 2000));
  CHECK_FALSE(g.wants(here, 100000));
}

TEST_CASE("EVERY FIELD OF THE POSITION RE-ARMS THE GATE ON ITS OWN") {
  // A page turn inside one block moves `line`; a turn across one moves `block`; a
  // chapter crossing moves `spine`. Comparing on only one of the three would silently
  // stop saving for whole classes of turn.
  const SavePoint base{3, 9, 2};

  SUBCASE("the line moved") {
    ProgressSaveGate g;
    g.noteStored(base);
    CHECK(g.wants(SavePoint{3, 9, 3}, 1000));
  }
  SUBCASE("the block moved") {
    ProgressSaveGate g;
    g.noteStored(base);
    CHECK(g.wants(SavePoint{3, 10, 2}, 1000));
  }
  SUBCASE("the spine moved") {
    ProgressSaveGate g;
    g.noteStored(base);
    CHECK(g.wants(SavePoint{4, 9, 2}, 1000));
  }
}

TEST_CASE("a position built from a cursor is the same point as one built from ints") {
  Cursor at;
  at.block = 9;
  at.line = 2;
  CHECK(SavePoint{3, at} == SavePoint{3, 9, 2});
  CHECK(SavePoint{3, at} != SavePoint{3, 9, 3});
}

TEST_CASE("reading on past a stored page keeps re-arming the gate") {
  ProgressSaveGate g;
  SavePoint at{0, 0, 0};
  for (int page = 0; page < 20; ++page) {
    at.line = page;
    REQUIRE(g.wants(at, 1000));
    g.noteStored(at);
    CHECK_FALSE(g.wants(at, 1000));
  }
}

TEST_CASE("AN UNCHANGED SAVE COUNTS AS STORED") {
  // `savePosition` answers Unchanged when the card already holds these bytes -- which
  // is the same fact about the card as Written, reached by a different route. Treating
  // it as anything else would leave the gate asking forever.
  ProgressSaveGate g;
  const SavePoint here{1, 2, 3};
  g.noteStored(here);  // the caller maps both Written and Unchanged to this
  CHECK_FALSE(g.wants(here, 5000));
}

TEST_CASE("A FAILED SAVE BACKS OFF BEFORE IT IS RETRIED") {
  ProgressSaveGate g;
  const SavePoint here{1, 2, 3};
  REQUIRE(g.wants(here, 1000));
  g.noteFailed(1000);
  CHECK(g.failures() == 1);
  CHECK_FALSE(g.givenUp());

  // Not immediately, and not just short of the backoff -- otherwise the give-up budget
  // is spent in three consecutive loop iterations and the backoff is decorative.
  CHECK_FALSE(g.wants(here, 1000));
  CHECK_FALSE(g.wants(here, 1010));
  CHECK_FALSE(g.wants(here, 1000 + ProgressSaveGate::kRetryBackoffMs - 1));
  CHECK(g.wants(here, 1000 + ProgressSaveGate::kRetryBackoffMs));
}

TEST_CASE("A MOVING READER DOES NOT ESCAPE THE BACKOFF") {
  // The obvious bug: gate the backoff on the position being unchanged, so that turning
  // the page skips it. That would let a read-only card be hammered once per page turn,
  // which is exactly the case the backoff exists for.
  ProgressSaveGate g;
  g.noteFailed(1000);
  CHECK_FALSE(g.wants(SavePoint{9, 9, 9}, 1100));
  CHECK_FALSE(g.wants(SavePoint{9, 9, 10}, 1200));
}

TEST_CASE("THREE CONSECUTIVE FAILURES AND THE GATE GIVES UP FOR GOOD") {
  ProgressSaveGate g;
  SavePoint at{0, 0, 0};
  uint32_t now = 1000;
  for (int i = 0; i < ProgressSaveGate::kGiveUpAfterFailures; ++i) {
    REQUIRE(g.wants(at, now));
    g.noteFailed(now);
    now += ProgressSaveGate::kRetryBackoffMs;
  }
  CHECK(g.givenUp());
  CHECK(g.failures() == ProgressSaveGate::kGiveUpAfterFailures);

  // However long you wait, and however far the reader gets. A write-protected card
  // costs three extra attempts a session and then this mechanism is silent, leaving
  // the three original save edges -- which is the behaviour that shipped.
  CHECK_FALSE(g.wants(at, now + ProgressSaveGate::kRetryBackoffMs * 100));
  at.line = 500;
  CHECK_FALSE(g.wants(at, now + 1000000));
}

TEST_CASE("A SUCCESS CLEARS THE FAILURES, so transients do not accumulate into a give-up") {
  ProgressSaveGate g;
  g.noteFailed(1000);
  g.noteFailed(1000 + ProgressSaveGate::kRetryBackoffMs);
  CHECK(g.failures() == 2);

  g.noteStored(SavePoint{1, 1, 1});
  CHECK(g.failures() == 0);
  CHECK_FALSE(g.givenUp());
  // And the backoff is lifted with them: the next moved page is wanted at once.
  CHECK(g.wants(SavePoint{1, 1, 2}, 1000 + ProgressSaveGate::kRetryBackoffMs));
}

TEST_CASE("forgetting the book re-arms the gate for the same coordinates") {
  // Two books can both be at spine 0, block 0, line 0, and they are different files.
  ProgressSaveGate g;
  const SavePoint at{0, 0, 0};
  g.noteStored(at);
  REQUIRE_FALSE(g.wants(at, 1000));
  g.forget();
  CHECK(g.wants(at, 1000));
}

TEST_CASE("FORGETTING THE BOOK DOES NOT RE-ARM A CARD THAT HAS GIVEN UP") {
  // Giving up is a fact about the card, not the book. Clearing it per book would hand
  // a read-only card three fresh attempts for every book opened, turning a give-up
  // into a pause.
  ProgressSaveGate g;
  uint32_t now = 1000;
  for (int i = 0; i < ProgressSaveGate::kGiveUpAfterFailures; ++i) {
    g.noteFailed(now);
    now += ProgressSaveGate::kRetryBackoffMs;
  }
  REQUIRE(g.givenUp());
  g.forget();
  CHECK(g.givenUp());
  CHECK_FALSE(g.wants(SavePoint{0, 0, 0}, now));
}

TEST_CASE("the backoff survives the millis() wrap") {
  // millis() wraps every ~49 days and the shell's other quiet-window gates all use the
  // unsigned-difference idiom for it. A gate that compared absolute timestamps would
  // stop saving for 49 days, once, on a device left running.
  ProgressSaveGate g;
  const uint32_t justBeforeWrap = 0xFFFFFFFFu - 10u;
  g.noteFailed(justBeforeWrap);
  const SavePoint at{1, 1, 1};
  CHECK_FALSE(g.wants(at, justBeforeWrap));
  // 11 ms later the counter has wrapped to 0; still inside the backoff.
  CHECK_FALSE(g.wants(at, 0u));
  CHECK_FALSE(g.wants(at, ProgressSaveGate::kRetryBackoffMs - 20u));
  // And the backoff still ends when it should, on the far side of the wrap.
  CHECK(g.wants(at, ProgressSaveGate::kRetryBackoffMs));
}
