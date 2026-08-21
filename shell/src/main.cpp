#include <Arduino.h>
#include <BoardConfig.h>
#include <EInkDisplay.h>
#include <InputManager.h>
#include <PowerManager.h>
#include <SPI.h>
#include <esp_sleep.h>
#include <esp_system.h>  // esp_restart(), for the RETRY-after-a-pull branch
#include <XteinkDetect.h>

#include <memory>
#include <optional>
#include <string>

#include "font_body400.h"
#include "font_body500.h"
#include "font_display700.h"
#include "font_label400.h"
#include "font_label500.h"
#include "font_meta400.h"
#include "font_meta500.h"
#include "font_title700.h"
#include "font_value500.h"
#include "font_value700.h"
#include "input_task.h"
#include "reader/app.h"
#include "reader/fontset.h"
#include "reader/framebuffer.h"
#include "reader/input.h"
#include "reader/json.h"
#include "reader/power.h"
#include "reader/refresh.h"
#include "reader/screen_home.h"
#include "reader/screen_sd_missing.h"
#include "reader/screens.h"
#include "reader/settings.h"
#include "reader/text.h"  // reader::Plane
#include "reader/theme_quiet.h"
#include "reader/viewmodel.h"
#include "sd_fs.h"
#include "sd_selftest.h"
#include "session.h"

// Xteink display SPI pins. Shared by X3 and X4; MISO is shared with the SD card.
constexpr int8_t EPD_SCLK = 8, EPD_MOSI = 10, EPD_CS = 21, EPD_DC = 4, EPD_RST = 5,
                 EPD_BUSY = 6, SPI_MISO = 7;

EInkDisplay display(EPD_SCLK, EPD_MOSI, EPD_CS, EPD_DC, EPD_RST, EPD_BUSY);

// THE THREE KNOBS THAT USED TO BE COMPILED IN.
//
// `kSleepAfterMs`, `kFullRefreshEvery` and `kFullOnTransition` were constants
// here through 2B. They are fields of reader::Settings now, read from
// /.reader/settings.json at boot, and the struct's DEFAULTS are the values that
// were compiled in -- so a device with no card, or with no settings file on the
// card, behaves exactly as it did. There is one copy of each number, in
// core/include/reader/settings.h, and gRefresh and gIdle below are constructed
// from it.
//
// What the numbers mean, and why they are what they are, stays here: the
// reasoning is about this panel and this driver, which is the shell's subject.
//
// SLEEP AFTER: five minutes idle. 0 means never sleep, which is a legitimate
// choice and the right one while a transfer is running, so the settings loader
// does not clamp it up to its floor.
//
// PERIODIC FULL REFRESH CADENCE: DISABLED, matching the reference firmware,
// which schedules no periodic full refresh in its UI at all.
//
// A cadence exists to clear the residue differential refreshes leave behind, and
// keeping one was the cautious choice -- but at 1-in-15 it put a black flash on
// an arbitrary navigation, which reads as MORE random than the transition flash
// it replaced, and unpredictable flashing was the complaint. Two things make the
// risk acceptable: CrossInk ships this way on this hardware, and the ink
// accumulation this project actually observed was caused by a missing grayscale
// settle pass, on a path chrome no longer takes at all -- not by FAST refreshes.
//
// If ghosting does appear, this number is the whole fix: set it to 15 or 20 --
// and it is a settings file away now rather than a rebuild.
// Watch for a screen that gradually stops being readable with no obvious cause.
//
// FULL ON TRANSITION: whether a screen change forces a FULL refresh. TRUE, and
// this is a deliberate divergence from the reference firmware.
//
// CrossInk does not do it on this panel: ScreenTransitionRefresh::modeFor returns
// FULL only for `screenChanged && !deviceIsX3()`, and its list/menu screens call
// displayBuffer() with no argument, whose default is FAST_REFRESH. So it avoids
// the flash and accepts the ghosting -- and the ghosting is observable, reported
// on its settings screen on this device.
//
// The distinction that matters is not "flash" versus "no flash". A flash on a
// SCREEN CHANGE is expected behaviour on an e-reader -- Kindle and Kobo both do
// it -- because a differential update there has a whole screen of stale content
// to ghost through. A flash on a focus move inside one screen is a defect. Those
// are separate settings here, so we take FULL on transitions and NO periodic
// cadence (see above), which is neither firmware's behaviour and is better than
// both.
//
// Applies to pop as well as push, deliberately: leaving Settings back to Home is
// exactly the case where the settings list would ghost onto Home, so treating
// only the outbound direction as a transition would fix half the problem.
//
// Cost, measured on the X3: a transition takes the 693 ms GC waveform instead of
// the 389 ms DU, so ~825 ms against ~520 ms. Focus moves are untouched. That is
// the right place to spend it -- screens change far less often than a focus does.

// Input-settle window before a repaint. ZERO, deliberately. NOT a setting: there
// is nothing here for a user to have a preference about, and no board row for it.
//
// It was 90 ms, added when a paint cost 1363 ms and a burst of presses cost N
// times that. A paint is now a single FAST waveform, so the insurance is worth
// far less and the latency costs far more: 90 ms was being added to EVERY press,
// including isolated ones, which is pure delay against a reference firmware that
// adds none.
//
// Bursts still coalesce, by the mechanism that was always doing the real work:
// the drain loop dispatches every queued event before painting, so presses that
// land DURING a paint are still merged into the next one. The settle window only
// ever caught presses landing in the gap between paints, and at a human press
// rate of ~150 ms it could not merge those without being long enough to feel.
constexpr uint32_t kCoalesceMs = 0;

// millis() of the last button transition, for the coalescing window. Starts at 0
// so the first paint in setup() is never deferred.
static uint32_t gLastInputMs = 0;

// Everything the render needs has to outlive setup(), so it lives here rather
// than on setup()'s stack -- but the frame stays heap-allocated behind
// unique_ptr on purpose. A file-scope Framebuffer would allocate during static
// init, before the largest-block check in setup() could run, and a vector that
// cannot allocate under -fno-exceptions is an abort() boot loop with no
// diagnostic. This project has already lost a boot to exactly that.
//
// ONE frame, not two. It is logically portrait (what the screens draw against)
// and physically landscape (what the panel is handed), because it is built with
// Rotation::Ccw -- see the allocation in setup().
static std::unique_ptr<reader::Framebuffer> gFrame;
// FontSet owns nothing: every Font it holds is a zero-copy view into a blob the
// caller supplies. The embedded kFont* arrays have static storage, so they
// outlive the set -- but nothing here may ever hand load() a scope-limited copy.
static std::optional<reader::FontSet> gFonts;
static reader::QuietTheme gTheme;
// The screen catalogue is the one from core/, shared with the simulator. A
// shell-local copy would drift from it on row lists and titles, and the drift
// would be invisible because each half would keep passing its own checks.
static reader::DemoScreenFactory gFactory;
static std::unique_ptr<reader::App> gApp;
static reader::PressRecognizer gPresses;
// The settings as they will be applied. Constant-initialised to the struct's
// defaults -- the numbers that were compiled in through 2B -- so gRefresh and
// gIdle below are correct before setup() runs and stay correct if there is no
// card, no settings file, or a settings file this build refuses.
static reader::Settings gSettings;
// Constructed FROM gSettings, declared above it in this same translation unit, so
// there is no second copy of the defaults to drift. Both take the loaded values
// through setters in loadAndApplySettings(); a constructor argument could not,
// because these are alive long before the card is mounted.
static reader::RefreshPolicy gRefresh(gSettings.fullRefreshEvery, gSettings.fullOnTransition);
static reader::IdleTimer gIdle(gSettings.sleepAfterMs);
static InputManager gInput;
// The card. One instance: SDCardManager is a singleton underneath, so a second
// SdFileSystem would address the same volume with its own idea of whether it is
// mounted.
static SdFileSystem gSd;
// Did SDCardManager::begin() ever return true this boot? It opens with
// `if (initialized) return true;` and the SPI path exposes no end()/unmount(), so
// after one success a later begin() reports success WITHOUT touching the
// hardware. That is the whole reason bringUpStorage() below does not trust it.
static bool gSdBeganOnce = false;
// Whether the UI currently believes storage is usable. This is the shell's own
// view, not gSd.mounted(): it is what decides whether the app is rooted at Home
// or at the SD-missing screen, and it is what the presence poll in loop() watches
// for a usable -> unusable edge. Kept separate from gSdBeganOnce because the two
// answer different questions -- "did the hardware ever come up this boot" versus
// "is the card usable right now" -- and the RETRY path needs both.
static bool gStorageUsable = false;

// Bring-up instrumentation. Serial here is native USB CDC, so the port
// re-enumerates when the app starts and anything printed in the first second is
// lost to the host. Every stage is announced and the last one reached is
// repeated from loop(), so a hang can be located by attaching at any time.
static const char* stage = "boot";
static void mark(const char* s) {
  stage = s;
  Serial.printf("[stage] %s\n", s);
  Serial.flush();
}

