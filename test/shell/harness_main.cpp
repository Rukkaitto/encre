// THE SCENARIO RUNNER (#180, phase 2 of the #178 epic).
//
// One scenario per invocation, chosen by argv[1], compared against a committed
// transcript. The transcript is main.cpp's OWN logf stream -- already a structured
// record of [stage], [boot], [detect], [paint], [i] and [sd] lines -- interleaved
// with the fakes' <panel>, <card>, <nvs>, <spi> and <input> records.
//
// ONE SCENARIO PER PROCESS, AND THAT IS NOT A CONVENIENCE. Every piece of state in
// main.cpp is a file-static with a boot-time initialiser and there is no reset
// function for any of it; a second scenario in one process would run against the
// first one's App, settings, factory and NVS. Writing that reset would be a change
// to untested code before the net exists, which inverts the whole sequence. So each
// scenario is its own add_test -- the idiom reader_sim already uses for its nine.
//
// DETERMINISM IS BY CONSTRUCTION, NOT BY FILTERING. The clock is virtual and moves
// only when a waveform or a delay moves it; the heap is scripted. Exactly ONE field
// is normalised, and it is named below. A post-filter would be a second place the
// truth lives.
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "InputManager.h"
#include "PowerManager.h"   // freeink::HarnessSlept -- a sleep is a throw here
#include "esp_system.h"     // HarnessRestarted -- so is esp_restart()
#include "harness_state.h"
#include "input_task.h"

void setup();
void loop();

namespace {

// THE ONE NORMALISED FIELD. `[frame] ...: viewing driver framebuffer %p` prints the
// fake's own static buffer, whose address is stable within a platform and not
// across them. It is the only line in a whole cold boot that carries one, which is
// why this is a rule with a single instance rather than a filter with a policy.
std::string normalise(const std::string& line) {
  const std::string key = "viewing driver framebuffer 0x";
  const size_t at = line.find(key);
  if (at == std::string::npos) return line;
  const size_t from = at + key.size() - 2;  // keep "0x" out of the replacement
  size_t to = from;
  while (to < line.size() && line[to] != ',') ++to;
  return line.substr(0, from) + "<fb>" + line.substr(to);
}

std::string cardRootFor(const char* id) {
  return (std::filesystem::temp_directory_path() / (std::string("encre-harness-") + id)).string();
}

// A card with the shape a first boot finds: a /books directory and nothing in it.
void giveCard(const char* id) {
  const std::string root = cardRootFor(id);
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root + "/books");
  harness::cardRoot() = root;
  harness::cardPresent() = true;
}

// ---------------------------------------------------------------- scenarios

// The device as it leaves the factory: a card, no books, no session record, no
// settings file. Everything setup() does, in order, ending at the first paint.
void bootColdCardPresent() {
  giveCard("boot-cold");
  setup();
}

// NO CARD AT ALL, which is a different root screen rather than a degraded Home:
// buildSdMissingApp instead of buildHomeApp, no settings load, no session restore.
void bootNoCard() {
  harness::cardPresent() = false;
  harness::cardRoot() = cardRootFor("boot-no-card");
  setup();
}

// ONE PRESS, AND THE WHOLE POINT IS THE [i] LINE. A Down on Home moves the focus
// and repaints; the transcript carries `[i] #1 DOWN SHORT from=home to=home` and a
// `[paint]` line with mode=FAST. Deleting `gApp->dispatch(ev)` -- which is a
// recorded defect that shipped -- leaves the [i] line reading paint=none and
// removes the [paint] line entirely.
void pressDownOnHome() {
  giveCard("press-down");
  setup();
  // BTN_RIGHT, NOT BTN_DOWN, AND THE INVERSION IS THE POINT. The SDK's names
  // describe ITS band order rather than this device's panel, so the shell maps
  // BTN_RIGHT onto reader::Button::Down and BTN_DOWN onto Right -- one of the two
  // SIDE buttons (main.cpp:8126-8135). Queueing the obvious constant produced an
  // `[i] #1 RIGHT` line on the first run of this scenario, which is the same
  // confusion that left the side buttons doing nothing for two phases while
  // test_gesture.cpp never mentioned Left or Right at all.
  //
  // BOTH EDGES, because that is what the real task queues and what the recognizer
  // needs: a Short fires on the DOWN edge, and the release is what classifies a
  // press made entirely inside a repaint -- which a gray refresh makes possible,
  // since tick() only runs from the main loop.
  harness::queueButton(InputManager::BTN_RIGHT, true, harness::clock_().ms);
  harness::queueButton(InputManager::BTN_RIGHT, false, harness::clock_().ms + 40);
  for (int i = 0; i < 4; ++i) loop();
}

