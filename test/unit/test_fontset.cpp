#include <string>
#include <vector>

#include "doctest.h"
#include "ramp.h"
#include "reader/fontset.h"

TEST_CASE("FontSet exposes one loaded face per role and reports readiness") {
  reader::FontSet fonts;
  CHECK_FALSE(fonts.ready());
  ramp::Ramp r;  // its constructor loads the whole ramp and requires readiness
  REQUIRE(r.load(fonts));
  CHECK(fonts.ready());

  // The ramp must actually be a ramp, or the design's hierarchy is lost. Display
  // tops it: the board's percentage is the dominant number on the screen, so it
  // has to out-measure the title rather than tie with it.
  CHECK(fonts[reader::Role::Display700].ascent() > fonts[reader::Role::Title700].ascent());
  CHECK(fonts[reader::Role::Title700].ascent() > fonts[reader::Role::Body400].ascent());
  CHECK(fonts[reader::Role::Body400].ascent() > fonts[reader::Role::Value700].ascent());
  CHECK(fonts[reader::Role::Label500].ascent() >= fonts[reader::Role::Meta400].ascent());
}

TEST_CASE("a rejected blob leaves the set not ready") {
  reader::FontSet fonts;
  const uint8_t junk[16] = {0};
  CHECK_FALSE(fonts.load(reader::Role::Body400, junk, sizeof junk));
  CHECK_FALSE(fonts.ready());
}

TEST_CASE("every role carries the weight and the size its name claims") {
  ramp::Ramp r;
  // The regression pin for the defect that shipped: the ramp used to be one
  // face per size with a weight chosen by majority vote across the boards, and
  // nothing anywhere said which weight that was. Home's author line is a
  // 400-weight --t-body run and was drawn in the 500 face.
  for (int i = 0; i < static_cast<int>(reader::Role::Count_); ++i) {
    const reader::Role role = static_cast<reader::Role>(i);
    const reader::RoleSpec spec = reader::roleSpec(role);
    CAPTURE(i);
    CAPTURE(spec.pt);
    CAPTURE(spec.weight);
    // The face bound to the role declares itself to be the role's face...
    CHECK(r.fonts[role].weight() == spec.weight);
    CHECK(r.fonts[role].ppem() == spec.ppem);
    // ...and the role's own size is the design's pt at 150 DPI, which is the
    // ramp's only definition of a size (ppem = pt * 150 / 72, FreeType's
    // rounding). If these part company the asset and the role disagree about
    // what "11pt" means.
    CHECK(spec.ppem == (spec.pt * 150 + 36) / 72);
  }

  // 29px exists at two weights because the boards set --t-body at two weights,
  // and they are genuinely different ink: the 400 face is lighter than the 500
  // one at the same size. Without this, "Body400" would be a name rather than a
  // fact -- both roles could be backed by the same blob and every check above
  // would still pass.
  const reader::Font& b400 = r.fonts[reader::Role::Body400];
  const reader::Font& b500 = r.fonts[reader::Role::Body500];
  CHECK(b400.ppem() == b500.ppem());
  int ink400 = 0, ink500 = 0;
  for (const char* s = "George Eliot"; *s; ++s) {
    const reader::Glyph* g4 = b400.glyph(static_cast<char32_t>(*s));
    const reader::Glyph* g5 = b500.glyph(static_cast<char32_t>(*s));
    REQUIRE(g4 != nullptr);
    REQUIRE(g5 != nullptr);
    for (int y = 0; y < g4->bitmapH; ++y)
      for (int x = 0; x < g4->bitmapW; ++x) ink400 += b400.coverage(*g4, x, y);
    for (int y = 0; y < g5->bitmapH; ++y)
      for (int x = 0; x < g5->bitmapW; ++x) ink500 += b500.coverage(*g5, x, y);
  }
  CHECK(ink400 < ink500);
  // And the gap is the ~19% Home's author line was over the board by, not a
  // rounding difference: the two faces are far enough apart to be worth two
  // assets.
  CHECK(ink500 * 100 / ink400 > 110);
}

TEST_CASE("FontSet refuses an asset that is not the role's face") {
  ramp::Ramp r;
  reader::FontSet fonts;

  // The 29px/500 asset is a perfectly valid font and the wrong one for a role
  // that says 400. This is the mis-binding that used to be undetectable -- and
  // it is one line in the shell -- so it has to be a load failure, not a screen
  // that is quietly 19% too heavy.
  CHECK_FALSE(fonts.load(reader::Role::Body400, r.body500.data(), r.body500.size()));
  CHECK(fonts.load(reader::Role::Body400, r.body400.data(), r.body400.size()));
  CHECK_FALSE(fonts.load(reader::Role::Body500, r.body400.data(), r.body400.size()));

  // The same for a size mismatch, in both directions.
  CHECK_FALSE(fonts.load(reader::Role::Label500, r.body500.data(), r.body500.size()));
  CHECK_FALSE(fonts.load(reader::Role::Title700, r.display700.data(), r.display700.size()));
  CHECK_FALSE(fonts.load(reader::Role::Display700, r.title700.data(), r.title700.size()));

  // A refused load leaves the role unloaded rather than half-bound: the set
  // cannot become ready by way of a rejection.
  reader::FontSet partial;
  REQUIRE(r.load(partial));
  REQUIRE(partial.ready());
  CHECK_FALSE(partial.load(reader::Role::Body400, r.body500.data(), r.body500.size()));
  CHECK_FALSE(partial.ready());
}