// --- Storage -------------------------------------------------------------
//
// Mount the card and say, honestly, whether the filesystem is usable.
//
// `mount()` alone is not that answer. SDCardManager::begin() opens with
// `if (initialized) return true;` and the SPI path exposes no end() or unmount(),
// so once it has succeeded it keeps succeeding whether or not the card is still
// in the slot. A retry that trusted it would report success, replace the
// SD-missing screen with Home, and then fail on the first read -- and a RETRY
// button that lies is worse than one that stays put, because the user stops
// believing the screen.
//
// So a mount is only accepted once probe() has actually read the card and
// mounted() agrees. Here, and ONLY here, that is still the root-directory read:
// this runs before armCardProbes() has a mounted volume to look for a target file
// on, so probe() is on its RootDir fallback. That is fine at this one call site
// and nowhere else -- SDCardManager::begin() has just run, so nothing has been in
// the sector cache long enough for a stale hit, and the alternative would be
// looking for a settings file before knowing there is a card. The cached-read
// hazard is a POLLING hazard; see ProbeTarget in sd_fs.h.
//
// `why` is "boot" or "retry", and it is in every line here on purpose: the two
// paths differ only in what they mean, so a serial log without it is ambiguous.
static bool bringUpStorage(const char* why) {
  const bool begun = gSd.mount();  // logs "[sd] mount ok" / "... FAILED"
  if (!begun) {
    Serial.printf("[sd] %s: no usable storage\n", why);
    Serial.flush();
    return false;
  }
  const bool wasFirst = !gSdBeganOnce;
  gSdBeganOnce = true;
  if (gSd.probe() && gSd.mounted()) {
    Serial.printf("[sd] %s: storage usable (mount confirmed by a root-directory read)\n", why);
    Serial.flush();
    return true;
  }
  // begin() said yes and the card would not answer. Two ways to get here:
  //   * first begin() of this boot: a card that mounts but cannot be read. Worth
  //     retrying -- reseating it may fix it.
  //   * a later begin(): the short-circuit above. The card mounted once and is
  //     now gone, and there is no unmount to undo that, so nothing short of a
  //     restart recovers it.
  //
  // The second branch is UNREACHABLE BY CONSTRUCTION now and kept anyway.
  // handleRetry() checks gSdBeganOnce itself and restarts rather than calling
  // this, so both live callers (setup(), and retry before the first successful
  // mount) arrive with wasFirst true. It stays because it is the only thing that
  // would name the failure if a third caller were added that did not check --
  // a silent "storage unusable" on a lying begin() is the hard version of this
  // bug to find, and one printf is cheap insurance against reintroducing it.
  if (wasFirst) {
    Serial.printf("[sd] %s: begin() succeeded but the card would not answer a directory "
                  "read; not treating storage as usable\n",
                  why);
  } else {
    Serial.printf("[sd] %s: begin() returned true WITHOUT touching the card -- it "
                  "short-circuits on its own `initialized` flag -- and the card is not "
                  "answering. A card that mounted once and was then pulled cannot be "
                  "re-mounted without a REBOOT; staying on the SD-missing screen (and this "
                  "caller should have restarted instead -- see handleRetry)\n",
                  why);
  }
  Serial.flush();
  return false;
}

// Why loadSettings() said no. It answers with one bool over six distinguishable
// causes, and this log line is the only way a user ever learns their hand-edited
// file was rejected rather than applied -- so the shell asks the same questions
// again, in the same order, and names the first one that fails.
//
// `defaulted` matters as much as the reason, because the two halves of that list
// leave the device in different states: the first four throw the file away and
// run on defaults, while the last two KEEP the file and correct one field. Saying
// "defaulted" for a clamped value would be a false statement about every other
// setting in the file.
//
// Called ONLY on failure, so the happy path pays nothing; the cost is one more
// small read on the shared bus when something is already wrong.
struct SettingsVerdict {
  const char* reason;
  bool defaulted;
};

static SettingsVerdict settingsFailure(reader::FileSystem& fs) {
  // First, because every question below answers "no" on an unmounted filesystem
  // and "there is no file" would be the wrong story to tell about a missing card.
  if (!fs.mounted()) return {"no usable storage, so there was nothing to read", true};
  if (!fs.exists(reader::kSettingsPath))
    return {"no file yet (first boot, or nothing has saved one)", true};
  std::string text;
  if (!fs.readAll(reader::kSettingsPath, text)) return {"the file exists but would not read", true};
  reader::JsonObject o;
  if (!o.parse(text))
    return {"not parseable flat JSON -- hand-edited, or a write lost power part way", true};
  int64_t version = 0;
  if (!o.getInt("version", version)) return {"no `version` key", true};
  if (version != reader::kSettingsVersion)
    return {"a `version` this build does not know", true};
  return {"a value was out of range and was clamped, or was the wrong JSON type and was "
          "ignored; every other field in the file still applies",
          false};
}

// Read the settings and apply them. Safe with an unmounted filesystem: every
// FileSystem method fails when mounted() is false, so loadSettings() falls back
// to defaults and this reports exactly that.
static void loadAndApplySettings() {
  const bool ok = reader::loadSettings(gSd, gSettings);
  if (ok) {
    Serial.printf("[boot] settings loaded from %s\n", reader::kSettingsPath);
  } else {
    const SettingsVerdict v = settingsFailure(gSd);
    Serial.printf("[boot] settings %s: %s\n", v.defaulted ? "DEFAULTED" : "CORRECTED", v.reason);
  }
  gRefresh.setCadence(gSettings.fullRefreshEvery);
  gRefresh.setFullOnTransition(gSettings.fullOnTransition);
  gIdle.setTimeout(gSettings.sleepAfterMs);
  Serial.printf("[boot] settings in force: sleepAfterMs=%lu fullRefreshEvery=%d "
                "fullOnTransition=%d\n",
                (unsigned long)gSettings.sleepAfterMs, gSettings.fullRefreshEvery,
                (int)gSettings.fullOnTransition);
  Serial.flush();
}

// --- The card-liveness probes --------------------------------------------
//
// The two cadences. What each probe does and why there are two of them is on
// pollCardPresence() below; these are up here because armCardProbes() names both
// numbers in its boot log, and handleRetry() has to be able to call it.
//
// 2000 ms for the fast probe: fast enough that a pull is noticed while the user
// still has their hand on the slot, cheap enough (three sector reads) to be a
// rounding error next to a paint.
//
// 25000 ms for the backstop, and the number has two halves. The floor is 20000 --
// SDCardManager caches sdUsedBytes() for exactly that long, so calling it any
// sooner returns the cached answer without touching the card and the whole
// mechanism becomes a no-op. 25000 is that floor plus margin against millis()
// jitter and a loop that skipped the slot. It is also the ceiling on how long a
// pull can go unnoticed even if the fast probe were somehow still being served
// from cache, which is the guarantee this layer is here to provide.
//
// It is NOT cheaper than that: freeClusterCount() reads the whole FAT one sector
// at a time, which on a large card is hundreds of milliseconds to over a second
// of SPI, and this is a battery device whose entire job is to sit idle. Hence the
// layering -- the fast probe carries the responsiveness so this one can be rare,
// and armCardProbes() logs the measured scan time so the real cost on the card in
// the slot is in the log rather than estimated in a comment. Both only run while
// AWAKE; the idle timer sleeps at five minutes and sleep cuts the X3's SD rail.
constexpr uint32_t kSdPollMs = 2000;
constexpr uint32_t kSdDeepPollMs = 25000;
static uint32_t gLastSdPollMs = 0;
static uint32_t gLastSdDeepPollMs = 0;