struct Scenario {
  const char* id;
  const char* what;
  void (*run)();
};

// THE REGISTRY IS A TABLE IN CODE, NOT A LIST IN PROSE, and it is checked for a
// repeated id on every run -- compare-design.py's two recorded reasons, and both
// halves of the same defect: a duplicated id there inflated a denominator to 37 for
// 36 screens, and an absent one shrank it, so no ratio ever looked wrong.
constexpr Scenario kScenarios[] = {
    {"boot_cold_card_present", "a first boot with a card and no books", bootColdCardPresent},
    {"boot_no_card", "no card at all: SdMissing is the root, not a degraded Home", bootNoCard},
    {"press_down_on_home", "one Down on Home: the [i] line and a FAST repaint", pressDownOnHome},
};

int fail(const char* fmt, const char* arg = "") {
  std::fprintf(stderr, fmt, arg);
  std::fprintf(stderr, "\n");
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  // A REPEATED ID IS AN AUTHORING MISTAKE IN THE TABLE and should not need the
  // right argument to surface, so it is checked over the whole table every run.
  for (size_t a = 0; a < std::size(kScenarios); ++a)
    for (size_t b = a + 1; b < std::size(kScenarios); ++b)
      if (std::strcmp(kScenarios[a].id, kScenarios[b].id) == 0)
        return fail("scenario id listed twice: %s", kScenarios[a].id);

  if (argc < 2) {
    std::printf("scenarios:\n");
    for (const Scenario& s : kScenarios) std::printf("  %-26s %s\n", s.id, s.what);
    return 0;
  }

  const Scenario* chosen = nullptr;
  for (const Scenario& s : kScenarios)
    if (std::strcmp(s.id, argv[1]) == 0) chosen = &s;
  // AN UNKNOWN ID IS AN ERROR, NOT AN EMPTY RUN. Selecting nothing finds nothing
  // wrong and would exit 0 -- compare-design.py's `--only` defect exactly.
  if (chosen == nullptr) return fail("no such scenario: %s", argv[1]);

  harness::resetAll();
  try {
    chosen->run();
  } catch (const freeink::HarnessSlept&) {
    harness::record("<harness> scenario ended in deep sleep");
  } catch (const HarnessRestarted&) {
    harness::record("<harness> scenario ended in a restart");
  }

  std::string got;
  for (const std::string& line : harness::transcript()) got += normalise(line) + "\n";

  const std::filesystem::path golden =
      std::filesystem::path(TRANSCRIPT_DIR) / (std::string(chosen->id) + ".txt");
  std::ifstream in(golden);
  if (!in) {
    // A MISSING TRANSCRIPT IS A FAILURE, NOT A BLESSING. The golden PNGs' own rule:
    // the candidate is written and named, and a human looks at it before it becomes
    // the baseline. A runner that wrote its own expectations would pin whatever it
    // did on the day it first ran.
    const std::filesystem::path candidate =
        std::filesystem::path(BUILD_DIR) / (std::string(chosen->id) + "_candidate.txt");
    std::ofstream(candidate) << got;
    std::fprintf(stderr, "no transcript at %s\n", golden.string().c_str());
    std::fprintf(stderr, "candidate written to %s -- READ IT, then copy it into place\n",
                 candidate.string().c_str());
    return 2;
  }
  const std::string want((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (got == want) return 0;

  const std::filesystem::path candidate =
      std::filesystem::path(BUILD_DIR) / (std::string(chosen->id) + "_candidate.txt");
  std::ofstream(candidate) << got;
  std::fprintf(stderr, "transcript differs for %s\n", chosen->id);
  std::fprintf(stderr, "  expected: %s\n", golden.string().c_str());
  std::fprintf(stderr, "  actual:   %s\n", candidate.string().c_str());
  return 1;
}
