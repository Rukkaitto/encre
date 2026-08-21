#include "doctest.h"
#include "golden.h"
#include "home_vm.h"
#include "ramp.h"
#include "reader/app.h"
#include "reader/framebuffer.h"
#include "reader/screen_home.h"
#include "reader/theme_quiet.h"

using ramp::Ramp;

namespace {

// Focus reached by PRESSING Down, not by assigning the index: the golden then
// pins the navigation as well as the render.
reader::HomeScreen focusedOnLibrary() {
  reader::HomeScreen h(sampleHome(),
                       {reader::ScreenId::Library, reader::ScreenId::Settings});
  h.onEvent({reader::Button::Down, reader::PressKind::Short});
  REQUIRE(h.focus() == 0);
  return h;
}

}  // namespace

TEST_CASE("Home with the Library row focused matches its golden at both geometries") {
  Ramp r;
  reader::QuietTheme theme;
  struct Case {
    int w, h;
    const char* name;
  };
  // The golden name carries no extension -- golden::goldenPath appends .png.
  for (const Case c : {Case{480, 800, "home_focus_library"},
                       Case{528, 792, "home_focus_library_x3"}}) {
    reader::HomeScreen screen = focusedOnLibrary();
    reader::Framebuffer fb(c.w, c.h);
    // One pass with the plane the screen's own fidelity implies, which is the
    // sequence the shell and the simulator drive: Home declares nothing, so it
    // takes the default Fidelity::Dithered.
    REQUIRE(screen.fidelity() == reader::Fidelity::Dithered);
    screen.render(fb, r.fonts, theme, reader::Plane::BwDithered);
    golden::checkGolden(fb, c.name);
  }
}