// Called once after every confirmed mount (boot, and a successful RETRY). Two
// jobs, and the first one is worth doing on its own merits.
//
// 1. GUARANTEE THE SETTINGS FILE EXISTS. On a fresh card there is none, because
//    nothing has ever saved one -- loadSettings() reports "no file yet" and the
//    device runs on the struct's defaults. Writing the defaults out at boot gives
//    the user a plain, hand-editable /.reader/settings.json (spec 5 already fixes
//    the path), gives 2C-3's Settings screen a file to UPDATE rather than create,
//    and gives the probe below a target it can count on.
//
//    Only when ABSENT. A file that exists but is corrupt or carries an unknown
//    version is left exactly as the user left it: loadAndApplySettings() has
//    already logged DEFAULTED and named the reason, and silently overwriting a
//    hand-edited file to "fix" it would destroy the only copy of what they typed.
//    A corrupt file is still a perfectly good probe target -- the probe reads one
//    byte and does not care what it says.
//
// 2. POINT THE PROBES AT IT. The fast probe reads that file; the backstop scans
//    the FAT. See ProbeTarget and deepProbe() in sd_fs.h for why each one is what
//    it is, and pollCardPresence() below for the cadences.
//
// EVERY OUTCOME IS LOGGED, including the ones where nothing went wrong, because
// this is the boot line that says which mechanism is in force. A probe that
// degrades quietly is the defect this whole block exists to fix, so "degraded"
// has to be visible from a serial log without knowing to look for it.
static void armCardProbes(const char* why) {
  if (!gSd.exists(reader::kSettingsPath)) {
    if (reader::saveSettings(gSd, gSettings)) {
      Serial.printf("[sd] %s: no settings file on the card, so this build's defaults were "
                    "written to %s -- hand-editable from here on\n",
                    why, reader::kSettingsPath);
    } else {
      Serial.printf("[sd] %s: there is no settings file and %s could NOT be written (card "
                    "full, write-protected, or failing). Running on defaults\n",
                    why, reader::kSettingsPath);
    }
    Serial.flush();
  }

  if (gSd.useFileProbeTarget(reader::kSettingsPath)) {
    Serial.printf("[sd] %s: fast probe reads %s every %lu ms. Opening it walks the root "
                  "directory, then /.reader, then a data sector -- three sectors against "
                  "SdFat's one 512-byte cache, so it cannot be answered from RAM\n",
                  why, gSd.probeTargetPath(), (unsigned long)kSdPollMs);
  } else {
    // The one state that must never be quiet. Reaching here means the settings
    // file is absent or unreadable after the attempt above, so probe() is back to
    // the root-directory read that could not see a pulled card at all.
    Serial.printf("[sd] %s: fast probe DEGRADED to a root-directory read -- %s is missing or "
                  "would not open, so there is no file to read. That read can be served from "
                  "SdFat's sector cache, which is exactly the defect this target exists to "
                  "avoid; the %lu ms FAT-scan backstop is what will catch a pull now\n",
                  why, reader::kSettingsPath, (unsigned long)kSdDeepPollMs);
  }
  Serial.flush();

  // The backstop's baseline, and the one place its real cost on THIS card is
  // measured. A big card means a big FAT, so print the number rather than
  // guessing at it in a comment.
  const uint32_t t0 = millis();
  const bool armed = gSd.armDeepProbe();
  const uint32_t scanMs = millis() - t0;
  if (armed) {
    Serial.printf("[sd] %s: FAT-scan backstop armed (%llu bytes used, scan took %lu ms) and "
                  "re-runs every %lu ms -- it is the check that cannot be served from cache\n",
                  why, (unsigned long long)gSd.deepProbeBaselineBytes(), (unsigned long)scanMs,
                  (unsigned long)kSdDeepPollMs);
  } else {
    Serial.printf("[sd] %s: FAT-scan backstop NOT armed -- the scan reported 0 bytes used, "
                  "which is also how it reports its own failure, so it could never tell a "
                  "dead card from this volume. The fast probe is the only card check\n",
                  why);
  }
  Serial.flush();
  // Both clocks restart here, so the first poll of each kind lands one full
  // interval after this -- the reads above have just answered both questions.
  gLastSdPollMs = millis();
  gLastSdDeepPollMs = gLastSdPollMs;
}

// --- The app, and the session record -------------------------------------

// Build the app with Home as its root, replacing whatever was there.
//
// App has no "replace the root", and a successful retry cannot PUSH Home: the
// SD-missing screen is the root in that state, so Home would be at depth 2 and
// Back would pop to a screen whose message is no longer true. A fresh App is the
// straightforward answer, and the long-press mask has to be re-synced with it --
// the mask is PressRecognizer's, not the App's, so a new stack whose top binds
// different holds leaves the recognizer bound to the old screen's.
static void buildHomeApp() {
  gApp = std::make_unique<reader::App>(
      std::make_unique<reader::HomeScreen>(reader::demoHomeVm(), reader::demoHomeTargets()),
      gFactory);
  gPresses.setLongPressable(gApp->longPressable());
}

// Store where the user is, so a wake can put them back. Cheap to call after every
// dispatch: saveSession() skips an identical rewrite, so navigating back and
// forth does not grind the NVS partition.
//
// FOCUS IS ALWAYS 0, and that is deliberate rather than unfinished. reader::Screen
// exposes id/fidelity/longPressable/onEvent/render and no focus accessor, so there
// is no way to read the focus of the screen on top without adding virtuals to
// every screen -- and the only multi-item screen where a restored focus would be
// visible is Library, which does not exist until 2C-2. Adding two virtuals now to
// carry a value nothing can produce is speculative; 2C-2 wires it when there is a
// concrete need, and the record already has the field.
//
// IT SAYS WHAT IT DID, and that is not decoration. The wake path and this are the
// two halves of one mechanism, and a wake that comes back to Home is ambiguous
// between them: either nothing was ever stored, or a record was stored and the
// restore would not honour it. So this logs a CHANGED record when it goes in --
// once per screen change, not once per focus move, because the unchanged case is
// the common one and a line per keypress would bury the interesting ones -- and
// logs distinctly when the store refuses. saveSession() prints the NVS-level
// reason (namespace would not open, or a put came back short); the line here is
// the consequence, which is the part a reader of the log actually cares about.
//
// `gLoggedScreen` tracks what was last announced rather than what is in NVS.
// saveSession() has its own skip-an-identical-rewrite cache and returns true
// without touching flash, so asking it "did you write?" is not possible from
// here; mirroring the comparison is. The two can only disagree by this printing
// one extra line after a failure, which is the harmless direction.
static bool gLoggedScreen = false;
static reader::ScreenId gLastLoggedScreen = reader::ScreenId::Home;

static void saveWhereWeAre() {
  Session s;
  s.screen = gApp->top().id();
  s.focus = 0;
  const bool changed = !gLoggedScreen || gLastLoggedScreen != s.screen;
  if (!saveSession(s)) {
    // The other half of defect "wake came back to Home": a save that fails here
    // leaves a record that either does not exist or names an older screen, and
    // the wake then looks like the restore failed when it was the write.
    Serial.printf("[session] NOT stored: screen=%s will not be restored by the next wake\n",
                  reader::screenName(s.screen));
    Serial.flush();
    gLoggedScreen = false;
    return;
  }
  if (changed) {
    Serial.printf("[session] stored screen=%s focus=%u; a wake will come back here\n",
                  reader::screenName(s.screen), s.focus);
    Serial.flush();
  }
  gLastLoggedScreen = s.screen;
  gLoggedScreen = true;
}

// Rebuild the app rooted at the SD-missing screen, replacing whatever was there.
//
// The counterpart to buildHomeApp(), and a fresh App for the same reason: there
// is no "replace the root", and the no-card prompt must be the ROOT rather than a
// screen pushed over the user's last one -- its Back slot is empty because there
// is nothing behind it, and leaving Home underneath would let Back walk into a
// library that cannot be read. A new App also starts dirty and in transition, so
// the swap paints itself as a screen change (a FULL refresh) rather than needing
// the caller to remember to mark it.
//
// The session record is deliberately LEFT ALONE. It names where the user was, and
// that is still the best answer for the next wake: if the card is back by then the
// restore honours it, and if it is not, the boot path roots at this screen anyway.
// Overwriting it with SD-MISSING would throw away the only useful thing it holds.
static void buildSdMissingApp() {
  gApp = std::make_unique<reader::App>(std::make_unique<reader::SdMissingScreen>(), gFactory);
  gPresses.setLongPressable(gApp->longPressable());
}

// The SD-missing screen's RETRY, which App latched for us because mounting is not
// core/'s to do.
//
// TWO BRANCHES, because there are two ways to be on this screen and only one of
// them can be fixed in process. They are told apart by gSdBeganOnce -- "did
// SDCardManager::begin() ever return true this boot" -- which is exactly the
// condition that makes a further begin() meaningless:
//
//   * NEVER MOUNTED (no card in the slot at power-on, or one that would not
//     mount). The hardware has not been initialised, so begin() will really try
//     again. The in-place attempt is correct here, and it is also the fast answer
//     -- a card pushed in and RETRY pressed comes up in well under a second.
//
//   * MOUNTED, THEN LOST (the poll in loop() saw the card stop answering). An
//     in-process remount is IMPOSSIBLE, not merely unreliable:
//     SDCardManager::begin() opens with `if (initialized) return true;` and the
//     SPI path exposes no end() or unmount(), so there is no call anywhere in the
//     SDK that puts that flag back. begin() would return true without addressing
//     the card, and this firmware would replace the SD-missing screen with Home
//     and then fail on the first read.
//
// So the second branch REBOOTS. That is the honest mechanism rather than a
// workaround, and the distinction is worth being precise about: the operation the
// user asked for is "re-initialise the card", the only code path that performs it
// is the one that runs at boot, and a restart is how you get to run it. It is not
// papering over a bug in this firmware -- it is forced by the SDK having no
// unmount on the SPI path, and nothing here can add one without forking the
// submodule. The cost is a few seconds of boot; the benefit is that RETRY simply
// works from the user's side, which is what spec 6 asks of the button. The
// alternative that shipped before this -- stay on the screen and log that a reboot
// is needed -- is a button that correctly does nothing, which reads as broken.
//
// A reboot is a cold boot (no wake cause), so setup() clears the session record
// and roots at Home. That is the right landing: the card has just been reseated,
// and resuming a screen from before it went away is not what the user is asking
// for when they press RETRY.
static void handleRetry() {
  // Clear the latch FIRST, so a failed attempt cannot re-fire on every loop.
  gApp->clearRetryRequest();
  mark("sd-retry");
  if (gSdBeganOnce) {
    Serial.printf("[sd] retry: the card mounted earlier this boot and was then lost. "
                  "SDCardManager::begin() short-circuits on its own `initialized` flag and "
                  "the SPI path has no end()/unmount(), so it cannot be re-initialised in "
                  "process -- RESTARTING, which re-runs the whole mount path\n");
    Serial.flush();
    mark("sd-retry-restart");
    Serial.flush();  // the restart is immediate; nothing buffered survives it
    esp_restart();
  }
  if (!bringUpStorage("retry")) {
    // Nothing changes on glass: the message is still true, App::dispatch
    // deliberately does not mark a Retry dirty, and spending a full refresh to
    // redraw an identical screen would read as the button having done something.
    // bringUpStorage() has already logged which failure this was.
    return;
  }
  gStorageUsable = true;
  loadAndApplySettings();  // the settings live on the card that just appeared
  // A brand-new card is the fresh-card case: it may have no settings file, and
  // both probes have to be re-pointed at whatever this card turns out to hold.
  // Skipping this is how the poll would go back to the cached root read on
  // exactly the card the user just inserted.
  armCardProbes("retry");
  buildHomeApp();  // and Home replaces the root; see buildHomeApp()
  mark("sd-retry-ok");
}

// --- Card-presence poll --------------------------------------------------
//
// PULLING THE CARD OUT HAS TO SHOW THE SD-MISSING SCREEN, and nothing used to
// notice. SdFileSystem::mounted() is "the card was there and nothing has since
// told us otherwise", and what tells it otherwise is an operation failing -- but
// V1 does almost no filesystem work after boot, so a card pulled on the Home
// screen stayed invisible until something happened to read a directory, which
// might be never. The screen exists for exactly this state and was unreachable
// from it.
//
// So the liveness check is made ACTIVE: a real read on a timer. TWO of them, in
// layers, and the reason there are two is a defect this file shipped once.
//
// THE FIRST ATTEMPT DID NOT WORK ON HARDWARE. It polled probe(), and probe()
// opened "/" -- the root directory, whose sector is the one sector guaranteed to
// be in SdFat's cache after boot. SdFat here has exactly ONE 512-byte cache slot
// (USE_SEPARATE_FAT_CACHE is gated on __arm__ and the C3 is RISC-V), so the poll
// was answered out of RAM and kept succeeding with the card in the user's hand:
// no log line, and the SD-missing screen was never reached. The risk was written
// down when the poll was added; the device then confirmed it.
//
//   * THE FAST PROBE, every kSdPollMs. probe() now reads a byte out of a real
//     file, which walks the root directory, then /.reader, then a data sector --
//     three sectors against one cache slot, and the slot ends up holding the last
//     of the three, so the next probe misses on its first access. See ProbeTarget
//     in sd_fs.h. This carries the responsiveness.
//   * THE BACKSTOP, every kSdDeepPollMs. The above is still an ARGUMENT about
//     cache geometry, and an argument is what was wrong last time. deepProbe()
//     scans the whole FAT (sdUsedBytes -> freeClusterCount), which is thousands
//     of sectors and cannot be served from a 512-byte cache under any reading of
//     the code. It is slow, so it is rare; it bounds worst-case detection at
//     ~25 s even if every assumption above is wrong.
//
// At most ONE of the two runs per call, and the backstop wins when both are due:
// its whole value is that it does not depend on the fast probe being right, so it
// must not be crowded out by it. Skipping one fast probe every ~25 s costs
// nothing.
//
// Three constraints shape both, all of them from CLAUDE.md's hardware notes
// rather than from taste:
//
//  1. IT IS SPI TRAFFIC ON THE PANEL'S BUS. SDCardManager does no locking of any
//     kind, so a transfer overlapping a refresh is a bus-level fault that looks
//     random. probe() takes the recursive SpiBusGuard internally and one is taken
//     here as well, so probe() and the mounted() read that follows it are one
//     atomic answer rather than two that could straddle a paint.
//  2. IT MUST NOT RUN WITH A PAINT PENDING. The call site is placed AFTER the
//     paint block in loop() and gated on !gApp->dirty(), so a frame that is owed
//     to the user goes to the panel before the bus is used for anything else.
//     Both live on the Arduino loop task today, so a paint cannot literally be
//     in flight concurrently -- this is what keeps that true if either one ever
//     moves off it, and it also stops the poll from delaying a repaint.
//  3. IT COSTS BATTERY, on a device whose entire job is to sit idle showing a
//     page. Every probe wakes the card and reads sectors, so a tight loop would
//     be a continuous drain for information nobody asked for. Both cadences are
//     declared and justified up at kSdPollMs / kSdDeepPollMs, and only run while
//     the device is AWAKE -- the idle timer sleeps at five minutes and sleep cuts
//     the X3's SD rail entirely.
//
// THE HONEST LIMIT, and it is the same one mounted() carries: neither probe is a
// card-detect. There is no card-detect GPIO in the board profiles at all and
// CMD13 is private to SDCardManager, so a pull is noticed by a read FAILING, not
// by the slot reporting empty -- which means it can be a poll or two late. The
// backstop is what puts a bound on "late". It is far more than the nothing that
// was here before, and it is the strongest claim this SDK supports.

// Which mechanism noticed, for the edge log. Not a bool, because "the card is
// gone" is worth much less in a log than "the card is gone and THIS is what saw
// it": if the backstop is doing all the detecting, the fast probe is still being
// served from cache and this file has the same bug in a new place.
static const char* fastProbeName() {
  return gSd.probeTarget() == SdFileSystem::ProbeTarget::File
             ? "the 2 s FAST PROBE (a byte read from a real file)"
             : "the 2 s FAST PROBE (a root-directory read -- the DEGRADED fallback, which "
               "should not have been able to see this)";
}

static void pollCardPresence(uint32_t now) {
  // Nothing to detect once the answer is already "no card": the SD-missing screen
  // is up, and there is no in-process remount for the poll to discover anyway
  // (that is RETRY's restart branch). Skipping is also the battery-cheap default
  // for a device sitting on this screen.
  if (!gStorageUsable) return;

  const char* by = nullptr;
  const bool deepDue = gSd.deepProbeArmed() &&
                       static_cast<uint32_t>(now - gLastSdDeepPollMs) >= kSdDeepPollMs;
  if (deepDue) {
    gLastSdDeepPollMs = now;
    // See constraint 1 above. Recursive, so deepProbe()'s own acquisition nests.
    SpiBusGuard bus;
    if (!(gSd.deepProbe() && gSd.mounted()))
      by = "the 25 s FAT-SCAN BACKSTOP, which the fast probe had not noticed -- so the fast "
           "probe was being answered from SdFat's sector cache";
  } else if (static_cast<uint32_t>(now - gLastSdPollMs) >= kSdPollMs) {
    gLastSdPollMs = now;
    SpiBusGuard bus;
    if (!(gSd.probe() && gSd.mounted())) by = fastProbeName();
  }
  if (!by) return;  // nothing due, or the card answered

  // A usable -> unusable edge. noteCardGone() has already said what stopped
  // answering; this says which mechanism asked, and what the UI is doing about it.
  gStorageUsable = false;
  Serial.printf("[sd] THE CARD IS NO LONGER ANSWERING -- pulled, or failed. Detected by %s. "
                "Routing to the SD-missing screen; RETRY will restart the device, because a "
                "card lost after a mount cannot be re-mounted in process\n",
                by);
  Serial.flush();
  buildSdMissingApp();
  mark("sd-lost");
}

// One binary drives both Xteink models, and the panel controller varies by
// production batch, so the running firmware has to work out what it is on
// before touching the display:
//   1. I2C fingerprint for the X3-only peripherals (fuel gauge / RTC / IMU)
//   2. select the matching board profile
//   3. probe the display bus, because newer X3 units carry a UC8279d in place
//      of the UC8253 and the two need different drivers
// Skipping step 3 is what made the first paint hang: a UC8279 acknowledges the
// UC8253 power-on but never completes its refresh waveform.
static void detectAndSelectBoard() {
  uint8_t s1 = 0, s2 = 0;
  const auto verdict = freeink::detectXteinkVerdict(&s1, &s2);
  const bool isX3 = (verdict == freeink::XteinkVerdict::X3Confirmed);
  Serial.printf("[detect] i2c verdict=%s (pass scores %u/%u) -> %s\n",
                verdict == freeink::XteinkVerdict::X3Confirmed    ? "X3Confirmed"
                : verdict == freeink::XteinkVerdict::X4Confirmed  ? "X4Confirmed"
                                                                  : "Inconclusive",
                s1, s2, isX3 ? "X3" : "X4");

  BoardConfig::selectDevice(isX3 ? BoardConfig::Board::XteinkX3
                                 : BoardConfig::Board::XteinkX4);

  const bool promoted = freeink::applyXteinkDisplayController();
  const auto& diag = freeink::getXteinkDisplayProbeDiag();
  Serial.printf("[detect] controller probe valid=%d promoted=%d "
                "ver=%02X %02X %02X %02X %02X flg=%02X\n",
                diag.valid, promoted, diag.ver[0], diag.ver[1], diag.ver[2],
                diag.ver[3], diag.ver[4], diag.flg);

  if (isX3 && BoardConfig::ACTIVE.displayController == BoardConfig::DisplayController::UC8279) {
    BoardConfig::selectDevice(BoardConfig::Board::XteinkX3Uc8279);
    Serial.printf("[detect] promoted profile to XteinkX3Uc8279\n");
  }
  Serial.printf("[detect] active controller=%u\n",
                (unsigned)BoardConfig::ACTIVE.displayController);
  Serial.flush();

  // SPI must be up, with MISO, before the driver owns the display pins.
  SPI.begin(EPD_SCLK, SPI_MISO, EPD_MOSI, EPD_CS);

  if (isX3) display.setDisplayX3();
  mark(isX3 ? "panel-profile-x3" : "panel-profile-x4");
}

// Time spent drawing, accumulated across a paint's passes so it can be reported
// separately from time spent waiting on the panel. A focus move on Home measured
// far slower than pushing a screen, and both take the same code path, so the
// difference has to be either the drawing (Home draws a dither block and two
// large faces; the placeholder draws almost nothing) or the panel's own waveform
// -- and guessing which would mean optimising blind.
static uint32_t gRenderMs = 0;

// Split the render pass so the log distinguishes drawing from the full-frame
// memcpy into the driver's own buffer, which the reference firmware does not do
// at all -- it draws straight into that buffer through an orientation-aware
// coordinate transform.
//
// There used to be a `rotate` field here too, and it is what this split was
// added to find: a full-frame rotate90CCW cost 37 ms of a 521 ms repaint, paid
// on all 418k pixels whether one changed or every one did, on a text screen that
// inks about 8% of them. It is gone -- the framebuffer now applies the same
// mapping per pixel as it draws -- so the field would report zero forever.
static uint32_t gDrawMs = 0, gCopyMs = 0;

// One render pass, straight into the panel-oriented frame.
//
// The rotation is the framebuffer's, declared once where gFrame is allocated;
// nothing here transposes anything. CCW is the correct direction, verified on X3
// hardware (CW renders the whole screen 180 degrees out -- the two differ by
// exactly half a turn, so swapping them is not the fix for a mirrored image).
// Unverified on X4: if an X4 comes out upside down, the Rotation passed to the
// constructor in setup() is the line, not anything in here.
static void paintPlane(reader::Plane plane) {
  const uint32_t t0 = millis();
  gFrame->clear(true);
  gApp->top().render(*gFrame, *gFonts, gTheme, plane);
  const uint32_t t1 = millis();
  gDrawMs += t1 - t0;
  gRenderMs += t1 - t0;
}

// The 4-level path: base frame, settle pass, two bit-planes, combine, rebase.
// Every comment below was earned by breaking the panel -- LSB before MSB, the
// settle pass before the planes, the Bw re-render instead of an extra frame.
//
// No screen declares Fidelity::Grayscale today, so nothing calls this: chrome
// moved to the one-pass paths, which are ~5x cheaper and legible. It is
// kept because it is the only way to put continuous tone on this panel, which is
// Phase 3's question about book images, and because getting the sequence right
// cost several bricked-looking paints. Do not delete it to remove dead code.
static void paintGray() {
  // 1. The B/W base frame the panel paints first. displayGrayscaleBase() takes
  //    no buffer argument — it drives the driver's own frameBuffer — so
  //    setFramebuffer() (a memcpy) has to land the frame there first.
  paintPlane(reader::Plane::Bw);
  display.setFramebuffer(gFrame->data());
  display.displayGrayscaleBase(EInkDisplay::HALF_REFRESH);
  mark("gray-base-displayed");

  // 2. The settle pass, which leaves the particles receptive to the weak
  //    grayscale nudge waveform. Must run BEFORE the planes are written: the
  //    driver skips it once grayscale planes have overwritten DTM1/DTM2.
  //
  //    DO NOT remove this as a duplicate of the settle inside
  //    displayGrayscaleBase. It was tried: the two issue the same commands --
  //    same XtfPreBwMid bank, same CDI/CCSET/TSSET, same trigger -- so on
  //    inspection it looks like the identical operation done twice, and dropping
  //    it saves 366 ms of a 1363 ms paint. On device the panel then accumulated
  //    ink, everything growing perceptibly thicker with every refresh. The
  //    difference is the controller's state, not the commands: the base's settle
  //    runs having just written the new frame into DTM2, this one runs against
  //    the synced planes, and the particles need both passes before the AA nudge
  //    is safe. Identical command sequences are not identical operations.
  display.preconditionGrayscale();
  mark("gray-preconditioned");

  // 3. The two bit-planes. Both copies go straight out over SPI into controller
  //    RAM and retain no pointer, so the single frame serves both — and served
  //    the base frame above, which the driver has already memcpy'd. LSB must go
  //    first: the MSB copy is dropped unless the driver has seen a valid LSB.
  paintPlane(reader::Plane::Lsb);
  display.copyGrayscaleLsbBuffers(gFrame->data());
  paintPlane(reader::Plane::Msb);
  display.copyGrayscaleMsbBuffers(gFrame->data());
  mark("gray-planes-written");

  // 4. Paint the combined 4-level image (the driver reads the planes it was
  //    handed, not any framebuffer), then put the controller back on a valid
  //    B/W baseline so the next ordinary refresh is differentially sane.
  display.displayGrayBuffer();
  mark("gray-displayed");
  // Re-render the B/W pass rather than keeping a second frame alive for it. The
  //    original reason was that a third frame would not allocate at all (largest
  //    contiguous block ~115 KB against 52 KB frames); drawing straight into
  //    panel orientation has since cut us to one frame, so the headroom is real
  //    now -- but re-rendering is still cheaper than the RAM, and Phase 3's
  //    pagination cache wants that headroom more than this path does.
  paintPlane(reader::Plane::Bw);
  display.cleanupGrayscaleBuffers(gFrame->data());
  mark("refresh-complete");
}

// Hand the one 1-bit frame in gFrame to the panel. Its bytes are already in the
// panel's own landscape orientation, so there is nothing between the render and
// the driver. Shared by both one-pass paths below, which differ only in the
// plane they render.
static void showOnePass(reader::RefreshMode mode) {
  const uint32_t tc = millis();
  display.setFramebuffer(gFrame->data());
  gCopyMs += millis() - tc;
  display.displayBuffer(mode == reader::RefreshMode::Full ? EInkDisplay::FULL_REFRESH
                                                          : EInkDisplay::FAST_REFRESH);
}

// The hard 1-bit path, and what every chrome screen ships on: one render pass
// and one panel waveform against the four-and-three above.
static void paintMono(reader::RefreshMode mode) {
  // Plane::Bw thresholds coverage at half: a pixel is ink or it is paper, and
  // nothing in between survives. That is what the reference firmware does to its
  // chrome -- CrossInk builds its UI fonts 1-bit and reads its anti-aliasing
  // setting only in the reader activities -- and matching it on the same glass is
  // the point. Compared side by side on an X3, the hard edge reads cleaner than a
  // stipple at chrome sizes.
  paintPlane(reader::Plane::Bw);
  showOnePass(mode);
  mark("mono-displayed");
}

// The stippled 1-bit path. Same cost as paintMono -- one render pass, one
// waveform -- and reachable only by a screen that overrides fidelity() to
// Fidelity::Dithered. Nothing does today; chrome ships Mono.
static void paintDithered(reader::RefreshMode mode) {
  // BwDithered, not Bw: this path keeps a soft edge on a two-level frame by
  // stippling glyph and icon edge coverage through a dispersed Bayer threshold
  // rather than thresholding it away. This is the technique freeink-ui.md
  // documents for exactly this ("reproduces the edge coverage on 1-bit panels
  // through its ordered Bayer dither"), and it works because every role in our
  // ramp is 21px or larger -- the doc's guidance is that dithered edges look best
  // from about 16px up. Kept for large display type, where a stroke is wide
  // enough for the stipple to read as tone rather than as grain.
  paintPlane(reader::Plane::BwDithered);
  showOnePass(mode);
  mark("dithered-displayed");
}

static void renderTop() {
  const reader::RefreshMode mode = gRefresh.next(gApp->transition());
  const reader::Fidelity fidelity = gApp->top().fidelity();
  // `mode` is what the POLICY decided, not necessarily what the panel does: a
  // Grayscale screen runs the full three-plane sequence regardless, because that
  // sequence has no differential form. So `fidelity=gray mode=FAST` is not a
  // contradiction -- it means the cadence had a fast slot available and this
  // screen could not use it. Nothing declares Grayscale or Dithered today, so in
  // practice every paint is `fidelity=mono`.
  Serial.printf("[paint] screen=%s fidelity=%s mode=%s sinceFull=%d\n",
                reader::screenName(gApp->top().id()),
                fidelity == reader::Fidelity::Grayscale  ? "gray"
                : fidelity == reader::Fidelity::Dithered ? "dithered"
                                                         : "mono",
                mode == reader::RefreshMode::Full ? "FULL" : "FAST", gRefresh.sinceFull());
  Serial.flush();
  gRenderMs = gDrawMs = gCopyMs = 0;
  const uint32_t t0 = millis();
  // THE OTHER HALF OF THE SHARED-BUS INVARIANT. Every public method of
  // SdFileSystem takes this same recursive guard; this is the one acquisition on
  // the panel side, and it spans the WHOLE paint sequence rather than the SPI
  // writes alone -- the driver keeps the display's CS asserted across its BUSY
  // waits, so a card transfer landing in a wait is still a transfer into an
  // asserted panel. The grayscale path holds it across all four passes and three
  // waveforms for the same reason.
  //
  // Today this is free insurance: paints and card access both run on the Arduino
  // loop task, so they are already serialised by there being one thread, and the
  // only other task (input_task.cpp) touches ADC and GPIO only. It is here to be
  // STRUCTURAL rather than a rule someone has to remember -- the day a background
  // library scan or a cover decode moves off this task, the fault it prevents is
  // intermittent, bus-level and miserable to find.
  SpiBusGuard bus;
  switch (fidelity) {
    case reader::Fidelity::Grayscale:
      // The grayscale sequence is inherently a full repaint; the policy's FAST is
      // not available here.
      paintGray();
      break;
    case reader::Fidelity::Dithered: paintDithered(mode); break;
    case reader::Fidelity::Mono: paintMono(mode); break;
  }
  const uint32_t total = millis() - t0;
  // render = drawing all passes (4 for gray: Bw, Lsb, Msb, then Bw again for the
  // cleanup rebase; 1 for mono and for dithered). panel = everything else, which
  // is essentially BUSY waits. `render` and `draw` are now the same number --
  // drawing is all a render pass does since the rotate went away -- and both are
  // kept so the field stays comparable against the logs that measured the
  // difference. If they ever diverge again, something new got added to the pass.
  Serial.printf("[paint] done total=%lums render=%lums (draw=%lu copy=%lu) panel=%lums\n",
                (unsigned long)total, (unsigned long)gRenderMs, (unsigned long)gDrawMs,
                (unsigned long)gCopyMs, (unsigned long)(total - gRenderMs - gCopyMs));
  Serial.flush();
}

void setup() {
  Serial.begin(115200);
  delay(2500);  // let USB CDC enumerate before the first print
  mark("serial-up");

  detectAndSelectBoard();

  display.begin();
  mark("display-begin-returned");
  // Fresh boot after a flash: force a clean full sync so the panel is not
  // differentially updated against whatever the previous firmware left on it.
  // A fresh boot after a flash must not be differentially updated against
  // whatever the previous firmware left on the panel, so the driver's two
  // initial full clears are right -- they are what flashes the screen black.
  //
  // Waking from deep sleep is a chip reset that looks identical from here, but
  // it is NOT the same situation: e-ink holds its image with no power, so the
  // panel still shows exactly what we painted before sleeping. Clearing then is
  // a black flash to replace a correct image with the same image. Tell the
  // driver the panel is already valid instead.
  const esp_sleep_wakeup_cause_t wake = esp_sleep_get_wakeup_cause();
  const bool fromSleep = (wake != ESP_SLEEP_WAKEUP_UNDEFINED);
  Serial.printf("[boot] wake cause=%d -> %s\n", (int)wake,
                fromSleep ? "resumed from sleep, panel holds our frame"
                          : "cold boot, clearing the panel");
  Serial.flush();
  if (fromSleep) {
    display.skipInitialResync();
  } else {
    display.requestResync();
  }

  Serial.printf("[info] panel %dx%d, buffer %u bytes\n", display.getDisplayWidth(),
                display.getDisplayHeight(), (unsigned)display.getBufferSize());
  Serial.printf("[info] free heap %u, largest block %u\n", (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMaxAllocHeap());
  Serial.flush();

  reader::FontSet& fonts = gFonts.emplace();
  // Each role names its weight and FontSet::load checks the asset against it,
  // so a mis-wired pair here is a loud "font-load-FAILED" at boot rather than a
  // screen drawn in the wrong weight for the rest of the project.
  const bool fontsOk =
      fonts.load(reader::Role::Meta400, kFontMeta400, kFontMeta400Size) &&
      fonts.load(reader::Role::Meta500, kFontMeta500, kFontMeta500Size) &&
      fonts.load(reader::Role::Label400, kFontLabel400, kFontLabel400Size) &&
      fonts.load(reader::Role::Label500, kFontLabel500, kFontLabel500Size) &&
      fonts.load(reader::Role::Value500, kFontValue500, kFontValue500Size) &&
      fonts.load(reader::Role::Value700, kFontValue700, kFontValue700Size) &&
      fonts.load(reader::Role::Body400, kFontBody400, kFontBody400Size) &&
      fonts.load(reader::Role::Body500, kFontBody500, kFontBody500Size) &&
      fonts.load(reader::Role::Title700, kFontTitle700, kFontTitle700Size) &&
      fonts.load(reader::Role::Display700, kFontDisplay700, kFontDisplay700Size);
  if (!fontsOk || !fonts.ready()) {
    mark("font-load-FAILED");
    return;
  }
  mark("fonts-ok");

  const int panelW = display.getDisplayWidth();
  const int panelH = display.getDisplayHeight();

  // ONE 1-bit frame. It used to be two -- a portrait one to draw into and a
  // landscape one to rotate the finished frame into -- and the rotate is gone,
  // so the portrait buffer is gone with it: the frame below is drawn against
  // portrait coordinates and stored landscape. That is 52272 bytes of heap given
  // back on the X3 (48000 on the X4), against a measured largest contiguous
  // block of only ~115 KB.
  //
  // Three frames was never possible on this hardware and is worth remembering,
  // because it is the same wall: measured free heap is ~233 KB but the largest
  // contiguous block is ~115 KB, so a third 52 KB allocation found no block big
  // enough even though the total would have covered it. std::vector then throws,
  // and the firmware is built -fno-exceptions, so that is an abort() and a boot
  // loop with no diagnostic. Re-rendering the B/W pass for the grayscale cleanup
  // rebase, rather than retaining a frame for it, is what avoids needing one.
  const unsigned frameBytes = display.getBufferSize();
  const unsigned largest = ESP.getMaxAllocHeap();
  Serial.printf("[info] frame %u bytes x1; free heap %u, largest block %u\n", frameBytes,
                (unsigned)ESP.getFreeHeap(), largest);
  Serial.flush();
  // Fail loudly rather than aborting inside a constructor: a vector that cannot
  // allocate takes the whole firmware down with no diagnostic. One frame is now
  // all that is needed, so this asks for one -- but the check STAYS. It is the
  // only thing standing between a fragmented heap and that silent abort, and the
  // headroom it reports is what will matter when Phase 3 wants a page cache.
  if (largest < frameBytes) {
    Serial.printf("[fatal] largest block %u < one frame (%u)\n", largest, frameBytes);
    mark("frame-alloc-WOULD-FAIL");
    return;
  }
  // Logical portrait, physical landscape: the constructor's dimensions are what
  // the screens draw against (528x792 on the X3), and Rotation::Ccw allocates
  // the store transposed so data() is the panel's own 792x528 buffer. CCW is
  // measured on X3 hardware; see the note on reader::Rotation.
  gFrame = std::make_unique<reader::Framebuffer>(panelH, panelW, reader::Rotation::Ccw);
  mark("frames-allocated");

  // A short buffer would make setFramebuffer's memcpy read past the end, and a
  // zero-length one means the panel geometry came back wrong. This is the check
  // that the rotation is applied to the STORE and not just to the coordinates:
  // a rotated frame whose stride came from the logical width would be exactly
  // this many bytes and still be laid out wrong, so it is not the whole proof --
  // test_rotate.cpp's byte-identity case against rotate90CCW is.
  if (gFrame->sizeBytes() != (int)display.getBufferSize()) {
    Serial.printf("[fatal] frame size %d != driver buffer %u\n", gFrame->sizeBytes(),
                  (unsigned)display.getBufferSize());
    mark("frame-size-MISMATCH");
    gFrame.reset();
    return;
  }

  // Which grayscale path the selected driver actually offers. Logged because
  // the sequence below is only correct for a driver that does NOT combine the
  // base frame into the gray waveform (X3/X4 do not; only Paper Mono does).
  Serial.printf("[info] gray caps: combinesBase=%d busyStaging=%d strip=%d\n",
                display.combinesGrayscaleBase(), display.supportsBusyGrayscaleStaging(),
                display.supportsStripGrayscale());
  Serial.flush();

  // MOUNT THE CARD -- after the display is up, and deliberately so.
  //
  // SPI.begin() ends up called TWICE on one bus: detectAndSelectBoard() calls it
  // with the display's pins, and SDCardManager::begin() calls it again with the
  // card's. That is the reverse of the order the SDK's comments assume, and it is
  // the ordering to be suspicious of if the panel misbehaves after a mount. Two
  // things say it should be benign: begin()'s mitigation is about CS lines, not
  // about who called SPI.begin() last (it drives the display's CS high before
  // probing, because a powered, never-deselected panel breaks card detection),
  // and SdFat issues its own beginTransaction with its own SPISettings on every
  // access, so the bus is reconfigured per transfer either way.
  //
  // Mounting before display.begin() or after it were the two options, and after
  // wins on three counts: the SD-missing screen cannot be painted before the
  // display is up anyway, so a failed mount has nowhere to go; the panel is
  // powered and its CS line settled by the time begin() drives it high to probe
  // the card, which is the condition that mitigation was written for; and the
  // whole ordering question ends up in one identifiable place.
  //
  // ON HARDWARE, THE FIRST PAINT AFTER A MOUNT IS THE THING TO WATCH. A corrupt
  // or hung first refresh with a card in the slot, and a clean one without, is
  // this call order and nothing else.
  mark("sd-mount");
  const bool storage = bringUpStorage("boot");
  // The contract self-test, which is a stub returning -1 unless the firmware was
  // built with -DENCRE_FS_SELFTEST=1 (see sd_selftest.h). Called from here rather
  // than left uncalled so the seam is reachable at all: shell/ has no test
  // harness, and an on-device routine nothing invokes checks nothing.
  const int fsFailures = runSdFsContractSelfTest(gSd);
  if (fsFailures >= 0) {
    Serial.printf("[sd] contract self-test: %d failed assertion(s)\n", fsFailures);
    Serial.flush();
  }

  // Settings on either branch. With no card every FileSystem method fails, so
  // this reports "no file" and applies the compiled-in defaults -- the settings
  // must not depend on the card for the device to behave.
  loadAndApplySettings();

  // Write the settings file if the card has none, then point both card-liveness
  // probes at it. After loadAndApplySettings() on purpose: gSettings holds what
  // will actually be in force by now, so a fresh card gets a file that matches
  // the running device rather than one written before the load had a say.
  if (storage) armCardProbes("boot");

  // The shell's own view of storage, which is what roots the app and what the
  // presence poll in loop() watches for a usable -> unusable edge.
  gStorageUsable = storage;
  if (storage) {
    // The root is Home, built from the shared catalogue. Nothing rebuilds it, so
    // popping back to Home returns this object with its focus intact.
    buildHomeApp();
    mark("root-home");
  } else {
    // A missing card is a DESIGNED SCREEN (spec 6: "device never boots into a
    // broken UI"), not a hang and not a Home screen with no books. It is the
    // root, not a screen pushed over Home: there is nothing behind it to go back
    // to, which is why its Back slot is empty. Same helper the runtime pull path
    // uses, so the two cannot build a different stack for the same state.
    buildSdMissingApp();
    mark("root-sd-missing");
  }

  // WHERE THE USER WAS. Only across a genuine wake: a device that boots into a
  // sub-screen after a week off is confusing, and 2B already distinguishes the
  // two cases from esp_sleep_get_wakeup_cause().
  if (!fromSleep) {
    // Cold boot starts at Home and forgets the record, so the next wake cannot
    // resume a screen from a previous run of the device. Logged because otherwise
    // "it started at Home" is indistinguishable from a restore that silently
    // failed, and that is exactly what a bring-up check needs to tell apart.
    clearSession();
    // Not "starting at Home": with no card the root above is the SD-missing
    // screen, and this line must not contradict it.
    Serial.printf("[session] cold boot: record cleared, nothing to restore\n");
    Serial.flush();
  } else if (storage) {
    // EVERY OUTCOME BELOW IS LOGGED, and it was not always so. This used to read
    // `if (loadSession(s) && s.screen != ScreenId::Home) { ... }` with no else at
    // all, which made the two most interesting outcomes print nothing: a
    // loadSession() that returned false, and a record that named Home. Both leave
    // the device on Home, which is also what a restore that silently failed looks
    // like, so "Settings, sleep, wake, back on Home" was indistinguishable from
    // working-as-designed in a serial log. Distinguishing them is the point of
    // this whole ladder -- read it against saveWhereWeAre()'s lines from the
    // previous run to place the fault on the write side or the read side.
    Session s;
    const bool found = loadSession(s);
    if (!found) {
      // loadSession() has already said WHICH no-record this is: no namespace, a
      // version this build does not know, or a screen id it cannot decode. This
      // line is what that means from here.
      Serial.printf("[session] no usable record, so nothing to restore; staying on %s. If a "
                    "'[session] stored screen=...' line appeared before the last sleep, the "
                    "WRITE is what failed, not the restore\n",
                    reader::screenName(gApp->top().id()));
      Serial.flush();
    } else if (s.screen == reader::ScreenId::Home) {
      // Skipped in silence by the old `!= Home` guard. It is a legitimate state --
      // the user was on Home when they slept -- but it has to be said out loud,
      // because it is the one case where landing on Home is CORRECT and every
      // other way of landing on Home is a fault.
      Serial.printf("[session] the record names HOME, which is already the root; nothing to "
                    "push (this is a correct wake onto Home)\n");
      Serial.flush();
    } else if (s.screen == reader::ScreenId::SdMissing) {
      // The mount above already decided this, and it decided there IS a card.
      // Restoring the no-card screen over a working card would be showing the
      // user a message that is no longer true.
      Serial.printf("[session] the record says SD-MISSING but the card mounted; Home\n");
      Serial.flush();
    } else if (gApp->pushScreen(s.screen)) {
      // Home stays underneath, so Back works. Only ONE screen is restored, so a
      // record naming a screen that was two deep (Settings > Input Monitor)
      // comes back with Home under it rather than Settings -- the record holds
      // one id, not a path. Nothing in V1 is unreachable that way; a restored
      // path is 2C-2's if the deeper screens make it worth one.
      Serial.printf("[session] restored screen=%s over Home\n", reader::screenName(s.screen));
      Serial.flush();
      mark("session-restored");
    } else {
      // The factory refused it: an id this build has no case for, from a newer
      // firmware's record. Home is already the root, so there is nothing to
      // undo.
      Serial.printf("[session] cannot build screen=%s; staying on Home\n",
                    reader::screenName(s.screen));
      Serial.flush();
    }
  } else {
    // Woke with no card. The record is left ALONE rather than cleared: it is
    // still true, and the next wake with a card in the slot can honour it.
    Serial.printf("[session] woke with no usable storage; the record is kept for next time\n");
    Serial.flush();
  }

  // Before the first poll, not just after each dispatch: a hold started on the
  // very first frame must be recognised too.
  gPresses.setLongPressable(gApp->longPressable());
  gInput.begin();
  startInputTask(gInput);
  mark("input-started");

  renderTop();
  // The paint above satisfied the App's initial dirty flag. Without this the
  // first loop() iteration would repaint an identical Home and spend another
  // 1.5 s of panel time on it.
  gApp->clearDirty();

  // The driver grants itself TWO full clears at init (_initialFullsRemaining),
  // tuned for a consumer that paints a splash before its first real screen. We
  // paint the real screen immediately, so the paint above already cleared the
  // panel and the second clear lands on the user's FIRST BUTTON PRESS -- a
  // black flash and an extra 693 ms DRF on an otherwise ordinary focus move,
  // which is exactly why it read as random. Measured: 2113 ms, 2056 ms, then
  // 1363 ms for every paint after.
  //
  // Spend the rest of the budget here. The panel now holds a frame we just
  // wrote, which is the assertion skipInitialResync exists to make.
  display.skipInitialResync();

  // Restart both presence clocks HERE rather than leaving them where
  // armCardProbes() set them, so the first probe of each kind lands one full
  // interval after the FIRST PAINT instead of one interval after the mount --
  // which, with a ~825 ms boot paint in between, would otherwise be almost
  // immediately. Everything either probe would ask has just been answered:
  // bringUpStorage() read the root directory, armCardProbes() read the settings
  // file and scanned the FAT. Probing again in the same breath would be traffic on
  // the panel's bus for answers we have.
  gLastSdPollMs = millis();
  gLastSdDeepPollMs = gLastSdPollMs;
  mark("first-paint-complete");
}

[[noreturn]] static void sleepNow() {
  // The Sleep screen is boarded and belongs to Phase 2C. Painting nothing is
  // not a gap in the picture: e-ink holds its last image with no power, so the
  // device keeps showing whatever you were looking at.
  //
  // THE SCREEN IS NAMED HERE ON PURPOSE, and it is the third leg of the tripod
  // that locates a bad wake. Nothing is written at this point -- the record was
  // stored when the user navigated -- so this line is the last chance to say what
  // the record OUGHT to contain. Read against the next boot:
  //   * this says SETTINGS and the wake says no usable record -> the WRITE is the
  //     problem (look for saveWhereWeAre's "NOT stored", or its absence entirely);
  //   * this says SETTINGS and the wake restores SETTINGS -> the session path is
  //     fine and the symptom is elsewhere;
  //   * this line never appears at all -> the sleep path is the problem: the
  //     Power press is not arriving as an event, or something slept without
  //     coming through here.
  Serial.printf("[power] sleeping from screen=%s; the record should name it on wake. Wake with "
                "the power button\n",
                reader::screenName(gApp->top().id()));
  Serial.flush();
  display.deepSleep();
  // Cuts the X3's SD rail (GPIO13) and any other gated rail, latched so the
  // switches stay off through sleep. Without it the card stays powered and
  // drains the battery all night.
  freeink::PowerManager::powerDownRailsForSleep();
  // Waits for release, arms the SoC-correct wake source from the board's power
  // pin and polarity, then sleeps. Wake is a chip RESET, so this never returns:
  // setup() runs again, sees a wake cause, and restores the screen from the NVS
  // session record -- which is why nothing is saved here. The record is written
  // after every dispatch, so it is already current, and Power is handled BEFORE
  // dispatch (a power press changes no screen), so there is nothing left to
  // store at this point.
  freeink::PowerManager::deepSleepUntilPowerButton();
}

void loop() {
  // setup() bails out without building the app on a font-load, heap or geometry
  // failure. Repeat the last stage reached so the hang point is visible even
  // when the host attaches late; dereferencing a null gApp below would turn a
  // diagnosable failure into a crash loop that looks like a bootloader hang.
  if (!gApp) {
    static uint32_t n = 0;
    Serial.printf("[alive] %lu last-stage=%s heap=%u (setup did not complete)\n",
                  (unsigned long)++n, stage, (unsigned)ESP.getFreeHeap());
    Serial.flush();
    delay(2000);
    return;
  }

  bool activity = false;
  RawSample s{};
  while (popRawSample(s)) {
    activity = true;
    // Explicit, not a cast. The SDK's BTN_* values happen to match Button's
    // order today, and a silent reinterpret would break the day either changes.
    reader::Button b;
    switch (s.button) {
      // The SDK's names describe ITS band order, not this device's front panel,
      // and on the Xteink they do not agree. Measured on an X3 with the input
      // monitor, pressing each button in turn:
      //
      //   front row, left to right : BTN_BACK  BTN_CONFIRM  BTN_LEFT  BTN_RIGHT
      //   the two side buttons     : BTN_UP    BTN_DOWN
      //
      // The design puts focus navigation on the front-right pair, under their
      // own hint slots, and page turns on the sides with no hints (spec 4.0).
      // So the front-right pair is Up/Down and the SIDE pair is Left/Right --
      // the two names the SDK gives them, swapped. Mapping straight through is
      // what put focus on the side buttons and left the front pair dead.
      case InputManager::BTN_BACK: b = reader::Button::Back; break;
      case InputManager::BTN_CONFIRM: b = reader::Button::Confirm; break;
      case InputManager::BTN_LEFT: b = reader::Button::Up; break;
      case InputManager::BTN_RIGHT: b = reader::Button::Down; break;
      // The sides turn pages in the Reader (Phase 3) and do nothing before it,
      // so which one is "left" is unverified -- there is no behaviour to check
      // it against yet. Verify when page turns land.
      case InputManager::BTN_UP: b = reader::Button::Left; break;
      case InputManager::BTN_DOWN: b = reader::Button::Right; break;
      case InputManager::BTN_POWER: b = reader::Button::Power; break;
      default: continue;
    }
    gPresses.sample(b, s.down, s.ms);
  }
  // Every poll, with or without a transition: a hold fires while the button is
  // still down, so without this a long press never resolves at all.
  gPresses.tick(millis());
  if (activity) {
    gIdle.noteActivity(millis());
    gLastInputMs = millis();
  }

  reader::InputEvent ev{};
  while (gPresses.pop(ev)) {
    Serial.printf("[input] %s %s\n", reader::buttonName(ev.button),
                  ev.kind == reader::PressKind::Long ? "LONG" : "SHORT");
    // POWER IS HANDLED BEFORE dispatch() AND BEFORE saveWhereWeAre(), and that
    // ordering is correct rather than an oversight -- worth stating, because it
    // reads like a bug the first time and re-deriving it costs an hour.
    //
    // sleepNow() is [[noreturn]] (wake is a chip reset), so a Power press means
    // this iteration never reaches either call below it. Neither one has anything
    // to do:
    //   * dispatch: no screen binds Power. It changes no screen and pushes and
    //     pops nothing, so there is no state for a dispatch to produce.
    //   * saveWhereWeAre: the record was already written by the dispatch that put
    //     the user on this screen, one iteration of this same loop ago. It is
    //     current before Power is pressed, which is exactly why sleepNow() does
    //     not save.
    // The consequence for defect diagnosis: if the record is wrong at wake, the
    // write that was supposed to fix it happened at NAVIGATION time, not at sleep
    // time -- so look for saveWhereWeAre's line on the navigation, not here.
    if (ev.button == reader::Button::Power) sleepNow();
    gApp->dispatch(ev);
    // Between the dispatch and the mask refresh below, so the refresh sees
    // whatever screen the retry left on top -- on success that is a brand new App
    // rooted at Home, whose holds are not the SD-missing screen's.
    if (gApp->retryRequested()) handleRetry();
    // The mask belongs to whatever screen is now on top, which a push or pop
    // just changed. Re-reading it here is what keeps a hold bound only where a
    // ring is drawn.
    gPresses.setLongPressable(gApp->longPressable());
    // Where the user is now, for a wake to restore. An unchanged record is not
    // rewritten, so this is nearly free on an event that did not move the stack.
    saveWhereWeAre();
  }

  if (gApp->sleepRequested() || gIdle.tick(millis()) == reader::PowerAction::Sleep) sleepNow();

  // Coalesce a burst of presses into one paint.
  //
  // A paint costs the panel ~1.3 s and cannot be interrupted, so holding Down
  // through a three-item menu used to cost three of them -- 4 s to show two
  // intermediate focus states nobody wanted to see, with every press landing
  // further behind. Waiting for input to go quiet first means a burst paints
  // once, at its final state.
  //
  // The cost is kCoalesceMs added to a single isolated press. That is a ~7%
  // penalty on one paint against a ~3x saving on a burst, and it is below what
  // is noticeable next to the refresh itself.
  const bool settled = static_cast<uint32_t>(millis() - gLastInputMs) >= kCoalesceMs;
  if (gApp->dirty() && settled) {
    renderTop();
    gApp->clearDirty();
  }

  // AFTER the paint block and only with nothing owed to the panel. The poll is
  // SPI traffic on the display's bus (see pollCardPresence), so a frame the user
  // is waiting for goes out first; and if the poll does find the card gone, the
  // fresh App it builds is dirty, so the SD-missing screen paints on the next
  // iteration ten milliseconds later.
  if (!gApp->dirty()) pollCardPresence(millis());

  static uint32_t beat = 0;
  if (++beat % 200 == 0) {
    Serial.printf("[alive] last-stage=%s heap=%u screen=%s depth=%d dropped=%lu/%lu\n", stage,
                  (unsigned)ESP.getFreeHeap(), reader::screenName(gApp->top().id()),
                  gApp->depth(), (unsigned long)rawSamplesDropped(),
                  (unsigned long)gPresses.dropped());
    Serial.flush();
  }
  delay(10);
}
