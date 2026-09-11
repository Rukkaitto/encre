#include <Arduino.h>
#include <BatteryMonitor.h>
#include <BoardConfig.h>
#include <EInkDisplay.h>
#include <InputManager.h>
#include <PowerManager.h>
#include <SPI.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <Preferences.h>  // esp_restart(), for the RETRY-after-a-pull branch
#include <XteinkDetect.h>
// THE COVER CACHE'S WRITER GOES STRAIGHT TO SdFat, and these two are the whole
// reason: 104 KB cannot go through reader::FileSystem::writeAll, which takes a
// whole buffer, and that contract has no write handle. `appendToCard` is a shell
// free function over SdMan for exactly the same reason -- see sd_fs.h, which
// argues it at length. Nothing else in this file needs them.
#include <SDCardManager.h>
#include <SdFat.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <vector>

#include "reader/screen_wifi_connect.h"
#include "reader/screen_wifi_error.h"
#include "reader/screen_wifi_network_actions.h"
#include "reader/screen_wifi_password.h"
#include "reader/screen_wifi_picker.h"
#include "reader/screen_wifi_settings.h"
#include "wifi_store_nvs.h"
#include "wifi_radio_arduino.h"

#include "font_body400.h"
#include "font_body500.h"
#include "font_body700.h"
#include "font_body_serif.h"
#include "font_body_serif_italic.h"
#include "font_display700.h"
#include "font_label400.h"
#include "font_label500.h"
#include "font_meta400.h"
#include "font_meta500.h"
#include "font_meta700.h"
#include "font_title700.h"
#include "font_value500.h"
#include "font_value700.h"
#include "input_task.h"
#include "reader/app.h"
#include "reader/battery_tracker.h"
#include "reader/booklist.h"
#include "reader/card_log.h"
#include "reader/cover.h"
#include "reader/font_manifest.h"
#include "reader/fontset.h"
#include "reader/framebuffer.h"
#include "reader/home_rebuild.h"
#include "reader/input.h"
#include "reader/json.h"
#include "reader/power.h"
#include "reader/profile.h"
#include "reader/document.h"
#include "reader/progress.h"
#include "reader/refresh.h"
#include "reader/screen_battery_empty.h"
#include "reader/screen_home.h"
#include "reader/screen_sd_missing.h"
#include "reader/book.h"
#include "reader/screen_reader.h"
#include "reader/layout.h"
#include "reader/scalablefont.h"
#include "reader/reading_store.h"
#include "reader/progress_save_gate.h"
#include "reader/screen_sleep.h"
#include "reader/sleep_cover.h"
#include "reader/screen_contents.h"
#include "reader/screen_peek.h"
#include "reader/screen_reader_menu.h"
#include "reader/toc.h"
#include "reader/screens.h"
#include "reader/session_record.h"
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

// --- PROGRESSIVE REFINEMENT ------------------------------------------------
//
// A grayscale screen is painted FAST FIRST and upgraded to four levels once the
// buttons go quiet. The reference firmware does this and it is the right shape:
// the reader wants the page NOW and the grey edges a moment later.
//
// The arithmetic, from this device's own logs: the grayscale sequence is three
// waveforms and ~1056 ms, a one-waveform paint is ~520 ms. So a page turn shows
// text in half the time, and the refinement that follows costs what the full
// sequence would have cost anyway. Someone flipping through pages pays 520 ms a
// turn instead of 1056; someone who stops reading gets the four-level page.
//
// IT SHOULD NOT COST A SECOND FLASH, and the reason is in the driver:
// Uc8279Driver::displayGrayscaleBase takes a "clean base" path -- a full visible
// B/W display -- only when `!_oldPlaneValid || _lsbValid || _forceFullSyncNext ||
// _initialFullsRemaining > 0`. After an ordinary one-waveform paint the old plane
// IS valid and no grayscale planes have been written, so the base pass is the
// cheap settle instead. That is the whole reason this is worth doing rather than
// just painting twice.
//
// THE QUIET WINDOW HAS TO MEAN "STOPPED", NOT "BETWEEN TURNS", and 600 ms did
// not. A paint blocks the loop for ~520 ms, so the earliest a second press can be
// DISPATCHED is ~520 ms after the first -- which means someone turning pages
// steadily produces gaps clustered just above that. A 600 ms window therefore
// fired about 80 ms after each paint finished: precisely into the window where the
// next press lands. The refinement then blocked it for its own ~550 ms, and turning
// several pages in a row felt far worse than before the refinement existed.
//
// THE WINDOW IS SET BY THE ASYMMETRY, not by taste. Measured from the device's own
// log across twelve consecutive page turns: the gap between the panel going free
// and the next press being painted was a median of 72 ms, but two of the twelve
// were 898 ms and 1360 ms -- pauses taken WHILE STILL TURNING. Meanwhile a
// refinement measured `[refine] done total=1408ms`, and it cannot be interrupted.
//
// So firing early costs 1408 ms of dead buttons and firing late costs a page that
// stays dithered a little longer. 5000 ms is ~3.5x the longest observed
// mid-turning pause and ~3.5x the cost of getting it wrong, and it is still a
// fifth of the ~23 s an average reader spends on twelve lines. A press RESETS it,
// so the remaining race is a press arriving after five seconds of quiet and inside
// the refinement -- and rawSamplesPending() below closes most of even that.
//
// 600 ms was the first guess and it was actively worse than no refinement: it fired
// ~80 ms after each paint finished, which is exactly where the median 72 ms gap
// puts the next press.
constexpr uint32_t kRefineQuietMs = 5000;

// The page count of a chapter too big to have been counted before its first paint
// (ReaderScreen::kEagerCountBytes -- 63% of a real book's chapters are under it).
//
// IT WAS 1200 ms AND THAT WAS WRONG, ON THE DEVICE'S OWN EVIDENCE. The reasoning
// was "the count is cheap and NEEDED where the refinement is expensive and
// cosmetic, so it can have a much shorter window", and the first half of that is
// simply false. Measured over a real book, `[index]` lines:
//
//     [index] pages=315 in 3605ms      [index] pages=203 in 2092ms
//
// against `[refine] done total=1409ms`. **The count is the MORE expensive of the
// two**, by up to 2.5x, and like the refinement it runs from loop() and cannot be
// interrupted. So it could not justify a shorter window than the cheaper job it
// was being contrasted with.
//
// What that cost, from the same run -- two presses inside a count:
//
//     [i] #31 UP ... | wait=1304 ... | total=1356ms  paint=none
//     [i] #53 UP ... | wait=1962 disp=874 ... | total=3401ms
//
// A press that took 1.3 s to be NOTICED and then did nothing visible, and one that
// took 3.4 s end to end -- against a 505-550 ms chrome interaction. 1200 ms is
// barely two paints, so it fired into exactly the gap a reader turning pages
// leaves, which is the identical mistake kRefineQuietMs records at 600 ms and for
// the identical reason.
//
// SO IT IS THE REFINEMENT'S NUMBER NOW, because it is answering the refinement's
// question: has the user stopped, not is the user between turns. The ordering that
// made two windows seem necessary is preserved by the code rather than by the
// constants -- the count block sits above the refinement block in loop(), so the
// count still lands and repaints before the four-level upgrade reads the footer.
//
// WHAT IT COSTS is the footer reading `3 / —` for longer on a big chapter. That is
// the designed state (design/Reader.dc.html states the em dash and why), and it is
// strictly better than seconds of dead buttons. The case the old comment was
// written for -- "a four-page chapter took four seconds to show its total" -- can
// no longer reach this path at all: four pages is under kEagerCountBytes, so it is
// counted before its first paint and never deferred.
//
// (It DOES repaint, on the fast path -- the "it does not" this comment used to end
// with was corrected once already, by a device log showing the counted total taking
// four seconds to reach the glass because the next page turn almost never won the
// race against the refinement's window.)
//
// IT WAS THE REFINEMENT'S NUMBER AND IT IS ITS OWN AGAIN, at 2000 ms, because the
// cost that tied them together has been paid off. What follows is the whole trade,
// including the part the previous version of this comment had WRONG.
//
// The argument for keeping 5000 was a cost belonging to the abandoning press:
// completeIndex resets `pb_` before it walks, because the walk rewinds the
// ChapterReader the builder reads from, and there is no second stream to rebuild it
// with (another 32 KB inflate window against a 42,152-byte floor). So a forward turn
// after an abandon misses the page ring -- which holds pages already visited, not
// the one ahead -- and pays a full seekTo: ~376 ms at page 38, ~1010 ms at page 99.
//
// TWO THINGS THAT ARGUMENT GOT WRONG, both found by putting the claim in a test
// (test_reader_restream.cpp):
//
//   * IT IS NOT ONLY THE ABANDONED COUNT. completeIndex ends in seekTo(at_), `at_`
//     is by definition the page the ring is most certain to hold, so the restore leg
//     takes a cache hit -- and a hit leaves `pb_` null. A count that COMPLETES spends
//     the stream too. So "at 5000 ms it usually completes, which leaves a live
//     builder behind it" was false: at 5000 ms it usually completes and leaves NO
//     builder, and the next forward turn paid the rewind anyway, on every deferred
//     chapter, guaranteed. The long window was buying nothing.
//   * IT IS THE INTERRUPTING PRESS, not the one after it. The queue is drained at the
//     top of the loop, so the press that made the stop predicate answer true is the
//     very next thing dispatched.
//
// ReaderScreen::restreamAtCurrentPage is the entry point that pays the cost off: it
// re-establishes the stream in a quiet window of its own, and ABANDONING IT IS FREE
// because it runs only when the builder is already null and so has nothing to spend.
// See kRestreamQuietMs.
//
// SO THE NUMBER IS DERIVED AGAIN, AND FROM A SMALLER COST OF BEING WRONG:
//
//   FLOOR -- 1360 ms, the longest pause measured while the reader was still turning
//   pages (twelve consecutive turns: median gap 72 ms, outliers 898 ms and 1360 ms).
//   Below that the count fires into a gap the reader is about to close, is abandoned,
//   and the abandoning press pays the rewind.
//
//   MARGIN -- 2000 ms is 1.47x that floor, where kRefineQuietMs uses ~3.5x. The
//   multiplier is smaller because the cost of being wrong is smaller: the refinement
//   is 1408 ms of UNINTERRUPTIBLE dead buttons, and this is one rewind on one press,
//   at most once per chapter, because a chapter counted once is never counted again.
//
//   CEILING -- the prize. The total lands on glass at window + count + paint: at
//   5000 that is ~6.0 s (5000 + 440 + 596, from the device's own [index] line), and
//   at 2000 it is ~3.0 s. Half the wait for a footer that currently reads `3 / -`.
//
// AND IT DOES NOT RE-OPEN THE PERCENTAGE-GOES-BACKWARDS BUG, which is the other thing
// this constant has to be checked against -- widening it 1200 -> 5000 made that one
// worse, because the count landed later. It cannot come back, and the reason is
// structural rather than a matter of degree: progressPercent is made of BYTES now
// (reading_store.cpp prefers bytesIntoChapter and reads page/pageTotal only when it
// is zero), and every chapter of every real EPUB is deflated, so the bytes are always
// there. test_reader_restream.cpp asserts the percentage across the moment the count
// lands, over a real archive, and it does not move. For the fallback path -- a stored
// entry or an in-memory chapter, with no inflater to ask -- shortening moves the
// SAME lever in the direction that made it better.
//
// WHAT IT STILL COSTS, stated plainly so the [index] line can be read against it: a
// pause between 2000 ms and 2000 ms + the count's duration ends in an abandon, and
// that press pays a rewind. `[index] counted|abandoned` is what measures how often.
constexpr uint32_t kCountQuietMs = 2000;

// PUTTING THE SPENT STREAM BACK, on a window of its own.
//
// Every quiet-window walk drops the live PageBuilder before it rewinds -- the count
// and the ring warm both -- and until restreamAtCurrentPage existed neither could put
// it back, so the next FORWARD turn paid ~376-1010 ms of seekTo on the button.
//
// WHY THIS ONE MAY HAVE A SHORT WINDOW WHERE THE OTHER TWO MAY NOT. Abandoning it
// costs NOTHING: it runs only when `pb_` is already null, so there is no live builder
// for an interrupted walk to lose. The other two each spend something real before
// they walk, which is what buys them the refinement's long window. Here the trade is
// one-sided -- it finishes and the next forward turn is free, or it is cut and that
// turn pays exactly what it pays today -- so there is no case in which firing early
// makes anything worse.
//
// 1200 ms, and the derivation is only the lower half of kCountQuietMs's: it has to
// clear the 72 ms median gap between steady page turns, and the 898 ms outlier, so
// that a reader flipping does not pay bus traffic for a builder each turn
// re-establishes by itself. It does NOT have to clear the 1360 ms pause, because
// being interrupted there is free -- which is precisely the difference from the two
// windows above.
constexpr uint32_t kRestreamQuietMs = 1200;
static bool gRefineOwed = false;

// WHERE AN IDLE WALK GAVE UP FOR GOOD, SO IT IS NOT ASKED AGAIN THERE (#45).
//
// Both of the quiet-window walks below -- restreamAtCurrentPage and warmPageRing --
// answer `false` for TWO REASONS THAT LOOK IDENTICAL AT THE CALL SITE, and only one
// of them should stop the job being retried:
//
//   * "A BUTTON ARRIVED AND I GAVE UP." Both take a stop predicate answered from
//     rawSamplesPending(), and being cut short is the whole design of an
//     interruptible idle job. That failure MUST stay retryable: the next quiet
//     window is exactly when it should run.
//   * "THIS POSITION CAN NEVER SUCCEED." ReaderScreen::rewalkToCurrentPage only
//     accepts a page the PageBuilder has FILLED, and the trailing partial page comes
//     from finish(), which that walk never calls -- so on the last page of any
//     chapter, and on the only page of a one-page chapter, it returns false however
//     long it is left alone. The shell's gate is `!hasLiveStream()`, which is still
//     true afterwards, so with no memory of the failure the job re-runs on EVERY
//     loop iteration: the device measured ~6 ms of inflate every ~13 ms, forever, on
//     a reader left sitting on a 239-byte cover chapter. warmPageRing joins it at
//     page 0 for the same reason -- it refuses page 0, so its headroom gate never
//     stops being true.
//
// THE SHELL CAN TELL THEM APART WITHOUT TOUCHING core/: ask rawSamplesPending()
// IMMEDIATELY AFTER THE CALL. An empty queue means nothing interrupted the walk, so
// the `false` is the walk's own answer about this position and not a press. That is
// the same instrument the predicate itself reads, one call later.
//
// A POSITION, NOT A FLAG, and that distinction is the whole safety of it. A bare
// bool could never be cleared correctly: the reader pages away and back, and the
// page that could not restream at the end of chapter 4 says nothing about page 12 of
// chapter 5. Keyed on (chapter, page), a memo taken anywhere else simply does not
// match, so it can only ever suppress the exact position that earned it -- and the
// first move off that page makes the job live again with no line to remember. It is
// also FORGOTTEN WHEN THE BOOK CLOSES, for the reason gLastChapter is: page 0 of the
// next book is a different page 0.
struct IdleWalkStuck {
  int chapter = -1;
  int page = -1;
  bool at(int ch, int pg) const { return chapter == ch && page == pg; }
  void note(int ch, int pg) {
    chapter = ch;
    page = pg;
  }
  void forget() {
    chapter = -1;
    page = -1;
  }
};
static IdleWalkStuck gRestreamStuck;
static IdleWalkStuck gWarmStuck;

// HOW LONG THE BUTTONS MUST BE QUIET BEFORE THE READING POSITION IS WRITTEN.
//
// THE POINT OF THIS NUMBER IS THAT A PAGE TURN NEVER PAYS FOR IT. The save is two
// small file reads and up to two small writes -- ~20-100 ms measured, on the display's
// SPI bus -- and unlike the page count and the ring warm, abandoning it costs nothing,
// so the temptation is to fire it almost immediately. That would be the wrong trade:
// at 400 ms it would land after every paint during steady reading and put its whole
// cost in front of the next press, which is precisely the "a card write on each turn
// would be felt" objection that kept this on three edges in the first place.
//
// So it is sized to MISS steady turning and catch the pause that follows it. The
// device's own figures: across twelve consecutive page turns the gap between the panel
// going free and the next press was a median of 72 ms, with the two longest at 898 and
// 1360 ms. 2000 ms clears all twelve, so a reader flipping through pages pays nothing
// at all -- and an average reader spends ~23 s on a page, so in ordinary reading the
// window opens about two seconds after every single turn.
//
// SHORTER THAN kCountQuietMs (5000) DELIBERATELY, because the trade is the opposite
// one. The count and the refinement wait five seconds because being wrong costs the
// reader a locked-up second and a half; being wrong here costs ~40 ms that the next
// press absorbs, and being LATE costs durability, which is the whole feature.
constexpr uint32_t kSaveQuietMs = 2000;

// THE BATTERY POLL'S CADENCE IS NOT A CONSTANT HERE ANY MORE (#96). It is
// BatteryTracker::pollIntervalMs(), because the two intervals are justified entirely
// by their ratio to kUnlatchMs and kCriticalDwellMs and those live in
// battery_tracker.h with their derivations -- a cadence spelled here would be a number
// whose reason is in another file. It is also logic, and `shell/` has no harness.
//
// What is still this file's is the PREDICATE the interval is chosen with. See
// bandRepaintPossible() beside homeOnGlass().

// WHETHER THE POSITION ON SCREEN IS WORTH THE BUS. See progress_save_gate.h: it holds
// the last point actually stored, so the quiet window cannot re-save the same page on
// every loop iteration, and it gives up after three consecutive refusals so a
// write-protected card cannot be hammered into routing the reader to SdMissingScreen.
static reader::ProgressSaveGate gSaveGate;

// Everything the render needs has to outlive setup(), so it lives here rather
// than on setup()'s stack.
//
// ONE frame, AND IT IS THE DRIVER'S. There used to be two live at once and only
// one of them ours: FreeInkDisplay::begin() allocates its own 52,272-byte frame
// (48,000 on the X4) whether we use it or not, and setFramebuffer() memcpy'd
// ours into it on every paint. This is now a VIEW over display.getFrameBuffer()
// -- no second allocation, and nothing between the render and the panel. The
// reference firmware does the same thing, drawing straight into that buffer
// through an orientation-aware coordinate transform.
//
// It is logically portrait (what the screens draw against) and physically
// landscape (what the panel is handed), because it is bound with Rotation::Ccw
// -- see bindFrameToDriver().
//
// std::optional, not unique_ptr: there is nothing left to allocate. The old
// comment here explained that the frame had to be heap-allocated behind a
// unique_ptr because a file-scope Framebuffer would allocate its vector during
// static init, before setup()'s largest-block check could run, and a vector
// that cannot allocate under -fno-exceptions is an abort() boot loop with no
// diagnostic -- this project has already lost a boot to exactly that. A view
// owns no vector, so the hazard is gone with the allocation. An optional keeps
// construction deferred to after display.begin() (getFrameBuffer() is null
// before it) at a stable address in .bss.
static std::optional<reader::Framebuffer> gFrame;
// The bytes gFrame currently views, so a change of them is DETECTABLE. See
// bindFrameToDriver: the driver can take its framebuffer away and give it back.
static const uint8_t* gFrameBytes = nullptr;
// Whether the frame's contents are unknown to the App -- true after a bind and
// until the next full paint. gFrame lives at a fixed address, so App's paint
// record (which compares the Framebuffer's address) cannot tell that the bytes
// behind it were replaced or wiped, and a partial repaint over a wiped frame is
// an overlay panel floating on paper. This flag is what tells it.
static bool gFrameContentsUnknown = true;
// FontSet owns nothing: every Font it holds is a zero-copy view into a blob the
// caller supplies. The embedded kFont* arrays have static storage, so they
// outlive the set -- but nothing here may ever hand load() a scope-limited copy.
static std::optional<reader::FontSet> gFonts;
static reader::QuietTheme gTheme;
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
// THE BATTERY. The monitor picks its backend at RUNTIME from the active board
// profile, which is what lets one C3 binary serve both models: X3 reads a BQ27220
// fuel gauge over I2C, X4 reads an ADC divider. So this must not be constructed
// before detectAndSelectBoard() has run -- it is, but it reads nothing until a
// method is called, and the first call is on the first Home paint.
static BatteryMonitor gBatteryMonitor;
static reader::BatteryTracker gBattery;
// Whether the active board can observe charging at all. Set from the FIRST
// reading, and it is what keeps the band's repaint -- and, since #96, the FAST
// cadence -- off an X4: that profile declares no gauge and no charge-status pin, so
// isCharging() is false there for ever and a poll could never see a change. Battery
// spent for nothing on a device built to sit idle.
static bool gChargingObservable = false;
static bool gBatteryEverRead = false;
// How many times the periodic poll has actually run. Its only other trace is the
// one-shot [battery] boot line and an occasional "-> repainting Home", so without
// this a disarmed poll, a poll pinned by kMaxGrantsPerSession and a poll quietly
// working are all silent in the same way -- reported on [alive] below for the same
// reason the listing cache's hit=/miss= is.
//
// AND SINCE #96 THE COUNT ALONE IS NOT ENOUGH, because there are now two cadences
// and this number cannot say which one produced it: a device stuck on the fast
// interval and one correctly on the slow one differ by 15x in this figure and by
// nothing else. [alive] carries the interval beside it for that reason.
static uint32_t gBatteryPolls = 0;
// When the cadence last had a reason to reset -- file scope because BOTH the
// paint-time read (renderTop) and the periodic poll (loop) stamp it, so a Home
// paint counts as a read for cadence purposes too. Without that a boot in
// particular leaves this at 0, and the periodic poll fires on the very next
// quiet iteration to re-read what the boot paint just read a moment earlier --
// harmless (the tracker will not double-fire a request), but an avoidable I2C
// transaction.
//
// ONLY A PAINT THAT TOOK A READING MAY STAMP IT, and that is the rule #96 must not
// break: the stamp used to be unconditional, which starved the ladder on the one
// screen the banner is drawn on -- a reader turning pages faster than the interval
// pushed the next reading out for ever. It is inside homeOnGlass() in renderTop for
// that reason, and it stays legal there because a Home paint really does feed
// gBattery.update() through refreshBatteryOnHome(). The stamp and the reading are the
// same event; anywhere they are not, there must be no stamp.
static uint32_t gLastBatteryPollMs = 0;
static InputManager gInput;
// The card. One instance: SDCardManager is a singleton underneath, so a second
// SdFileSystem would address the same volume with its own idea of whether it is
// mounted.
static SdFileSystem gSd;
// The screen catalogue is the one from core/, shared with the simulator. A
// shell-local copy would drift from it on row lists and titles, and the drift
// would be invisible because each half would keep passing its own checks.
//
// OVER THE REAL CARD, rooted at /books: that is what makes the Library on the
// device list the user's files rather than the board's seven sample rows. It is
// declared AFTER gSd on purpose -- static initialisation within a translation
// unit runs in declaration order, and this constructor stores a reference to it.
// (Binding a reference to an object whose constructor has not run is not
// undefined here -- SdFileSystem's is trivial and nothing is called until
// setup() -- but relying on that would be relying on a detail of another file.)
//
// It is given the card whether or not the card mounted. A Library over an
// unmounted filesystem lists nothing, which is correct and is also unreachable:
// with no card the app is rooted at the SD-missing screen and there is no way to
// a Library at all.
static reader::DemoScreenFactory gFactory(gSd, reader::kBooksRoot);

// ---------------------------------------------------------------- Wi-Fi
//
// THE V1.1 CONNECT FLOW'S SHELL HALF. core/ owns the screens, the record's
// format and the reason mapping; what lives here is NVS, the radio, and the
// one function that reads an outcome off a screen and acts on it.
//
// IT SHIPPED WITHOUT ANY OF THIS, and the symptom was exact: pressing OPEN on
// Settings' Wi-Fi row did nothing at all. The factory refuses to build
// WifiSettings unless something primes it, App::pushScreen returns false, and
// dispatch's Push case ignores that -- so nothing is marked dirty and nothing
// reaches the glass. A refused push is silent BY DESIGN (it is what makes a
// wake restore stop short of a screen it cannot build) and is indistinguishable
// from a dead button when a finger caused it.
static ArduinoWifiRadio gRadio;
static shellwifi::NvsWifiSink gWifiSink;
static reader::SavedNetworks gWifiNets;
// The ONE join attempt in flight, which is the shell's by design -- spec 4.1b:
// a screen holding it would be the screen that happens to be on top, and the
// flow replaces its own screens as it goes.
static std::string gJoinSsid;
static std::string gJoinPsk;
static bool gJoinLocked = false;
// Whether a scan has been asked for on the picker currently on top, so
// arriving at the picker starts exactly one.
static bool gScanArmed = false;
// Whether the completed scan's rows have been handed over. scanState() stays
// Done once it is Done, so without this the poll called setResults on EVERY
// loop iteration -- and setResults puts the focus back at the top, correctly,
// because the list it indexed no longer exists. The reported symptom was that
// moving through the networks "goes back to the first one on its own": the
// focus was being reset several times a second under the reader's thumb.
static bool gScanDelivered = false;

// THE BODY FACE, and it is resident now rather than a boot-time local. It used to
// be scoped to the check below and released before setup() returned, because
// nothing drew body text; the Reader does. Resident costs its glyph cache --
// 16 KB, sized in reader/scalablefont.h against the measured working set at
// ppem 32 -- for as long as the device is on, which is the right trade against
// re-parsing a 236 KB TTF and re-rasterising every glyph on each page turn.
//
// A file-scope object rather than one owned by the Reader screen: a screen is
// built and destroyed on every push and pop, and a cache that died with the screen
// would make leaving a book and coming back cost a cold rasterisation of the whole
// page. It also has to outlive every ReaderScreen that borrows it.
// THE LOOP TASK'S STACK, AND WHY IT IS NOT THE DEFAULT 8 KB.
//
// stb_image's inflate wants 6,608 bytes in ONE FRAME. Read off the panic that
// found it: `add sp,sp,t0` at the faulting address with T0 = 0xffffe630, which is
// -6608, inside stbi_zlib_decode_noheader_buffer. The compiler inlines
// stbi__parse_zlib, stbi__compute_huffman_codes and stbi__zbuild_huffman into that
// one function, so all three stbi__zhuffman tables -- fast[512] plus size[288] plus
// value[288] each -- live in a single frame. Arduino's default loopTask stack is
// 8,184 usable bytes, and the chain above the inflate (loop -> handleOpen ->
// openChapter -> Zip::read -> inflateRaw) already spends ~1.5 KB of it. Opening any
// book was a stack-protection fault, every time.
//
// 16 KB rather than 12: the inflate peak is ~8.2 KB, buildDocument's own peak is a
// 1 KB tag stack plus an Xml with its 16 attribute slots, and pagination sits on
// top of neither. Doubling leaves ~7.8 KB spare, which is headroom a future caller
// can spend without this needing to be rediscovered by another panic.
//
// SET_LOOP_TASK_STACK_SIZE, not a build flag. `-DCONFIG_ARDUINO_LOOP_STACK_SIZE`
// looks like the obvious fix and does NOTHING: arduino-esp32 ships PRECOMPILED, so
// our -D never reaches its main.cpp. The weak-symbol override in Arduino.h is the
// supported mechanism and the only one that takes effect.
//
// It costs 8 KB, taken from the heap when the task is created -- against ~200 KB
// free at boot. The [stack] line below is what keeps that a measurement.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

static reader::ScalableFont gBody;
// THE ITALIC, WITH ITS OWN CACHE, and the budget is measured rather than defaulted.
//
// 16 KB is right for the ROMAN: it is hot on every line of every page, and the union
// of printable ASCII plus the accents fontc.py subsets is 12,292 B at ppem 32. The
// italic is not hot. Measured over eight real books, `<em>` covers 0.6%-5.8% of a
// chapter's characters over 103 distinct codepoints -- 9,969 B to hold the union of
// all eight books and never evict.
//
// 10 KB therefore holds essentially the whole working set, and the failure mode if a
// book exceeds it is that the arena wraps and re-rasterises: SLOWER, never dead,
// which is the property ScalableFont's bounded budget exists to give. Against a
// measured heap floor of 45,840 bytes with a page on glass, taking 16 KB here for a
// face that sets 3% of the text would have been the easy wrong answer.
//
// BOTH NUMBERS ARE NOW "AT ppem 32" RATHER THAN "ALWAYS", and neither had to change
// to become that: ScalableFont::init scales its budget by the reading size, so what
// these two lines state is the PROPORTION between a hot face and a cold one, which is
// what was actually measured and is what should survive a Typography `Size` row. At
// the shipped ppem 32 the scale is 1 and these are the same 16 KB and 10 KB as before,
// to the byte; at the top of the ramp they cap at 24,576 and 15,360.
static reader::ScalableFont gItalic(10u * 1024u);
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

// WHAT A POSITION SAVE NEEDS ABOUT THE OPEN BOOK, captured when it was opened.
//
// The ReaderScreen knows where the reader IS but not what the book is called or how
// big its file is, and openBook's result is released back to the factory -- so the
// few facts a sidecar and the Home pointer need are kept here rather than re-read
// off the card at save time. `bytes` is the EPUB's file size, which is the staleness
// check: see ReadingPosition::bookBytes for why that and not a checksum.
static struct {
  std::string path;
  std::string title;
  std::string author;
  uint32_t bytes = 0;
  bool open = false;
  // THE BOOK'S CONTENTS, read at OPEN rather than when the list is asked for.
  //
  // Reading it on demand could not work and the numbers were already written down:
  // loadToc re-opens the archive, so it needs a second Inflater (~36,956 bytes of
  // window and tables) plus the zip's 121-entry directory and the epub's 92 chapters --
  // about 48 KB -- and the heap floor with a page on glass is 45,840. It failed to
  // allocate every time, returned empty, and the factory substituted its demo: the
  // device showed Middlemarch's chapters for Le Fleau.
  //
  // CLAUDE.md had the answer under the eager page count: "counting on a second
  // ChapterReader would buy one pass for another 32 KB window against a 45,840-byte
  // floor". Same window, same floor, and I did it anyway one screen later.
  //
  // At OPEN there is room -- openBook has released its archive and the Reader's own
  // inflater does not exist yet, so the heap is ~133 KB. And it is cheap to keep:
  // measured 1,161 bytes of labels for a 96-entry book, ~12 a row.
  std::vector<reader::TocEntry> toc;
} gReading;

// HOME'S VIEW MODEL IS BUILT ONCE AND HAS TO BE REBUILT, which is the whole of a bug
// the device reported: after reading a book, going Home still said NOTHING OPEN YET.
// Home is the App's ROOT, so returning to it hands back the same instance with the
// view model it was constructed with -- and that one was built at boot, before any
// pointer existed.
//
// A REBUILD USED TO BE THE MOST EXPENSIVE THING ON A NAVIGATION, which is why this
// is a flag and not an unconditional refresh: homeVmForCard() counts /books, and a
// listing costs ~2.7 ms an ENTRY on this card -- ~1.1 s on a 203-book library, since
// macOS writes a `._name` beside every file. Paying that on every Back to Home would
// be a second's pause on a navigation that is currently instant.
//
// The count is CACHED now (see libraryCountForHome), so the rebuild costs a
// last.json read and an exists() rather than a second of listing -- but the flag
// stays, because "cheap" is not "free" and the rule it encodes is still the honest
// one: rebuild when the thing Home draws has changed, not on a timer.
//
// AND IT USED TO BE A BARE `bool`, WHICH MADE IT A CALLER LIST -- the shape this
// project's own rule calls a function not yet written. It was set in exactly one place,
// saveReadingPosition, on the stated grounds that "nothing else on the device moves
// that block"; a DELETE moves it too, and set nothing, so Home kept the count it was
// born with until a position was saved, a card was re-inserted, or the device rebooted
// (#43, reported off an X3 with a 208-book card). The count's own cache was already
// keyed on gSd.removals() -- the invalidation was right and nothing ever asked it.
//
// So the book-left half is DERIVED from that same counter now and only the
// position-moved half is latched. See reader/home_rebuild.h, which holds the split and
// its reasoning, and which lives in core/ because shell/ has no harness and the trap
// here -- a rebuild that does not re-stamp the counter rebuilds Home on every loop
// iteration for the rest of the session -- is invisible on a desktop.
static reader::HomeRebuildGate gHomeRebuild;

// THE SAME FACT, FOR THE OTHER SCREEN THAT DRAWS IT. The Library gives every row a
// percentage or NEW, and it is built ONCE -- when it is pushed. The Reader is pushed
// ON TOP of it, so the pop that leaves a book hands back that same screen with the
// rows it was born with: a book just read to 31% still read NEW, which is what was
// reported off the device.
//
// TWO FLAGS AND NOT ONE, because they are consumed at different moments and each
// clears its own: Home rebuilds when it is the root and on top, the Library refreshes
// when IT is on top. Sharing one would let whichever screen was reached first clear it
// for the other -- Library, Back, Home would leave Home stale.
//
// Set in the one function that changes reading progress, and consumed in loop() after
// the dispatch, which is what makes the pop that reveals the Library the press that
// refreshes it.
static bool gLibraryStale = false;

// The spine Contents chose, or -1. Held for exactly one dispatch: the choice is made
// while Contents is on top and acted on once the pop has put the Reader back.
//
// IT NO LONGER JUMPS THE READER. The pop that Contents' GO returns opens a PEEK over
// the page instead -- the same capture, a different thing done with it -- because a
// contents list cannot answer "is this the chapter I meant?" and being wrong about a
// chapter used to cost the walk there and the walk back.
static int gPendingSpine = -1;

// A PEEK IS ON THE STACK, so the Reader's chapter has been released and has to be taken
// back when the panel goes. Tracked rather than inferred from the stack, because the pop
// that removes the peek is what makes the answer needed and the stack no longer says a
// peek was ever there.
static bool gPeekOpen = false;
// WHICH CHAPTER THE CROSSING DETECTOR LAST SAW, and -1 for "no book open".
//
// IT WAS A FUNCTION-LOCAL STATIC INSIDE loop() AND IT COULD NEVER BE INITIALISED,
// which the device showed: opening a peek logged `[chapter] spine=55 ... in 0ms` and
// ran a save for a chapter that had not changed. The detector is gated on the Reader
// being on TOP, and `handleOpen()` -- which pushes the Reader -- runs BELOW it in the
// same iteration. So on the press that opens a book the detector looks while Home is
// still on top, the static stays -1, and the FIRST later press that leaves the Reader
// on top fires a crossing for the chapter the reader is already in. A plain page turn
// did it too; the peek is only where it was noticed.
//
// It costs two sidecar reads and a stray log line rather than a wrong screen, which is
// why it survived. Recorded at file scope now and SET BY openBookAt, which is the one
// function a button press and a wake both go through and the moment the chapter
// becomes known -- so the detector fires on crossings and nothing else.
//
// RESET WHEN THE BOOK CLOSES, because it outlives one book otherwise: opening a second
// book at the same spine index as the first was left on would suppress the next real
// crossing, which is the same defect wearing the opposite sign.
static int gLastChapter = -1;
// WHAT THE PEEK CHOSE, taken while it is still on top -- the dispatch pops it, and after
// that there is no screen left to ask. Three values rather than a pointer, because the
// screen is gone by the time they are used.
static bool gPeekCommitted = false;
static int gPeekSpine = 0;
static reader::Cursor gPeekCursor{};

// --- ONE LINE PER INTERACTION ------------------------------------------------
//
// Everything from the button going down to the panel being finished with, as a
// single greppable record. It exists because the cost was spread across four log
// families that could not be added up: `[input]` said a press happened, `[paint]
// done` said what the panel cost, and the stretch between them -- a Library
// rescan, an archive open for an author, a chapter jump, an NVS write, two SD
// writes for a reading position -- had no line at all. A navigation that felt slow
// could not say which part was slow, which is how a second of directory listing
// sat on the critical path of a Back with nobody able to name it.
//
// A BURST IS ONE INTERACTION. Several events can drain before a single paint --
// that is the coalescing the loop exists to do -- so this reports the FIRST
// event's timestamp against the paint that eventually satisfied it, with `ev=N`
// saying how many presses went into that frame. Reporting per event would divide
// one visible response between N lines and make every one of them look fast.
//
// The fields, in the order the time is spent:
//   wait   -- the event's own timestamp to this loop picking it up. Raw queue,
//             loop wake, drain. NOT the whole input latency: the input task's
//             10 ms poll and the SDK's 5 ms debounce happen before the timestamp
//             exists and are only knowable from the constants.
//   pre    -- work done because of what is on top BEFORE the dispatch: the details
//             author's archive open, the contents hand-over, the position save on
//             the way out of a book.
//   disp   -- App::dispatch. A push builds a screen, so a Library's rescan is here.
//   post   -- everything the dispatch made necessary: handleOpen, a chapter jump,
//             Home's rebuild, the session record.
//   render -- drawing the frame, all passes.
//   up     -- the plane upload to the controller, before the waveform starts.
//   wave   -- the waveform, plus the baseline sync that follows it.
//   ser    -- how much of `total` was this device talking to the USB host. See
//             logf(): unplugged it is ~0, and `net` is then the whole story.
//   net    -- total minus ser. THE NUMBER TO COMPARE ACROSS RUNS.
struct Interaction {
  bool pending = false;
  uint32_t at = 0;      // InputEvent::at of the first event of the burst
  uint32_t popped = 0;  // millis() when this loop began handling it
  uint32_t preMs = 0, dispMs = 0, postMs = 0;
  uint32_t logAtStart = 0;
  int events = 0;
  reader::Button button = reader::Button::Back;
  reader::PressKind kind = reader::PressKind::Short;
  // screenName returns a string literal, so holding the pointer is safe and
  // holding a std::string here would allocate on the path being measured.
  const char* from = "";
};
static Interaction gAct;
static uint32_t gInteractionSeq = 0;

// Bring-up instrumentation. Serial here is native USB CDC, so the port
// re-enumerates when the app starts and anything printed in the first second is
// lost to the host. Every stage is announced and the last one reached is
// repeated from loop(), so a hang can be located by attaching at any time.
static const char* stage = "boot";

// --- THE COST OF WATCHING ----------------------------------------------------
//
// SERIAL BLOCKS WHEN A HOST IS ATTACHED, AND IS FREE WHEN ONE IS NOT. Both halves
// matter and the second is why this was never noticed:
//
//   * unplugged -- HWCDC::write and HWCDC::flush both short-circuit on
//     `!isCDC_Connected()` and just drain the ring. Microseconds. This is the
//     device's real behaviour, since it spends its life on battery.
//   * plugged    -- write() sends what fits the TX ring and then BLOCKS until the
//     host takes the rest; flush() spins `delay(1)` until the ring empties, up to
//     tx_timeout_ms (100). So a burst of lines costs real milliseconds.
//
// The consequence is that EVERY TIMING TAKEN OVER USB IS INFLATED BY THE CABLE,
// and a device measured while being watched is not the device. This project has
// already paid once for a measurement artefact read as a device fact -- a 2.5 s
// delay in setup() was recorded as the panel detection's cost because the first
// timestamped line was read as time zero.
//
// So the cost is MEASURED rather than removed. Every log site inside a paint's
// critical window goes through logf(), which accumulates into gLogMs, and the
// per-interaction line reports it as `ser=` beside a `net=` with it subtracted.
// Unplugged that field reads ~0 and `net == total`, which is the proof that the
// numbers either side of it are the device's own.
static uint32_t gLogMs = 0;

static void logTee(const char* s, size_t n);

static void logf(const char* fmt, ...) {
  const uint32_t t0 = millis();
  va_list args;
  va_start(args, fmt);
  // vprintf rather than a formatted buffer: Print::printf builds into a stack
  // buffer of its own and this part is not what costs anything.
  char line[512];
  const int n = vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  const size_t took = n > 0 ? (static_cast<size_t>(n) < sizeof(line) ? static_cast<size_t>(n)
                                                                     : sizeof(line) - 1)
                            : 0;
  if (took > 0) Serial.write(reinterpret_cast<const uint8_t*>(line), took);
  // THE ONE CHOKE POINT, which is why the tee is one line: every print site in this
  // file goes through logf, so the card log and the serial log cannot diverge about
  // what happened. It is counted inside gLogMs deliberately -- a memcpy into RAM is
  // microseconds, and pretending it is free is the habit that produced `ser=`.
  logTee(line, took);
  gLogMs += millis() - t0;
}

// The flush half, timed the same way. SEPARATE FROM logf ON PURPOSE: this file has
// 92 print sites and 63 flushes, so folding the flush into logf would ADD one at
// the 29 sites that deliberately do not have it -- a behaviour change smuggled in
// under a measurement change, and a slower device than the one being measured.
// Every existing Serial.flush() became one of these and nothing else moved.
// --- THE LOG ON THE CARD ------------------------------------------------------
//
// Everything logf() writes is TEED into a RAM buffer and appended to /encre.log in
// an idle window. It exists for the one class of fault the cable cannot see: serial
// write and flush short-circuit when no host is attached and BLOCK when one is, so
// a timing taken over USB is not the device's -- and attaching after a sleep can
// reset the chip, which turns the wake being investigated into a cold boot.
//
// THE MEASUREMENT MUST NOT MAKE THE THING IT MEASURES. That is the whole design
// here, and it is why this is a buffer and not a write per line:
//
//   * A CARD WRITE COSTS ~40 ms, measured (`[fs] writeAll ... in 40ms`), and takes
//     the DISPLAY'S SPI BUS. One per log line would put tens of milliseconds into
//     every interaction -- and a delay is precisely what is being hunted, so the
//     instrument would be indistinguishable from the fault.
//   * SO IT FLUSHES ONLY WHEN THE PANEL AND THE BUTTONS ARE BOTH QUIET, under the
//     same gate the card-presence poll uses, and never inside a paint.
//   * AND IT REPORTS ITS OWN COST, for the reason `ser=` exists: an instrument that
//     hides its own weight lets you attribute it to the device.
//
// Bounded on both sides: 4 KB of RAM, and the file is truncated and restarted past
// kLogFileCapBytes so a device left running cannot fill the card.
constexpr size_t kLogBufBytes = 4096;
// Flush at three quarters rather than at full: a burst arriving after the threshold
// still has room, so the newest lines are not the ones dropped.
constexpr size_t kLogFlushAtBytes = 3072;
constexpr uint32_t kLogFileCapBytes = 256u * 1024u;
constexpr const char* kLogPath = "/encre.log";

// THE ARRAY IS THE SHELL'S AND THE RULES ARE core/'s. reader::CardLogBuffer holds
// the three-state arming, the append, the drop counting and the flush threshold --
// all of it bytes in and bytes out, and all of it the kind of logic `shell/` has no
// harness to check. This file keeps the 4 KB itself (nothing in `core/` allocates)
// and owns the one part a desktop test cannot reach: the card write.
//
// THE STATE STARTS AS `Pending`, WHICH IS THE WHOLE FIX FOR #47/#69. The setting
// lives on the card, so it cannot be read until the card is mounted -- hundreds of
// lines below here -- and the lines this feature exists to capture are all printed
// before that: [wake] refused / [wake] held, the [prev] crumb record, the reset
// reason, the storage bring-up. So the tee is armed from the first line of boot and
// the setting decides, afterwards, whether what it holds is kept (Enabled) or thrown
// away (Disabled). Nothing may be WRITTEN while Pending, which costs nothing: there
// is no mounted volume to write to that early anyway.
static char gLogBuf[kLogBufBytes];
static reader::CardLogBuffer gCardLog(gLogBuf, sizeof(gLogBuf));
static uint32_t gLogSdMs = 0;  // time spent writing the card, cumulative

// Append into the buffer. Never blocks, never allocates, never touches the card.
static void logTee(const char* s, size_t n) { gCardLog.append(s, n); }

// Write what is buffered. Returns the milliseconds it cost, which the caller logs
// -- see the header note: an instrument that hides its own weight lets you
// attribute it to the device.
//
// `landed` IS AN OUT-PARAM RATHER THAN NOTHING, because the alternative is a line
// that says `wrote 3072B in 2ms` about a write-protected card that took nothing.
// The bytes really are gone either way (see CardLogBuffer::wrote), so the only
// question is whether the log lies about where they went -- and a false claim is
// worse than an absent one. Defaulted to null so the two sleep-path callers, which
// have nobody left to tell, are unchanged.
static uint32_t flushLogToCard(bool* landed = nullptr) {
  if (landed != nullptr) *landed = false;
  if (!gCardLog.enabled() || gCardLog.size() == 0) return 0;
  const uint32_t t0 = millis();
  const bool ok = appendToCard(kLogPath, gCardLog.data(), gCardLog.size(), kLogFileCapBytes);
  if (landed != nullptr) *landed = ok;
  // DROPPED EITHER WAY, and CardLogBuffer::wrote is what does it. A card that
  // refuses the write must not make the buffer grow until it starts losing lines
  // silently -- and a log that stops the device working is worse than no log. The
  // failure shows up as a gap plus the dropped count on the next line that lands.
  gCardLog.wrote(ok);
  const uint32_t took = millis() - t0;
  gLogSdMs += took;
  return took;
}

static void logFlush() {
  const uint32_t t0 = millis();
  // NOT logFlush(). The sweep that turned every `Serial.flush();` in this file
  // into a `logFlush();` matched this line too, because this function's body IS
  // one of the call sites it was rewriting -- so it replaced itself with a call to
  // itself and would have been a stack-protection fault on the first log line.
  // CLAUDE.md records the identical shape from a previous session; a pattern that
  // matches more than was meant is the whole family.
  Serial.flush();
  gLogMs += millis() - t0;
}

// Every bring-up stage already prints, so carrying the heap on that line turns
// the existing stage trail into a heap TRACE for nothing -- and the trace is what
// a single figure cannot give.
//
// The first run of `minHeap` reported a 72 KB transient dip during boot: larger
// than the framebuffer, unaccounted for, and invisible to `free` because it
// happens BETWEEN two lines. It matters because 3B's buffers get sized against
// what looks free, while the real ceiling is that much lower. `min` here falls at
// exactly the stage that spent it, which is the whole bisect in one boot.
// --- Breadcrumbs across a sleep ---------------------------------------------
//
// Some faults happen ONLY on a wake, and a wake cannot be watched: deep sleep
// powers down USB, and a host attaching afterwards can reset the chip -- three
// attempts to capture a resume came back as cold boots (see CLAUDE.md). So the
// device records what happened to it and the NEXT boot prints the record.
//
// RTC memory survives deep sleep and does not survive a power cycle, which is
// exactly the lifetime wanted: the record describes the sleep/wake cycle just
// ended and never a stale one from days ago. The magic guards against reading
// uninitialised RTC bytes as a record.
//
// Deliberately small and fixed-size. No allocation, no pointers -- RTC memory
// outlives the heap it would point into, so a pointer stored here is a dangling
// pointer by construction.
struct WakeCrumbs {
  uint32_t magic;
  uint8_t resetReason;
  uint8_t wakeCause;
  uint8_t mountOk;        // did bringUpStorage() find usable storage
  uint8_t probeFirstDone; // has the first card probe run at all
  uint8_t probeFirstOk;   // ...and did it pass
  uint32_t probeFirstMs;
  uint32_t cardLostMs;    // 0 = the card never stopped answering
  uint32_t firstPaintMs;
  char lastStage[28];
  char lostBy[56];
};
static WakeCrumbs gCrumbs;
static constexpr uint32_t kCrumbMagic = 0x454E4352u;  // "ENCR"

// IN NVS, NOT RTC MEMORY, and the first version got this wrong in a way that
// wasted a reproduction.
//
// RTC_DATA_ATTR looked ideal: free to write, survives deep sleep, gone on a power
// cycle. But ESP-IDF's startup RE-INITIALISES `.rtc.data` on every reset that is
// not a deep-sleep wake -- and the reset we have to survive is exactly the one a
// host causes by attaching (reset reason 11, ESP_RST_USB). So the act of plugging
// in to read the record was what destroyed it: the fault was reproduced, the
// cable went in, and the log came back with no [prev] line at all.
//
// Flash survives everything. The cost is NVS writes, which is why this is written
// at a few decisive points rather than continuously, and why the payload is a
// fixed 100-odd bytes: `encre_diag` is wear-levelled by NVS and a handful of
// writes per boot is the same order as the session record already does.
static constexpr const char* kCrumbNs = "encre_diag";
static constexpr const char* kCrumbKey = "prev";

static void saveCrumbs() {
  Preferences p;
  if (!p.begin(kCrumbNs, false)) return;  // diagnostics must never break a boot
  p.putBytes(kCrumbKey, &gCrumbs, sizeof(gCrumbs));
  p.end();
}

// Print the previous cycle's record, then start a fresh one.
static void reportAndResetCrumbs(esp_reset_reason_t rst, esp_sleep_wakeup_cause_t wake) {
  {
    Preferences p;
    if (p.begin(kCrumbNs, true)) {
      WakeCrumbs prev{};
      if (p.getBytesLength(kCrumbKey) == sizeof(prev) &&
          p.getBytes(kCrumbKey, &prev, sizeof(prev)) == sizeof(prev))
        gCrumbs = prev;
      p.end();
    }
  }
  if (gCrumbs.magic == kCrumbMagic) {
    logf("[prev] the boot before this one: reset=%u wake=%u mount=%s "
         "firstProbe=%s@%lums firstPaint=%lums lastStage=%s\n",
         (unsigned)gCrumbs.resetReason, (unsigned)gCrumbs.wakeCause,
         gCrumbs.mountOk ? "ok" : "FAILED",
         !gCrumbs.probeFirstDone ? "never-ran"
                                          : (gCrumbs.probeFirstOk ? "ok" : "FAILED"),
                  (unsigned long)gCrumbs.probeFirstMs, (unsigned long)gCrumbs.firstPaintMs,
                  gCrumbs.lastStage[0] ? gCrumbs.lastStage : "(none)");
    if (gCrumbs.cardLostMs != 0)
      logf("[prev] ...and the card stopped answering at %lums, detected by: %s\n",
           (unsigned long)gCrumbs.cardLostMs, gCrumbs.lostBy);
    else
      logf("[prev] ...and the card answered for the whole of it\n");
    logFlush();
  }
  gCrumbs = WakeCrumbs{};
  gCrumbs.magic = kCrumbMagic;
  gCrumbs.resetReason = static_cast<uint8_t>(rst);
  gCrumbs.wakeCause = static_cast<uint8_t>(wake);
  saveCrumbs();  // so a boot that dies before the next save still leaves the reason
}

static void mark(const char* s) {
  stage = s;
  // The last stage reached. RAM only -- an NVS write per stage would be a dozen
  // flash writes a boot for a field that only matters at the decisive points
  // below, where it is flushed with the rest of the record.
  snprintf(gCrumbs.lastStage, sizeof(gCrumbs.lastStage), "%s", s);
  // millis() FIRST, because a stage line without one is how a boot cost gets
  // attributed to the wrong thing. These lines carried heap and no time, so the
  // only timestamps in a boot log came from the SDK -- and the first of those was
  // read as time zero, which put a 2.5 s delay in setup() down as the panel
  // detection's cost. Everything before the first timestamp is invisible, so
  // every stage gets one.
  logf("[stage] %lums %s heap=%u min=%u\n", (unsigned long)millis(), s,
       (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());
  logFlush();
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
    logf("[sd] %s: no usable storage\n", why);
    logFlush();
    return false;
  }
  const bool wasFirst = !gSdBeganOnce;
  gSdBeganOnce = true;
  if (gSd.probe() && gSd.mounted()) {
    logf("[sd] %s: storage usable (mount confirmed by a root-directory read)\n", why);
    logFlush();
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
    logf("[sd] %s: begin() succeeded but the card would not answer a directory "
         "read; not treating storage as usable\n",
         why);
  } else {
    logf("[sd] %s: begin() returned true WITHOUT touching the card -- it "
         "short-circuits on its own `initialized` flag -- and the card is not "
         "answering. A card that mounted once and was then pulled cannot be "
         "re-mounted without a REBOOT; staying on the SD-missing screen (and this "
         "caller should have restarted instead -- see handleRetry)\n",
         why);
  }
  logFlush();
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

// Push `gSettings` into the two objects this function owns. Factored out of
// loadAndApplySettings because the Settings SCREEN needs exactly this and nothing
// else: it has already changed the struct, and re-reading the file would undo the
// change it is trying to make.
//
// THREE THINGS ACT ON gSettings AND THIS PUSHES TWO. The third is the reader's
// PageMetrics: `bodyPpem`, `margins`, `lineSpacing` and `justify` are consumed by
// Theme::readerMetrics and by the body face, and neither is touched here.
//
// IT IS NOT FIXED BY RECOMPUTING HERE, and that was checked rather than assumed:
// this function runs at boot before the font ramp exists, so it cannot call
// readerMetrics at all.
//
// THE THIRD CONSUMER IS THE TYPOGRAPHY APPLY PATH, which exists now -- this
// comment said it "belongs to" that path while there was none, and then that the
// PageMetrics is "set once in setup()", both of which have stopped being true.
// `applyBodyPpem` below re-rasterises the faces at the chosen size and the loop's
// gTypographyDirty branch recomputes readerMetrics and calls
// ReaderScreen::relayout, which re-paginates the open chapter at the reader's own
// page. So a change made through the panel reaches an open book.
//
// WHAT IT STILL DOES NOT REACH IS A SETTING THAT ARRIVES FROM THE FILE rather than
// from the panel, and that gap is unchanged: boot with no card (defaults, margins
// 18), insert a card whose settings.json says 30, press RETRY, and gSettings is 30
// while pages are still laid at 18, because loadAndApplySettings takes this path
// and not the apply path.
//
// AND `bodyPpem` IS THE SHARPER HALF OF THE SAME GAP, on a plain boot: setup()
// inits gBody at the CONSTANT reader::kBodyPpem, well before loadAndApplySettings
// has read the file, so a stored size does not survive a reboot. Named here rather
// than fixed, because the fix is a boot-order question and not a settings push.
static void applySettings() {
  gRefresh.setCadence(gSettings.fullRefreshEvery);
  gRefresh.setFullOnTransition(gSettings.fullOnTransition);
  gIdle.setTimeout(gSettings.sleepAfterMs);
}

// RE-RASTERISE THE BODY FACES AT THE CHOSEN SIZE.
//
// The Typography panel's preview draws with the SAME face object the reader draws
// with, which is what makes it a live preview rather than a second approximation of
// one -- and it is affordable only because that panel is a full screen: for as long
// as it stands the reader's page and metrics are stale, and nothing draws them.
//
// THE ARENA GROWS, AND THE PEAK IS BOTH ARENAS AT ONCE. ScalableFont::init takes the
// new block before releasing the old, so ppem 46 costs 24,576 + 16,384 for the roman
// transiently. Against a reading floor of 42,152 bytes that is tight, which is why
// the caller shrinks the reader's page ring on the way INTO the panel -- see the
// pre-dispatch block in loop().
//
// A FAILED init KEEPS THE ARENA IT HAD and returns false, so an out-of-memory device
// is slower rather than dead. It is logged, because a size that silently did not take
// is a screen that looks like it ignored a button.
static void applyBodyPpem() {
  const uint32_t t = millis();
  const bool okBody = gBody.init(kFontBodySerif, kFontBodySerifSize, gSettings.bodyPpem);
  const bool okItalic =
      gItalic.init(kFontBodySerifItalic, kFontBodySerifItalicSize, gSettings.bodyPpem);
  logf("[typo] body ppem=%d roman=%s italic=%s line=%d in %lums free=%u min=%u\n",
       gSettings.bodyPpem, okBody ? "ok" : "FAILED", okItalic ? "ok" : "FAILED",
       gBody.lineHeight(), (unsigned long)(millis() - t), (unsigned)ESP.getFreeHeap(),
       (unsigned)ESP.getMinFreeHeap());
  logFlush();
}

// WHETHER A COMMIT MOVED ANYTHING THE READER'S LAYOUT DEPENDS ON, consumed after the
// pop that leaves the Typography panel. Set by the sink, because the last step may be
// the very press being dispatched -- reading the screen before the dispatch would be
// too early, and after the pop there is no screen left to ask.
static bool gTypographyDirty = false;

// Where the Settings screen's changes go. See reader::SettingsSink: this is the
// one place that both APPLIES a change and persists it, which is why the interface
// has a single method rather than two.
//
// Applied FIRST and persisted second, deliberately. The user has pressed a button
// and expects the device to behave differently; a card that has gone read-only
// must not also cost them the change until the next boot. So the refresh policy
// and the idle timer are updated whatever the file does, and a failed write is
// reported as a failed WRITE rather than as a setting that did not take.
class ShellSettingsSink : public reader::SettingsSink {
 public:
  bool commit(const reader::Settings& s) override {
    // THE PPEM IS THE ONE FIELD THAT COSTS SOMETHING TO APPLY, so it is asked about
    // rather than applied blindly: a commit from the SETTINGS screen never touches
    // typography, and re-initing at the same size would flush both glyph caches for
    // nothing -- ~127 glyphs to re-rasterise at ~3,794 us each the next time a page
    // is drawn.
    const int wasPpem = gSettings.bodyPpem;
    const int wasMargins = gSettings.margins;
    const int wasLead = gSettings.lineSpacing;
    const bool wasJustify = gSettings.justify;
    gSettings = s;
    applySettings();
    if (gSettings.bodyPpem != wasPpem) applyBodyPpem();
    // ANY of the four, not just the ppem: margins change the column, and line spacing
    // and alignment change the layout, all without touching a face.
    if (gSettings.bodyPpem != wasPpem || gSettings.margins != wasMargins ||
        gSettings.lineSpacing != wasLead || gSettings.justify != wasJustify)
      gTypographyDirty = true;
    // The factory holds a COPY, because it is what constructs the screen and the
    // screen is handed its starting values. Without this, closing Settings and
    // reopening it would show the values from before the change -- the struct
    // would be right, the refresh policy would be right, and the screen would be
    // the one thing still lying.
    gFactory.setSettings(gSettings);
    const bool wrote = reader::saveSettings(gSd, gSettings);
    logf("[settings] sleepAfterMs=%lu fullRefreshEvery=%d fullOnTransition=%d "
         "ppem=%d margins=%d lead=%d justify=%d -> %s\n",
         (unsigned long)gSettings.sleepAfterMs, gSettings.fullRefreshEvery,
         (int)gSettings.fullOnTransition, gSettings.bodyPpem, gSettings.margins,
         gSettings.lineSpacing, (int)gSettings.justify,
         wrote ? "applied and saved"
                        : "APPLIED BUT NOT SAVED (the change is live; it will not survive a "
                          "reboot)");
    logFlush();
    return wrote;
  }
};
static ShellSettingsSink gSettingsSink;

// Read the settings and apply them. Safe with an unmounted filesystem: every
// FileSystem method fails when mounted() is false, so loadSettings() falls back
// to defaults and this reports exactly that.
static void loadAndApplySettings() {
  const bool ok = reader::loadSettings(gSd, gSettings);
  if (ok) {
    logf("[boot] settings loaded from %s\n", reader::kSettingsPath);
  } else {
    const SettingsVerdict v = settingsFailure(gSd);
    logf("[boot] settings %s: %s\n", v.defaulted ? "DEFAULTED" : "CORRECTED", v.reason);
  }
  applySettings();
  // THE FOUR TYPOGRAPHY FIELDS ARE ON THIS LINE TOO, because a device booting with a
  // hand-edited size must say so -- and because `bodyPpem` here is what the FILE says,
  // which is not necessarily what gBody was inited at (see applySettings).
  //
  // AND `logToCard`, WHICH WAS MISSING AND IS HALF OF #69. It was the one field in
  // the struct with no line reporting it, so a card asking for a card log and a
  // firmware ignoring the request looked identical -- which is exactly how the
  // request went unimplemented for two phases. An instrument that reports on less
  // than it claims is worse than none.
  logf("[boot] settings in force: sleepAfterMs=%lu fullRefreshEvery=%d "
       "fullOnTransition=%d ppem=%d margins=%d lead=%d justify=%d logToCard=%d\n",
       (unsigned long)gSettings.sleepAfterMs, gSettings.fullRefreshEvery,
       (int)gSettings.fullOnTransition, gSettings.bodyPpem, gSettings.margins,
       gSettings.lineSpacing, (int)gSettings.justify, (int)gSettings.logToCard);
  logFlush();

  // THE ONE PRODUCER OF THE CARD LOG'S ARMING, AND #47 IS THAT IT DID NOT EXIST:
  // gLogToCard was read at four sites and assigned at none, so the buffer, the idle
  // flush, the dropped-byte counting and the 256 KB cap had never run on any device.
  //
  // ARMED HERE AND NOWHERE ELSE, from the FILE. Not in applySettings(), which the
  // Settings screen's sink also calls: `logToCard` is a diagnostic rather than a
  // preference and has no Settings row, so the screen is not its author, and routing
  // it through there would let a commit whose copy of the struct had lost the field
  // silently switch the log off mid-session.
  //
  // Called twice per device life at most: here at boot, and again from handleRetry()
  // when a card that was absent at boot has appeared. applySetting() is idempotent
  // for the same answer precisely so the second call cannot discard what the first
  // one has been accumulating.
  //
  // THE RETRY PATH IS ALSO THE ONE STATED LOSS. A device that booted with no card
  // decided `off` and threw the boot buffer away; if the card that then appears asks
  // for a log, the tee arms from that point and the boot preamble is gone. It is not
  // recoverable and it is not worth recovering -- at the moment the question was
  // asked, the only answer available was the default -- so the line below says which
  // of the three transitions this was rather than leaving them to look alike.
  using LogState = reader::CardLogBuffer::State;
  const LogState before = gCardLog.state();
  gCardLog.applySetting(gSettings.logToCard);
  if (gCardLog.enabled()) {
    // FLUSH THE BOOT PREAMBLE NOW, and this is not merely tidy. Every line from
    // Serial.begin() to here is in the 4 KB buffer, and the next legal flush is a
    // quiet window in loop() -- which is after the first paint, several thousand
    // more bytes of stage lines, font timings, library scan and session restore
    // later. Boot does not fit in 4 KB, so without this the log would open with a
    // HOLE exactly where the wake diagnostics are, and a hole is the one thing this
    // design says a diagnostic must never have.
    //
    // It is safe here for the same two reasons the settings file's own creation is:
    // the card is mounted (loadSettings just read it) and, at BOOT, nothing has been
    // painted -- so the display's bus is idle and there is no frame the user is
    // waiting for. On the RETRY path there IS a repaint owed, and this puts ~40 ms in
    // front of it; that is the same bus the settings read on the line above just
    // took, on a press whose whole point is re-reading the card, so it buys the boot
    // log at a cost the retry was already paying. The guard is recursive and taken
    // anyway, structurally, as every other user of that bus does.
    if (gCardLog.size() > 0) {
      SpiBusGuard bus;
      const unsigned buffered = static_cast<unsigned>(gCardLog.size());
      bool landed = false;
      const uint32_t took = flushLogToCard(&landed);
      logf("[log] %s, teeing to %s: %s %uB in %lums\n",
           before == LogState::Pending
               ? "armed"
               : (before == LogState::Disabled ? "armed late, so the boot preamble is "
                                                 "not in the file"
                                               : "still armed"),
           kLogPath, landed ? "wrote" : "COULD NOT WRITE", buffered, (unsigned long)took);
      logFlush();
    }
  } else if (before != LogState::Disabled) {
    // SAID SO, because the alternative is silence in both directions: with the tee
    // off there is no [log] line anywhere, which is indistinguishable from the
    // firmware ignoring the setting -- the state #47 was reported from. Only on the
    // transition, so a RETRY on a card that says no does not repeat it.
    logf("[log] logToCard is off, so nothing is written to %s%s\n", kLogPath,
         before == LogState::Enabled ? " from here on" : " and the boot buffer was discarded");
    logFlush();
  }
}

// --- /books ---------------------------------------------------------------
//
// A CARD WITH NO /books: CREATE IT. The plan left this open ("offer to create it,
// or show an empty library; decide, and say which in a comment"), so here is the
// decision and the argument for it.
//
// Creating it wins on discoverability, which is the only thing at stake. A
// first-run device that shows an empty Library is indistinguishable, from the
// user's side, from a device that cannot read the card: same screen, same absence
// of books, no hint about what to do. Creating the directory turns that into a
// visible instruction -- they mount the card over USB, see a `books` folder next
// to `.reader`, and the place to put an EPUB is obvious without a manual. It also
// makes the Library's root a real directory, so `list()` succeeds and "empty" and
// "unreadable" stop being the same observation up in the UI.
//
// It is the same reasoning, and the same shape, as the settings file this file
// already writes at boot: give the user a hand-editable artefact rather than an
// invisible convention.
//
// WHAT IT COSTS: a write on first boot, on a card that may be read-only, full or
// failing. So the failure is handled rather than assumed away:
//
//   * mkdirs() reports the END STATE (it is idempotent and returns true for a
//     directory that already exists), so "already there" and "created" are told
//     apart by the exists() check, and every outcome is logged.
//   * A FAILED create is not fatal and not hidden. The Library still opens; it
//     lists nothing, because there is nothing to list. What it must not do is
//     claim the card is fine -- so the failure is logged with the three causes
//     that produce it, and Home's LIBRARY row shows no count at all rather than a
//     0 (see homeVmForCard). A 0 would be a lie about a card we could not read; a
//     blank is an honest absence.
//   * Nothing here touches the SD-missing screen. A card that mounts, reads and
//     will not accept a mkdir is a working card with a problem, not a missing
//     one, and routing it to the no-card prompt would be the lie in the other
//     direction.
//
// Called once per confirmed mount, beside armCardProbes and for the same reason:
// this is the point where there is definitely a volume to write to.
static void ensureBooksDir(const char* why) {
  if (gSd.exists(reader::kBooksRoot)) return;  // the ordinary case, and silent
  if (gSd.mkdirs(reader::kBooksRoot)) {
    logf("[sd] %s: no %s on the card, so it was created -- put EPUBs there and they "
         "appear in the Library\n",
         why, reader::kBooksRoot);
  } else {
    logf("[sd] %s: %s does not exist and could NOT be created (card write-protected, "
         "full, or failing). The Library will open and list nothing, and Home's "
         "LIBRARY row will show no count rather than a 0\n",
         why, reader::kBooksRoot);
  }
  logFlush();
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
      logf("[sd] %s: no settings file on the card, so this build's defaults were "
           "written to %s -- hand-editable from here on\n",
           why, reader::kSettingsPath);
    } else {
      logf("[sd] %s: there is no settings file and %s could NOT be written (card "
           "full, write-protected, or failing). Running on defaults\n",
           why, reader::kSettingsPath);
    }
    logFlush();
  }

  if (gSd.useFileProbeTarget(reader::kSettingsPath)) {
    logf("[sd] %s: fast probe reads %s every %lu ms. Opening it walks the root "
         "directory, then /.reader, then a data sector -- three sectors against "
         "SdFat's one 512-byte cache, so it cannot be answered from RAM\n",
         why, gSd.probeTargetPath(), (unsigned long)kSdPollMs);
  } else {
    // The one state that must never be quiet. Reaching here means the settings
    // file is absent or unreadable after the attempt above, so probe() is back to
    // the root-directory read that could not see a pulled card at all.
    logf("[sd] %s: fast probe DEGRADED to a root-directory read -- %s is missing or "
         "would not open, so there is no file to read. That read can be served from "
         "SdFat's sector cache, which is exactly the defect this target exists to "
         "avoid; the %lu ms FAT-scan backstop is what will catch a pull now\n",
         why, reader::kSettingsPath, (unsigned long)kSdDeepPollMs);
  }
  logFlush();

  // ARM THE BACKSTOP ONLY IF THE FAST PROBE IS DEGRADED.
  //
  // Measured on the user's card: the FAT scan takes **14108 ms**. It was armed
  // unconditionally, which delayed the first paint by fourteen seconds and would
  // then have blocked the loop for fourteen seconds out of every twenty-five,
  // forever. That is not insurance, it is a device that appears to hang.
  //
  // It was added because the fast probe's guarantee was an ARGUMENT about
  // SdFat's cache geometry, and an argument is what was wrong the time before.
  // The device has since settled that argument: pulling the card produced
  // "Detected by the 2 s FAST PROBE (a byte read from a real file)" within two
  // seconds. So the fast probe works, and paying fourteen seconds to second-guess
  // it is strictly worse than not paying it.
  //
  // It stays for the one case where the fast probe genuinely cannot tell -- no
  // target file, so `probe()` falls back to the cache-served root read. There the
  // scan is the only check there is, and fourteen seconds every twenty-five is
  // better than never noticing. That path also announces itself loudly above.
  if (gSd.probeTarget() == SdFileSystem::ProbeTarget::File) {
    logf("[sd] %s: FAT-scan backstop NOT armed -- the fast probe reads a real file, "
         "which the device has confirmed detects a pull in ~2 s. The scan costs "
         "~14 s on this card and is only worth that when the fast probe is "
         "degraded\n",
         why);
    logFlush();
    return;
  }
  const uint32_t t0 = millis();
  const bool armed = gSd.armDeepProbe();
  const uint32_t scanMs = millis() - t0;
  if (armed) {
    logf("[sd] %s: FAT-scan backstop armed (%llu bytes used, scan took %lu ms) and "
         "re-runs every %lu ms -- it is the check that cannot be served from cache\n",
         why, (unsigned long long)gSd.deepProbeBaselineBytes(), (unsigned long)scanMs,
         (unsigned long)kSdDeepPollMs);
  } else {
    logf("[sd] %s: FAT-scan backstop NOT armed -- the scan reported 0 bytes used, "
         "which is also how it reports its own failure, so it could never tell a "
         "dead card from this volume. The fast probe is the only card check\n",
         why);
  }
  logFlush();
  // Both clocks restart here, so the first poll of each kind lands one full
  // interval after this -- the reads above have just answered both questions.
  gLastSdPollMs = millis();
  gLastSdDeepPollMs = gLastSdPollMs;
}

static reader::SleepViewModel sleepVmFromCard(std::string note);

// Declared here for setup()'s wake paint, which has to know whether the glass is
// holding a COVER before it may assert anything about the panel's baseline. Both
// definitions are far below, beside the sleep path they were written for.
static reader::CoverSource* sleepCoverForPaint();

// --- WHAT THE CARD'S POINTER SAYS ---------------------------------------------
//
// `/.reader/last.json` read, checked and shaped, once. Home's reading column and
// the Sleep screen's card are the same four facts about the same book, and they
// were reading and shaping them SEPARATELY -- the same load, the same `exists`
// check against the card, the same title fallback, written twice.
//
// They never disagreed about the DATA, and the device's 42%-asleep against
// 40%-at-home was not this: both take `last.percent`, and the difference was WHEN
// each read it (Home builds its view model once and holds a snapshot; Sleep builds
// its at the moment it paints). But a second copy is where a rule stops being one,
// and this file's own says the second copy is the extraction point rather than the
// fifth. The title fallback is the part most likely to have drifted: it is the same
// decision Book details makes about a book with no OPF title.
struct ReadingPointer {
  bool valid = false;      // there is a pointer AND the book it names is still there
  std::string bookPath;
  std::string title;       // the OPF's, or the filename
  std::string author;
  int percent = 0;
  int spine = 0;
  // The chapter's NAME, or empty when the pointer predates the key -- see
  // reader::LastRead::chapter. Home draws that line blank rather than substituting
  // the spine position it used to compose a false `CH. n OF N` from.
  std::string chapter;
};

static ReadingPointer readingPointer() {
  ReadingPointer p;
  reader::LastRead last;
  if (!gStorageUsable || !reader::loadLastRead(gSd, last)) return p;
  // CHECKED AGAINST THE CARD, not trusted. A book deleted on a computer, or a
  // different card in the slot, leaves a pointer naming something that is not
  // there -- and offering to continue a book that cannot be opened is worse than
  // not offering.
  if (!gSd.exists(last.bookPath)) {
    logf("[progress] the last book is gone from the card: %s\n", last.bookPath.c_str());
    return p;
  }
  p.valid = true;
  p.bookPath = last.bookPath;
  p.title = last.title.empty() ? last.bookPath : last.title;
  p.author = last.author;
  p.percent = last.percent;
  p.spine = last.spine;
  p.chapter = last.chapter;
  return p;
}

// --- The app, and the session record -------------------------------------

// HOME'S `LIBRARY` ROW SHOWS THE REAL COUNT. demoHomeVm() carries the board's
// `12`, which is right for the goldens and the design comparison and a lie on a
// device, so the shell patches that one field from the card.
//
// The number is BookList::countLibrary's -- the books in /books plus the books
// one level down -- which is the same rule the Library's own header band uses, so
// Home and the Library cannot disagree about how many books there are. -1 means
// the directory could not be read, and the row then shows NOTHING rather than a
// 0: "no books" and "could not look" are different claims, and the second one is
// not ours to make on the user's behalf.
//
// IT IS ONE DIRECTORY LISTING PLUS ONE PER FOLDER, AND IT IS CACHED, because the
// sentence above this one used to say "at boot and after a retry only. Not on a
// paint, and not on a timer" and that stopped being true the moment Home learned
// to rebuild itself: gHomeRebuild puts this on the critical path of a Back from a
// book, which is ~1.1 s of directory listing on a 203-book card -- for one
// integer -- with the user holding a button and nothing on the panel.
//
// THE CACHE IS SOUND BECAUSE OF WHAT V1 IS. The count can only change if a book
// arrives or leaves. Nothing can arrive: transfer is card-only, so putting a book
// on the card means the card is in a computer and this firmware is not running.
// So the only mutation is a delete, and every delete goes through
// SdFileSystem::remove -- see removals() there for why the counter lives on the
// filesystem rather than on the Library. gStorageUsable is the other half: a card
// that went away or came back invalidates the count for a different reason, and
// both are checked rather than either being assumed to imply the other.
//
// Wi-Fi transfer returns in V2 and books WILL be able to arrive while the device
// runs. That is the change that has to invalidate this, and it is a one-line
// invalidation next to whatever writes the file.
static int gLibraryCount = 0;
static bool gLibraryCountValid = false;
static uint32_t gLibraryCountAtRemovals = 0;
static bool gLibraryCountWhileUsable = false;

static int libraryCountForHome() {
  if (gLibraryCountValid && gLibraryCountAtRemovals == gSd.removals() &&
      gLibraryCountWhileUsable == gStorageUsable)
    return gLibraryCount;
  const uint32_t t0 = millis();
  gLibraryCount = gStorageUsable ? reader::BookList::countLibrary(gSd, reader::kBooksRoot) : -1;
  gLibraryCountValid = true;
  gLibraryCountAtRemovals = gSd.removals();
  gLibraryCountWhileUsable = gStorageUsable;
  // The cost, once, where it is paid. A second of listing that shows up on a
  // navigation the user thinks is instant is exactly the kind of thing that has
  // to be in the log rather than inferred from a device feeling slow.
  //
  // AND WHAT THE FOLDER MEMO DID, because a hit is otherwise INVISIBLE: a folder
  // answered from RAM produces no `[fs] list` line at all, so success and "the
  // count stopped being called" print identically. The counters are cumulative
  // over the session and this is the one line that prints them, so they also
  // cover the Library's own rescan -- a Library push between two of these lines
  // shows up as `hit=` having grown by one per folder, which is the whole point.
  // `held=` is how many folders are remembered RIGHT NOW, so it alone falls back
  // to zero when an invalidation fires, and it stopping short of the folders on
  // the card means the ceiling in dir_counts.h was reached.
  //
  // At boot the honest reading is `held=N hit=0 miss=N`: the first walk cannot be
  // avoided, and this line is where you see that it will not be paid again.
  const reader::DirCountCache& counts = gSd.bookCounts();
  logf("[library] counted %d book(s) in %s in %lums | folders held=%u hit=%u miss=%u\n",
       gLibraryCount, reader::kBooksRoot, (unsigned long)(millis() - t0),
       (unsigned)counts.held(), (unsigned)counts.hits(), (unsigned)counts.misses());
  logFlush();
  return gLibraryCount;
}

static reader::HomeViewModel homeVmForCard() {
  const int books = libraryCountForHome();

  // NO BOOKS ON THE CARD is its own state, not Home with a blank count: there is
  // nothing to continue, so the whole reading column goes and the board's
  // explanation takes its place. design/HomeEmpty.dc.html.
  //
  // EXACTLY zero, and only when the card was readable. `books < 0` means /books
  // could not be read at all -- a card that is present but unreadable, or absent
  // -- and telling that user "no books yet, copy some onto the card" would be
  // advice about a card the device cannot see. They keep the ordinary Home, whose
  // LIBRARY row shows a blank count, and the SD-missing screen handles the case
  // where the card really has gone.
  // THREE STATES, AND NONE OF THEM IS demoHomeVm's MIDDLEMARCH. This line used to
  // choose that for any card with books on it, so a device that had never opened a
  // book showed a stranger's novel at 6% -- fiction presented as the user's reading
  // position, the same defect class as the Reader factory falling through to demo
  // content on a session restore.
  reader::HomeViewModel vm =
      books == 0 ? reader::demoHomeEmptyVm() : reader::demoHomeUnopenedVm();
  if (books == 0) {
    logf("[boot] /books holds no readable book: Home shows the empty state\n");
    logFlush();
    return vm;
  }

  // WHAT A BOOK COSTS IN RAM -- OFF BY DEFAULT, because it is not free.
  //
  // It lists /books TWICE, and a listing costs 2.7 ms per ENTRY on this card. On a
  // 203-book library that is 406 entries -- macOS writes a `._name` beside every
  // file -- so the probe adds ~2.1 seconds to every boot, for a measurement that
  // has been taken and is recorded in the roadmap. It stays because those numbers
  // are how the 256-row cap was chosen, and the next change to BookEntry or
  // DirEntry will want them again.
  //
  //   PLATFORMIO_BUILD_FLAGS="-DENCRE_LIBRARY_PROBE=1" make firmware
  //
  // Same shape as the filesystem self-test, for the same reason: a diagnostic
  // that costs seconds of boot has to be opt-in, or it quietly becomes the
  // product's behaviour.
#if defined(ENCRE_LIBRARY_PROBE) && ENCRE_LIBRARY_PROBE
  // here whose size the user controls and the cap on it has to come from a number
  // rather than from sizeof-arithmetic. (Two memory questions have now been
  // guessed at wrongly in this project; both were settled by a boot line.)
  //
  // The two allocations are measured SEPARATELY rather than as one peak, because
  // getMinFreeHeap() is monotonic over the boot and the body probe has already
  // driven it below anything a library scan will reach -- so a peak measured that
  // way would read as zero and mean nothing.
  //
  //   raw  -- what FileSystem::list() retains: one DirEntry per directory entry,
  //           books and non-books alike, so it is charged on the whole folder.
  //   list -- what BookList::scan() retains: one BookEntry per ROW, names moved
  //           out of `raw` rather than copied, no title string built.
  //
  // Peak is the two together, which is what a rescan holds while it runs.
  if (gStorageUsable) {
    std::vector<reader::DirEntry> raw;
    const uint32_t h0 = ESP.getFreeHeap();
    const uint32_t t0 = micros();
    const bool rawOk = gSd.list(reader::kBooksRoot, raw);
    const uint32_t t1 = micros();
    const uint32_t rawHeld = h0 - ESP.getFreeHeap();
    const size_t rawN = raw.size();
    raw.clear();
    raw.shrink_to_fit();

    std::vector<reader::BookEntry> rows;
    const uint32_t h1 = ESP.getFreeHeap();
    const uint32_t t2 = micros();
    const bool listOk = reader::BookList::scan(gSd, reader::kBooksRoot, rows);
    const uint32_t t3 = micros();
    const uint32_t listHeld = h1 - ESP.getFreeHeap();
    const size_t rowN = rows.size();

    logf(
        "[library] %s entries=%u rows=%u | raw %lu B (%lu/entry, %luus) | "
        "list %lu B (%lu/row, %luus) | peak %lu B | sizeof DirEntry=%u "
        "BookEntry=%u\n",
        (rawOk && listOk) ? "ok" : "READ FAILED", (unsigned)rawN, (unsigned)rowN,
        (unsigned long)rawHeld, (unsigned long)(rawN ? rawHeld / rawN : 0),
        (unsigned long)(t1 - t0), (unsigned long)listHeld,
        (unsigned long)(rowN ? listHeld / rowN : 0), (unsigned long)(t3 - t2),
        (unsigned long)(rawHeld + listHeld), (unsigned)sizeof(reader::DirEntry),
        (unsigned)sizeof(reader::BookEntry));
    // Extrapolated, with both distortions named, because a number this drives a
    // decision from has to carry its own error bars.
    //
    //  * `peak` is an UPPER BOUND, not a measurement. It adds the two retained
    //    figures, and the names are MOVED from one to the other rather than
    //    copied -- so every byte of every book's name is counted twice in it.
    //  * `retained/row` is INFLATED whenever rows < entries, because scan()
    //    reserves for the whole listing (`out.reserve(raw.size())`) and a folder
    //    with non-book files pays for slots it never fills. Reserving is still
    //    right -- reallocating mid-scan is worse -- but it means a card whose
    //    /books holds covers or metadata files reads high here.
    //
    // Both distortions shrink toward nothing on a real library, where entries
    // and rows converge. THE FIX FOR A BAD FIGURE HERE IS MORE BOOKS ON THE
    // CARD, not more arithmetic: `make epubs-bulk N=200`.
    if (rowN) {
      const uint32_t perRow = listHeld / rowN;
      logf("[library] extrapolated retained: 256 rows = %lu KB, "
           "1024 rows = %lu KB, 4096 rows = %lu KB "
           "(inflated %ux by rows<entries)\n",
           (unsigned long)(perRow * 256u / 1024u),
           (unsigned long)(perRow * 1024u / 1024u),
           (unsigned long)(perRow * 4096u / 1024u),
           (unsigned)(rowN ? (rawN + rowN - 1) / rowN : 1));
    }
    logFlush();
  }
#endif  // ENCRE_LIBRARY_PROBE
  // demoHomeTargets() runs parallel to this menu and its first entry is the
  // Library, so row 0 is the row to patch. Guarded anyway: an empty menu here
  // would be a change in the shared catalogue, and indexing into it would be a
  // crash rather than a wrong label.
  // THE READING COLUMN, from the card's own pointer.
  //
  // Everything it draws comes out of /.reader/last.json, which the reader wrote on
  // the way out of the book -- so nothing here opens an EPUB. Doing it properly
  // would mean a central directory and an OPF parse at boot, ~100 ms and ~32 KB of
  // transient, for a block the user may not be looking at.
  //
  // THE POINTER IS CHECKED AGAINST THE CARD, not trusted. A book deleted on a
  // computer, or a different card in the slot, leaves a pointer naming something
  // that is not there -- and drawing it would be Home confidently offering to
  // continue a book that cannot be opened. `exists` is one cheap call and it is the
  // whole check. (design/HomeMissing.dc.html is the state that shows the last book
  // WITH a warning; it is boarded and not built, so for now a stale pointer falls
  // back to the nothing-open screen, which is honest if less informative.)
  if (books > 0) {
    const ReadingPointer p = readingPointer();
    if (p.valid) {
      vm = reader::demoHomeVm();     // the reading-column shape, then every field
      vm.nothingToContinue = false;  // ...replaced, because none of it is this book
      vm.title = p.title;
      vm.author = p.author;
      vm.percent = p.percent;
      // THE CHAPTER'S NAME, STRAIGHT OFF THE POINTER AND NOT COMPOSED HERE.
      //
      // This line built `CH. %02d OF %d` out of the spine position and the spine
      // count, and it was a FALSE CLAIM reported off an X3: a spine counts the cover,
      // the title page, the copyright, the contents, the part dividers, the notes and
      // the colophon alongside the chapters, so the two numbers invite an arithmetic
      // they do not support. `47% - CH. 14 OF 36` for a book of 7 chapters in 2 parts,
      // and Contents then put the reader at part 2 with five selectable rows left.
      //
      // AND NO BETTER TOTAL EXISTS. Measured over the 206 corpus books with a usable
      // NCX: 126 (61.2%) have a spine count above the spine entries their TOC names at
      // all, worst 164 against 26, and 183 (88.8%) have one differing from the count of
      // entries the TOC names as selectable chapters. Numbering the TOC's own entries
      // would be a third numbering system -- the mistake Contents' right-hand slot was
      // already fixed for -- so the NAME is the answer and there is no total.
      //
      // IT IS NOT DERIVED HERE, which is the load-bearing part: it is the Reader's own
      // header label, cached by saveReadingPosition, so Home, the Reader's band and
      // Contents' `NOW` row name the reader's chapter in the same words. A book with no
      // contents stores `CH. 08` -- a position with no total, the Reader's own fallback
      // -- so this is empty only for a pointer written before the key existed, and the
      // theme then draws the line blank. An absent claim beats a false one.
      //
      // ASSIGNED UNCONDITIONALLY, and a `if (!p.chapter.empty())` here would be the
      // substitution defect this file already records: `vm` is `demoHomeVm()` two
      // lines up, so a guarded assignment leaves the BOARD's `I - Miss Brooke` on the
      // glass of a device reading something else -- Middlemarch fiction, which is how
      // this device once woke into a book nobody was reading.
      vm.chapterLabel = p.chapter;
      vm.focusedMenuIndex = -1;  // the CONTINUE block, which exists again
      vm.hints = {"READ", "SELECT", "UP", "DOWN"};
      // The chapter is quoted so an EMPTY one is visible as empty in the log: this is
      // the one field the pointer can legitimately fail to carry, and a bare %s makes
      // "the pointer predates the key" indistinguishable from "the line was drawn".
      logf("[progress] Home continues \"%s\" at %d%%, spine %d, chapter \"%s\"\n",
           vm.title.c_str(), p.percent, p.spine + 1, p.chapter.c_str());
      logFlush();
    }
  }

  const bool patched = !vm.menu.empty() && books >= 0;
  if (!vm.menu.empty()) vm.menu[0].value = books >= 0 ? std::to_string(books) : std::string();
  // `patched`, not `books >= 0`: the guard above exists because an empty menu
  // would be a crash rather than a wrong label, and reading vm.menu[0] here on
  // the strength of `books` alone undid it two lines later. Unreachable today
  // (demoHomeVm always fills two rows) and exactly the kind of latent hole a
  // shared catalogue change opens.
  logf("[boot] Home's LIBRARY row: %s (%s)\n",
       patched ? vm.menu[0].value.c_str() : "blank",
       books >= 0 ? "books in /books plus one level down"
                           : "/books could not be read, so no count is claimed");
  logFlush();
  return vm;
}

// Re-teach the recognizer what the TOP SCREEN binds. The masks are
// PressRecognizer's, not the App's, so anything that changes what is on top --
// a dispatch that pushed or popped, a replaced App -- has to re-sync them or
// the recognizer stays bound to the previous screen's holds. One function, so
// forgetting HALF of the pair (a mask synced, a repeat left stale) is not
// writable.
static void syncRecognizer() {
  gPresses.setLongPressable(gApp->longPressable());
  gPresses.setAutoRepeat(gApp->autoRepeat());
}

// REPLACE THE APP with one rooted at `root`. App has no "replace the root", and
// the reasons a fresh App is right are the callers' (see buildHomeApp and
// buildSdMissingApp); what this function owns is the ORDER:
//
//   1. the new App, which starts dirty and in transition, so the swap paints
//      itself as the screen change it is. Assigning it destroys the old App and
//      with it whatever Library was on its stack -- which is what nulls the
//      factory's pointer, from the screen's own destructor. This used to be a
//      forgetLibrary() call HERE, ordered before the swap, and that only covered
//      the App-replacing sites: a Library popped off a live App left the pointer
//      dangling, because a pop is not a swap. See DemoScreenFactory::library().
//   2. the recognizer re-sync, because the top screen just changed.
static void replaceApp(std::unique_ptr<reader::Screen> root) {
  gApp = std::make_unique<reader::App>(std::move(root), gFactory);
  syncRecognizer();
}

// Build the app with Home as its root, replacing whatever was there.
//
// A successful retry cannot PUSH Home: the SD-missing screen is the root in
// that state, so Home would be at depth 2 and Back would pop to a screen whose
// message is no longer true. Replacing the App is the straightforward answer.
static void buildHomeApp() {
  replaceApp(std::make_unique<reader::HomeScreen>(homeVmForCard(), reader::demoHomeTargets()));
  // AND THE ONE FUNCTION THAT BUILDS HOME IS WHERE THE GATE IS ANSWERED, because a
  // caller that had to remember this is exactly what #43 was. It clears the latch and
  // re-stamps the removal counter together: without the re-stamp, the rebuild a delete
  // asks for asks again on every loop iteration for the rest of the session, and each
  // one replaces the App under the user. Taken AFTER homeVmForCard(), so the count the
  // view model holds and the counter this records are the same card state.
  gHomeRebuild.noteBuilt(gSd.removals());
}

// Store where the user is, so a wake can put them back. Cheap to call after every
// dispatch: saveSession() skips an identical rewrite, so navigating back and
// forth does not grind the NVS partition.
//
// THE WHOLE STACK, and there is nothing screen-specific left in here to get
// wrong. App::snapshot() asks every screen on the stack where its focus is
// through one virtual, so a screen added in Phase 3 is stored the day it exists
// and needs no line here, in session.cpp or in the restore. That is the third
// attempt at this: 2C-1 stored a hardcoded 0, 2C-2 stored the top screen's focus,
// and both left screens that reported a focus nobody read back -- because each
// version knew the names of the screens it handled.
//
// IT SAYS WHAT IT DID, and that is not decoration. The wake path and this are the
// two halves of one mechanism, and a wake that comes back to Home is ambiguous
// between them: either nothing was ever stored, or a record was stored and the
// restore would not honour it. So this logs a CHANGED record when it goes in,
// once per change to the encoded stack -- which includes a focus move, so it is a
// line per navigation on a list. That is a real cost in log volume and the right
// trade: what the line reports is load-bearing, and a stored value nobody could
// see going in is how the focus field stayed decorative for two phases.
//
// It also logs distinctly when the store refuses. saveSession() prints the
// NVS-level reason (namespace would not open, or a put came back short); the line
// here is the consequence, which is the part a reader of the log actually cares
// about.
//
// `gLastLogged` tracks what was last ANNOUNCED rather than what is in NVS.
// saveSession() has its own skip-an-identical-rewrite cache and returns true
// without touching flash, so asking it "did you write?" is not possible from
// here; mirroring the comparison is. The two can only disagree by this printing
// one extra line after a failure, which is the harmless direction.
//
// COMPARED AS A SNAPSHOT, NOT AS THE ENCODED STRING, which is the difference
// between building one string per keypress and building none. This runs after
// every dispatch and the overwhelmingly common outcome is "nothing changed"; the
// wire form is only wanted for a log line, so it is built only when there is a
// line to print.
static bool gLogged = false;
static std::vector<reader::StackEntry> gLastLogged;

static void saveWhereWeAre() {
  const std::vector<reader::StackEntry> stack = gApp->snapshot();
  const bool changed = !gLogged || gLastLogged != stack;
  if (!saveSession(stack)) {
    // The other half of defect "wake came back to Home": a save that fails here
    // leaves a record that either does not exist or describes an older stack, and
    // the wake then looks like the restore failed when it was the write.
    logf("[session] NOT stored: %s will not be restored by the next wake\n",
         reader::encodeSessionStack(stack).c_str());
    logFlush();
    gLogged = false;
    return;
  }
  if (changed) {
    logf("[session] stored %s; a wake will come back here\n",
         reader::encodeSessionStack(stack).c_str());
    logFlush();
  }
  gLastLogged = stack;
  gLogged = true;
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
  // The card going away at runtime is the one swap that can happen with a
  // Library on the stack -- so the pointer the factory keeps to that Library has
  // to go with it, which the Library's own destructor does.
  replaceApp(std::make_unique<reader::SdMissingScreen>());
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
// The Open latch, answered exactly as handleRetry answers Retry: the card is the
// shell's, so the screen asks and this does the work.
//
// EVERY FAILURE IS LOGGED AND LEAVES THE LIBRARY STANDING. A book that will not
// open is a book on somebody's card, and the only honest outcomes are "the Reader
// appears" or "the log says why". Repainting the Library would cost a full refresh
// to show an unchanged screen; a Push of an error screen is design/BookError.dc.html
// and is not built.
// THE READER ANYWHERE ON THE STACK, or null.
//
// Scanned rather than tracked, for the reason the book-closed check is scanned: a
// remembered depth would be a second copy of the stack's own shape, and the stack is
// three deep at most here. Three callers now -- the book-closed check, and both halves
// of the Typography apply path -- which is why it is a function and not a third inline
// walk.
//
// MUTABLE, because two of the three have to MOVE the screen they find: giving its page
// ring back on the way into the panel, and re-paginating it on the way out. App::at is
// const for the renderer's sake, so App::atMut exists for exactly this; a const_cast
// here would do the same thing and say nothing about why it is allowed.
static reader::ReaderScreen* readerOnStack(reader::App& app) {
  for (int i = 0; i < app.depth(); ++i)
    if (app.at(i).id() == reader::ScreenId::Reader)
      return static_cast<reader::ReaderScreen*>(&app.atMut(i));
  return nullptr;
}

// IS `id` ANYWHERE ON THE STACK. readerOnStack's question without the pointer, for a
// caller that wants the fact rather than the screen.
//
// SCANNED, NOT TRACKED, which is readerOnStack's own reasoning: a remembered bool is a
// second copy of the stack's own shape, and the two copies are free to disagree on
// exactly the paths nobody walked. The stack is four deep at most here.
static bool appHasScreen(const reader::App& app, reader::ScreenId id) {
  for (int i = 0; i < app.depth(); ++i)
    if (app.at(i).id() == id) return true;
  return false;
}

// SAVE WHERE THE READER IS, to the card, if a book is open.
//
// FOUR CALLERS, AND THE FOURTH IS THE ONE THAT MAKES THIS DURABLE. Three are edges
// that change the answer -- leaving the book, crossing into another chapter, going to
// sleep -- and they fire unconditionally because each is a moment the reader would
// notice losing. The fourth is loop()'s quiet window, which offers the position after
// kSaveQuietMs of silence and gets an answer from ProgressSaveGate first.
//
// THIS USED TO BE THREE EDGES ONLY, on the grounds that "a turn is ~570 ms of panel
// and a card write on top of each one would be felt". That reasoning was right about
// the cost and wrong about where to put the work: it bounded a power cut's damage at
// ONE CHAPTER, which on a real novel is an hour of reading. The write is not made
// cheaper, it is made to happen when the loop is already idle -- the same answer the
// page count, the refinement and the ring warm all reached, and the same one the card
// log reached before them.
//
// IT IS NOT HIDDEN UNDER THE WAVEFORM, which was the idea this replaced. The
// triggerDisplay/completeDisplay seam really is open for ~389 ms of otherwise idle
// CPU, but the SD card is on the DISPLAY'S SPI bus and the SDK states the contract in
// three separate places -- PanelDriver.h's "the caller does non-SPI CPU work in the gap
// and issues no other bus op until displayFinish()" is the sharpest. Uc8279Driver
// leaves a PARTIAL_IN window open across that gap for displayFinish to close, so the
// controller is mid-sequence the whole time. The quiet window costs the reader the
// same nothing and breaks no contract.
//
// A FAILURE HERE IS LOGGED AND NOTHING ELSE, which is the one hazard in this whole
// feature. A card can be readable and refuse writes -- a physical write-protect tab
// does exactly that -- and writeAll calls noteCardGone() when a write it had already
// opened goes wrong, which pollCardPresence turns into an App rooted at
// SdMissingScreen. So treating a failed save as an error to act on would throw the
// reader out of a book they can still perfectly well read. There is nothing to do
// about it and nothing worth telling the user, so it goes in the log and the reader
// keeps reading.
// RETURNS THE COMBINED OUTCOME OF BOTH RECORDS, which only the quiet-window caller
// reads -- the three edges fire regardless and have nothing to decide. Failed if
// either half was refused, Written if either half moved, Unchanged when the card
// already held both. The "nothing to save" early returns answer Unchanged, and the
// quiet-window caller additionally guards on the same conditions so it can never
// record a point that was not actually stored.
// `from` NAMES THE READER WHEN IT IS NOT ON TOP, and null means "the top, if it is
// one" -- which is every caller that predates the Typography panel. The four edges all
// fire with the Reader on top; the apply path fires after a pop that lands on the
// reader MENU, an overlay, so its Reader is one below and the top-only lookup would
// answer Unchanged and silently store nothing. Passed in rather than broadening the
// lookup, because broadening it would change what the existing four do on screens
// nobody has looked at.
static reader::SaveResult saveReadingPosition(const char* why,
                                              const reader::ReaderScreen* from = nullptr) {
  if (!gReading.open || gApp == nullptr) return reader::SaveResult::Unchanged;
  if (from == nullptr && gApp->top().id() != reader::ScreenId::Reader)
    return reader::SaveResult::Unchanged;
  const auto* rd =
      from != nullptr ? from : static_cast<const reader::ReaderScreen*>(&gApp->top());

  // BUILT FRESH, AND THAT IS WHAT DROPS `finished` -- deliberately, not by oversight.
  // READING THE BOOK AGAIN IS WHAT UN-MARKS IT: there is no board for a toggle, so the
  // alternative is finished-forever, and a flag with no way back is worse than the cost
  // of clearing it. The cost is real and bounded -- reopening a finished book and
  // leaving it also clears the flag -- and it is recoverable in two presses from the
  // item-actions overlay, and VISIBLE, because the Library row changes back.
  //
  // This looks like a bug from here, which is why it is written down here: a reviewer
  // reading only this function would carry `finished` forward and silently make the
  // flag permanent.
  //
  // THE FINISH FLOW ITSELF IS NOT AT RISK, for two independent reasons. handleFinish
  // leaves the book through dispatchBack(), which calls App::dispatch directly and so
  // never reaches loop()'s pre-dispatch `leaving` save; and every later save is gated
  // on the Reader being on top (just above), which it no longer is.
  reader::ReadingPosition p;
  p.bookPath = gReading.path;
  p.spine = rd->chapterIndex();
  const reader::Cursor at = rd->currentCursor();
  p.block = at.block;
  p.line = at.line;
  p.bookBytes = gReading.bytes;
  // THE GEOMETRY THE LINE WAS MEASURED AT, which is what makes `line` reusable or
  // not. Read from the live metrics rather than assumed, so a type-size setting
  // invalidates exactly the field it should.
  //
  // THE FACE'S OWN ppem, NOT reader::kBodyPpem, and that constant is what this line
  // used to say -- correct for as long as nothing could change the size, and a lie
  // from the moment the Typography panel could. A record claiming 32 for lines
  // measured at 46 grades `Exact` and hands the reader a line index from a layout
  // that never existed, which is the one thing fitOf is there to prevent.
  p.ppem = gBody.ppem();
  p.columnW = gFactory.readerMetrics().columnW;
  // The percentage goes IN the sidecar, so the Library can show it per row without
  // opening every book's archive to recompute one. Computed once, just below, and
  // shared with the Home pointer.
  p.percent = reader::progressPercent(gFactory.readerBook(), rd->chapterIndex(), rd->vm().page,
                                      rd->vm().pageTotal, rd->chapterBytesRead());
  // THE CHAPTER'S NAME AS THE READER SEES IT, which is the header's own label -- so a
  // book with no contents stores the `CH. 08` fallback and Book details' "Current story"
  // says that, rather than inventing a name or leaving the row blank.
  p.chapter = rd->vm().chapter;
  // THE WAY BACK, riding this record's edges and adding none of its own. Losing an
  // anchor to a power cut costs a shortcut and nothing else -- the reader is still
  // sitting on a real page -- so it does not justify a write on an edge that does not
  // already take one.
  //
  // WRITTEN ONLY WHILE THE MARK IS AHEAD, not merely while one is stored. Under the
  // high-water rule the mark is raised to wherever the reader stands, so `isSet()` is
  // true almost always and would put three keys in every record to say "the way back
  // is the page you are on". Gated this way, a reader at their furthest point writes a
  // record byte-identical to one from before anchors existed -- which is the property
  // reading_position.h's absent-rather-than--1 rule is there to give.
  if (rd->anchor().aheadOf(rd->here())) {
    const reader::AnchorPos a = rd->anchor().get();
    p.anchorSpine = a.spine;
    p.anchorBlock = a.block;
    p.anchorLine = a.line;
  }

  reader::LastRead last;
  last.bookPath = gReading.path;
  last.title = gReading.title;
  last.author = gReading.author;
  last.spine = rd->chapterIndex();
  // THE SAME STRING THE SIDECAR TAKES, from the same expression one field up, so Home
  // and the Reader's header band cannot disagree about which chapter this is. It is
  // cached here rather than read back out of the sidecar because Home's reading column
  // is built at boot and on every rebuild, and a second small-file read there would be
  // on the critical path of a Back out of a book -- the path the listing cache and the
  // folder-count memo were both written to keep clear.
  //
  // `spineCount` WENT WITH THE LABEL IT EXISTED FOR. It was written here, read in
  // readingPointer() and spent composing `CH. n OF N`; with that gone it had no reader
  // anywhere, which is the shape this project has twice shipped as a field outliving
  // its producer. Dropping the key rewrites every card's pointer once and nothing
  // reads it on the way in, so an older pointer still loads.
  last.chapter = p.chapter;
  // By BYTES through the book, because a page-based percentage would need every
  // chapter counted -- ~49 s of decode on this device. See progressPercent.
  last.percent = p.percent;

  const reader::SaveResult a = reader::savePosition(gSd, p);
  const reader::SaveResult b = reader::saveLastRead(gSd, last);
  // NOT NAMED `word`: Arduino.h defines word(...) as a macro over makeWord, so a
  // lambda by that name compiles on the desktop and fails only in the firmware.
  const auto outcome = [](reader::SaveResult r) {
    return r == reader::SaveResult::Written ? "written"
           : r == reader::SaveResult::Unchanged ? "unchanged" : "FAILED";
  };
  // Logged at every outcome including `unchanged`, because "the save did nothing"
  // and "the save did not happen" look identical on a device and are not the same.
  //
  // BUT `unchanged` DOES NOT LATCH HOME, and it did until a device run caught it. This
  // block latched unconditionally, on the argument that "the pointer in hand is newer
  // than the one Home was built from either way" -- which is true of `written` and of
  // `FAILED`, and FALSE of `unchanged`: that answer means the card ALREADY held this
  // record, so whichever earlier save actually wrote it has already latched, and Home
  // was either rebuilt from it or is still latched from then. Nothing is newer.
  //
  // Two things it cost, both observed on an X3 (2026-09-07, #43's own validation run):
  //   * A FALSE LOG LINE. Leaving a book without moving in it latched Home, so the next
  //     rebuild -- whatever really caused it -- reported `the reading position has
  //     moved`. In that run the real cause was a DELETE, and the line named the wrong
  //     one of the two causes the strings exist to tell apart.
  //   * A NEEDLESS REBUILD. `stale()` is `latched_ || the counter moved`, so a Back out
  //     of a book the reader only looked at put a /books listing (~96 ms on a 14-entry
  //     card) plus a repaint on the way to Home, for a block whose content is identical.
  //     That is exactly the cost this gate exists to avoid.
  //
  // LATCHED rather than derived, because a save REWRITES the sidecar and leaves no
  // counter behind for the gate to notice -- see reader/home_rebuild.h.
  const bool wrote = a != reader::SaveResult::Unchanged || b != reader::SaveResult::Unchanged;
  if (wrote) gHomeRebuild.markStale();
  // ...and so does the Library's row for this book, on the same terms and for the same
  // reason: `written` and `FAILED` both mean the percentage in hand is newer than the
  // one those rows were built from, and `unchanged` means it is not. Gated by the SAME
  // expression rather than by a second copy of the test -- the observed cost here was a
  // `Library rows re-read: ok in 160ms` on every Back out of an unmoved book.
  if (wrote) gLibraryStale = true;
  logf("[progress] %s: spine=%d block=%d line=%d %d%% -- position %s, pointer %s\n", why,
       p.spine, p.block, p.line, last.percent, outcome(a), outcome(b));
  logFlush();

  // TWO RECORDS, ONE ANSWER. The gate's question is "is the card worth touching
  // again", and it is not settled until BOTH halves are down -- so a half-landed save
  // reports Failed and will be retried, rather than being recorded as stored because
  // the sidecar happened to succeed before the pointer did not.
  if (a == reader::SaveResult::Failed || b == reader::SaveResult::Failed)
    return reader::SaveResult::Failed;
  if (a == reader::SaveResult::Written || b == reader::SaveResult::Written)
    return reader::SaveResult::Written;
  return reader::SaveResult::Unchanged;
}

// WHAT OPENING THIS CHAPTER COST, AND WHICH BRANCH TOOK IT. A chapter under
// kEagerCountBytes is counted before its first paint and a larger one is not, and
// only the deferred side had a log line -- so a report of "no dash, and the page is
// slow again" could not be told from "the count ran and was cheap". `indexPending()`
// IS the branch: false means the pages are already known.
static void logChapterOpen(const reader::ReaderScreen* rd, uint32_t elapsedMs) {
  logf("[chapter] spine=%d bytes=%u %s pages=%d in %lums\n", rd->chapterIndex(),
       (unsigned)rd->chapterBytes(), rd->indexPending() ? "deferred" : "counted",
       rd->pageCount(), (unsigned long)elapsedMs);
}

// Defined below, because it is long and handleOpen reads better as the resolution of
// WHICH book followed by one call. Declared here rather than reordered so the two
// stay adjacent.
// --- TELLING THE USER SOMETHING IS STILL HAPPENING ---------------------------
//
// design/LibraryOpening.dc.html. One tracked line where the hint bar was, drawn
// OVER the frame already on the panel -- so no screen carries a flag for it, and
// adding a slow operation later needs no screen work at all. See drawStatusBar for
// why it replaces the hint bar rather than sitting somewhere of its own.
//
// IT COSTS A WHOLE WAVEFORM, ~439 ms, and there is no cheaper way: a windowed
// update would still drive every gate line, because the rotation is CCW and a
// portrait row band is a landscape column band. So this is bought, not free -- the
// content it is reporting on arrives 439 ms later than it would in silence. That is
// the trade this file already states as its own rule: on e-ink, feedback and speed
// are separate problems.
//
// THE DEADLINE IS WHY IT IS WORTH IT. Below it nothing is drawn and nothing is
// spent; a book that opens quickly never pays. It is only the operations that were
// already going to feel broken that buy the extra refresh.
//
// 500 ms, DOWN FROM 1000 after using it on the device. What that buys is feedback
// half a second sooner on the one operation that arms this -- opening a book, which
// measured 1000-2300 ms depending on how deep the saved position was.
//
// WHAT IT COSTS is the band between the two: an open that would have finished in
// 600 ms now shows the bar and pays a whole waveform for it, so it takes ~1040 ms
// instead. That is the trade being made deliberately -- on this glass feedback and
// speed are separate problems, and an open in that band is one where the device
// looked frozen for long enough to notice.
constexpr uint32_t kStatusAfterMs = 500;

static uint32_t gSlowOpStartedMs = 0;
static const char* gSlowOpLabel = nullptr;
static bool gSlowOpShown = false;

// Defined further down, beside showOnePass -- it needs the panel and this does not.
static void paintStatusBar(const char* label);

// Installed as reader::Progress's handler for the duration of a slow operation. It
// is called from inside the reader's decode walks, per block, so it must stay a
// comparison in the common case -- the paint happens once and then never again for
// this operation.
static void slowOpTick(void*) {
  if (gSlowOpShown || gSlowOpLabel == nullptr) return;
  if (static_cast<uint32_t>(millis() - gSlowOpStartedMs) < kStatusAfterMs) return;
  gSlowOpShown = true;
  paintStatusBar(gSlowOpLabel);
}

// RAII, because every path out of an open -- including the refusals, of which
// openBookAt has several -- has to take the hook back down. A handler left
// installed would fire inside the next chapter turn, which is not a slow operation
// and must never grow a status bar.
struct SlowOperation {
  explicit SlowOperation(const char* label) {
    gSlowOpStartedMs = millis();
    gSlowOpLabel = label;
    gSlowOpShown = false;
    reader::Progress::install(slowOpTick, nullptr);
  }
  ~SlowOperation() {
    reader::Progress::install(nullptr, nullptr);
    gSlowOpLabel = nullptr;
  }
  SlowOperation(const SlowOperation&) = delete;
  SlowOperation& operator=(const SlowOperation&) = delete;
  // True when the bar went up, so the caller knows the panel no longer holds what
  // it thinks and a transition is owed.
  bool shown() const { return gSlowOpShown; }
};

static bool openBookAt(const std::string& path, uint32_t bookBytes, bool push);

static void handleOpen() {
  gApp->clearOpenRequest();  // first, so a book that refuses does not re-fire

  // TWO SCREENS CAN ASK TO OPEN A BOOK, and they mean different books. The Library
  // means the row it has selected; Home's CONTINUE means the one the card's pointer
  // names. Action::Kind::Open carries no path -- deliberately, since core/ does no
  // storage -- so resolving it is this function's job.
  std::string path;
  uint32_t bookBytes = 0;
  if (gApp->top().id() == reader::ScreenId::Home) {
    reader::LastRead last;
    if (!reader::loadLastRead(gSd, last)) {
      logf("[open] CONTINUE with no saved book\n");
      return;
    }
    // Checked again here, not just when Home was built: the card can have changed in
    // between, and openBook would fail less clearly.
    if (!gSd.exists(last.bookPath)) {
      logf("[open] CONTINUE names a book that is gone: %s\n", last.bookPath.c_str());
      return;
    }
    path = last.bookPath;
  } else {
    reader::LibraryScreen* lib = gFactory.library();
    if (lib == nullptr) {
      logf("[open] no Library to ask\n");
      return;
    }
    const reader::LibraryItem* item = lib->focusedItem();
    if (item == nullptr || item->entry.isDir) {
      logf("[open] nothing selected, or a folder\n");
      return;
    }
    // BookEntry::name is a leaf name and never a path (booklist.h), so the path is
    // the Library's current directory joined with it -- which is also why this
    // cannot live in core/: only the Library knows where it has descended to.
    path = lib->path();
    if (path.empty() || path.back() != '/') path += '/';
    path += item->entry.name;
    bookBytes = item->entry.size;
  }
  // THE FILE'S SIZE IS THE STALENESS CHECK for a saved position, so it has to be
  // known on both paths. The Library already listed it; the CONTINUE path opens the
  // handle for it, which is one ~100-byte allocation dropped immediately -- cheaper
  // than a directory listing, and openBook is about to open the file anyway.
  if (bookBytes == 0) {
    std::unique_ptr<reader::FileHandle> h = gSd.openRead(path);
    if (h != nullptr) bookBytes = h->size();
  }

  openBookAt(path, bookBytes, /*push=*/true);
}

// LEAVE THE TOP SCREEN FROM OUTSIDE A GESTURE, by handing it the press it would have
// taken.
//
// THE SHELL CANNOT APPLY AN Action AT ALL, which is worth stating because it looks as
// though it should be able to. `Action::pop()` and `Action::popTo()` are values a
// SCREEN returns; `App::dispatch` is the only thing that interprets one, and the only
// stack call App exposes is `pushScreen()` -- there is no popScreen() and no
// apply(Action). So a latch handler that has to leave a screen either grows a second
// interpretation of the stack in the shell, or synthesises the press. This is the
// second, and it is the one that cannot drift: whatever Back means on that screen is
// what runs, decided by the screen, once.
//
// Short on Back, because gestureFor turns exactly that into Gesture::Back -- a Long is
// dropped there unless the screen bound a hold, which is not the press being imitated.
static void dispatchBack() {
  reader::InputEvent ev{};
  ev.button = reader::Button::Back;
  ev.kind = reader::PressKind::Short;
  ev.steps = 1;
  // The press is happening now. `at` is what the interaction line's `wait=` is measured
  // from, and a zero here would report this as having waited since boot.
  ev.at = millis();
  gApp->dispatch(ev);
}

// THE USER ASKED FOR A BOOK TO BE MARKED FINISHED -- from BookEnd's MARK AS FINISHED
// slab, or from the item-actions overlay's row. See Action::finish().
//
// WHICH BOOK IS RESOLVED HERE rather than carried in the Action, which is exactly the
// shape handleOpen has for the identical two-caller problem: BookEnd means the book
// that is open, the overlay means the Library's selected row. An Action that carried a
// path would put a std::string in every Action returned by every gesture on every
// screen to serve one kind.
static void handleFinish() {
  // The latch first, so a write that fails does not re-fire on every loop.
  gApp->clearFinishRequest();

  const reader::ScreenId asked = gApp->top().id();
  const bool fromBookEnd = asked == reader::ScreenId::BookEnd;

  std::string path;
  uint32_t bookBytes = 0;
  if (fromBookEnd) {
    if (!gReading.open) {
      logf("[finish] BookEnd with no open book\n");
      logFlush();
      return;
    }
    path = gReading.path;
    bookBytes = gReading.bytes;
  } else {
    reader::LibraryScreen* lib = gFactory.library();
    if (lib == nullptr) {
      logf("[finish] no Library to ask\n");
      logFlush();
      return;
    }
    const reader::LibraryItem* item = lib->focusedItem();
    if (item == nullptr || item->entry.isDir) {
      logf("[finish] nothing selected, or a folder\n");
      logFlush();
      return;
    }
    // handleOpen's own three lines, and for its reason: BookEntry::name is a leaf name
    // and never a path (booklist.h), so only the Library knows where it has descended
    // to. A second spelling of this join is a second place to get a subfolder wrong.
    path = lib->path();
    if (path.empty() || path.back() != '/') path += '/';
    path += item->entry.name;
    bookBytes = item->entry.size;
  }

  // KEEP SD TRAFFIC OFF THE DISPLAY BUS. The card shares the panel's SPI and
  // SDCardManager does no locking at all, so a transfer racing a refresh is the kind of
  // fault that looks random. Every SdFileSystem method takes the guard itself and it is
  // recursive; taking it around the whole sequence is what the retry and the poll do.
  SpiBusGuard bus;

  // A BOOK NEVER OPENED HAS NO SIDECAR, and marking one finished from the Library is a
  // legitimate thing for a reader to assert about a book they read elsewhere -- so a
  // minimal record is BUILT rather than the press refused. `bookBytes` is what makes it
  // a record about THIS book: ReadingPosition::bookBytes is the staleness check, and a
  // record with a zero there would read back as a book that had changed.
  reader::ReadingPosition pos;
  if (!reader::loadPosition(gSd, path, pos)) {
    pos = reader::ReadingPosition{};
    pos.bookPath = path;
    pos.bookBytes = bookBytes;
  }
  pos.finished = true;

  const reader::SaveResult r = reader::savePosition(gSd, pos);
  // NOT FATAL, and this is the one hazard in the feature. writeAll calls noteCardGone()
  // on a write that fails after opening, which pollCardPresence turns into an App
  // rooted at SdMissingScreen -- so acting on this would throw a reader out of a book
  // they can still perfectly well read, over a flag. reading_store.h states the same
  // hazard for savePosition and it applies unchanged.
  logf("[finish] %s from %s -> %s\n", path.c_str(), reader::screenName(asked),
       r == reader::SaveResult::Failed
           ? "FAILED"
           : (r == reader::SaveResult::Unchanged ? "unchanged" : "ok"));

  // ...AND ONLY IF IT NAMES THIS BOOK. Clearing it unconditionally would take an
  // unrelated book off Home's CONTINUE block -- another book's state destroyed by this
  // one's button -- and the overlay can mark any row finished, including one that is
  // not the book the pointer names.
  //
  // gReading.open IS DELIBERATELY NOT TOUCHED HERE. The book-closed scan in loop() is
  // the one place that closes a book, and it does two more things this would have to
  // copy -- the crossing detector's gLastChapter and the two idle-walk stuck trackers,
  // each of which outlives one book and suppresses real work on the next. Clearing the
  // flag here would make that scan's condition false forever and leak all three.
  reader::LastRead last;
  if (reader::loadLastRead(gSd, last) && last.bookPath == path) {
    const bool forgot = reader::forgetLastRead(gSd);
    logf("[finish] the card's pointer named this book: %s\n",
         forgot ? "cleared" : "NOT CLEARED");
  }
  logFlush();

  // BOTH, AND SEPARATELY. Each is consumed when its own screen is reachable, and one
  // shared flag would let Library, Back, Home clear it before Home ever used it. Both
  // blocks that consume these are BELOW this call in loop(), which is what puts the
  // refreshed row on the very paint this press causes.
  //
  // Home's is still LATCHED here even though forgetLastRead() above went through
  // FileSystem::remove and so moved the counter the gate also watches: what changed is
  // the reading position, the removal was incidental, and a fact that has a signal of
  // its own should use it rather than rely on a second one that happens to fire too.
  gHomeRebuild.markStale();
  gLibraryStale = true;

  // AND NOW LEAVE. Neither producer pops itself: BookEnd's slab and the overlay's row
  // both answer a bare Action::finish(), because what the write should cost on glass is
  // the shell's to decide.
  if (fromBookEnd) {
    // BookEnd's own Back returns to the last page and the Reader's returns to whatever
    // pushed it, so the two together ARE Action::popTo(Library) for every stack that
    // can reach here -- the Library when one is beneath, and Home when the reader
    // arrived through CONTINUE, which is popTo's own "stop at the root" rule.
    dispatchBack();
    if (gApp->top().id() == reader::ScreenId::Reader) dispatchBack();
  } else if (asked == reader::ScreenId::ItemActions) {
    // DISMISSED IN PLACE, because the board gives that row no chevron -- so the Library
    // underneath is repainted with the row reading its new percentage.
    dispatchBack();
  }
}

// ------------------------------------------------------------------ Wi-Fi
//
// PRIMES THE FACTORY FROM NVS. Called at boot and again after every change to
// the list, because the factory holds a COPY -- the same trap setSettings has,
// where closing a screen and reopening it shows the values from before the
// change.
static void primeWifi() {
  gFactory.setWifiNetworks(gWifiNets);
  gFactory.setWifiSink(&gWifiSink);
}

// Boot. The list, then the one rule that needs both namespaces at once.
static void loadWifi() {
  shellwifi::load(gWifiNets);
  // A LOCKED NETWORK WHOSE PASSPHRASE IS GONE CAN ONLY FAIL, and the two
  // halves live in different NVS namespaces so only a read can see both. This
  // is the only caller of dropLockedWithoutSecret and the reason SecretProbe
  // is an interface rather than a flag on the record.
  const shellwifi::NvsSecretProbe probe;
  const int dropped = gWifiNets.dropLockedWithoutSecret(probe);
  if (dropped > 0) {
    logf("[wifi] dropped %d saved network(s) whose passphrase is missing; a row that can "
         "only fail is worse than no row\n",
         dropped);
    shellwifi::save(gWifiNets);
  }
  // AN EMPTY SCAN, PRIMED UP FRONT, and this is load-bearing rather than
  // tidiness: WifiSettings' SETUP row returns Action::push(WifiPicker)
  // DIRECTLY, so the factory has to be able to build the picker before the
  // press happens. Unprimed, that push is refused and SETUP is the dead
  // button this whole function exists to remove -- one screen deeper.
  //
  // An empty list is a boarded state (WifiPickerEmpty), so what the reader
  // sees for the moment before the scan starts is a screen the design has,
  // not a hole.
  gFactory.setWifiScan({});
  primeWifi();
  logf("[wifi] %d saved network(s)\n", gWifiNets.size());
  logFlush();
}

// Takes the radio down and forgets the attempt. Called on every way out of
// the flow, because Wi-Fi stays off except while it is being used -- forced
// by heap rather than chosen: ~23 KB static against a measured 13,696-byte
// floor with a book open.
static void endWifiSession() {
  gRadio.down();
  gJoinSsid.clear();
  gJoinPsk.clear();
  gJoinLocked = false;
  gScanArmed = false;
}

// Starts the join the flow has assembled, and puts the CONNECTING... dialog
// where the asking screen was. REPLACE rather than PUSH, because the keyboard
// and the error panel must not be left standing under it -- Action::replace's
// own reason, and what makes one veiled parent truthful for both entry paths.
static void beginJoinFlow(bool replace) {
  gFactory.setWifiTarget(gJoinSsid);
  if (!gRadio.beginJoin(gJoinSsid, gJoinPsk)) {
    gFactory.setWifiFailure(reader::JoinFailure::Incomplete);
    if (replace) gApp->replaceScreen(reader::ScreenId::WifiError);
    else gApp->pushScreen(reader::ScreenId::WifiError);
    return;
  }
  if (replace) gApp->replaceScreen(reader::ScreenId::WifiConnect);
  else gApp->pushScreen(reader::ScreenId::WifiConnect);
}

// A CONNECT-FLOW SCREEN LATCHED AN OUTCOME. See Action::wifi() and
// App::wifiRequested()'s four-step note, which this follows in order -- and
// note step 2: the screen is STILL ON TOP, which is the whole reason nothing
// was popped. After a pop there is no screen left to ask.
static void handleWifi() {
  gApp->clearWifiRequest();

  const reader::ScreenId id = gApp->top().id();
  switch (id) {
    case reader::ScreenId::WifiPicker: {
      auto& p = static_cast<reader::WifiPickerScreen&>(gApp->top());
      if (p.rescanChosen()) {
        p.clearChoice();
        gScanDelivered = false;  // a second scan owes a second delivery
        if (gRadio.beginScan()) p.setScanning(true);
        return;
      }
      // A COPY, not a reference: the push below can destroy the screen these
      // live in.
      const std::string ssid = p.chosenSsid();
      if (ssid.empty()) return;
      const bool locked = p.chosenLocked();
      p.clearChoice();

      gJoinSsid = ssid;
      gJoinLocked = locked;
      // AN OPEN NETWORK JOINS DIRECTLY and a locked one asks for a password
      // -- the board's own note -- EXCEPT where a passphrase is already
      // stored, which is the case the picker cannot know about and the
      // reason it reports `locked` rather than deciding.
      gJoinPsk = locked ? shellwifi::secret(ssid) : std::string();
      if (locked && gJoinPsk.empty()) {
        gFactory.setWifiTarget(gJoinSsid);
        gApp->pushScreen(reader::ScreenId::WifiPassword);
        return;
      }
      beginJoinFlow(/*replace=*/false);
      return;
    }

    case reader::ScreenId::WifiPassword: {
      const auto& kb = static_cast<const reader::WifiPasswordScreen&>(gApp->top());
      if (kb.cancelled()) {
        endWifiSession();
        dispatchBack();
        return;
      }
      if (!kb.joinChosen()) return;
      gJoinPsk = kb.entered();
      beginJoinFlow(/*replace=*/true);
      return;
    }

    case reader::ScreenId::WifiConnect: {
      // The only outcome this screen latches is the cancel; READY and the
      // failures are the POLL's, below.
      endWifiSession();
      dispatchBack();
      return;
    }

    case reader::ScreenId::WifiError: {
      const auto& e = static_cast<const reader::WifiErrorScreen&>(gApp->top());
      switch (e.chosen()) {
        case reader::WifiErrorScreen::Chosen::EditPassword:
          // BACK TO THE KEYBOARD HOLDING WHAT WAS TYPED, which is the whole
          // reason that slab exists. One call carries both, so a fresh join
          // cannot inherit this passphrase -- see setWifiTarget.
          gFactory.setWifiTarget(gJoinSsid, gJoinPsk);
          gApp->replaceScreen(reader::ScreenId::WifiPassword);
          return;
        case reader::WifiErrorScreen::Chosen::TryAgain:
          beginJoinFlow(/*replace=*/true);
          return;
        case reader::WifiErrorScreen::Chosen::Cancel:
        case reader::WifiErrorScreen::Chosen::None:
          endWifiSession();
          dispatchBack();
          return;
      }
      return;
    }

    case reader::ScreenId::WifiNetworkActions: {
      const auto& a = static_cast<const reader::WifiNetworkActionsScreen&>(gApp->top());
      if (!a.forgetChosen()) return;
      const std::string ssid = a.facts().ssid;  // a copy; the pops destroy the screen
      gWifiNets.forget(ssid);
      shellwifi::dropSecret(ssid);
      shellwifi::save(gWifiNets);
      primeWifi();
      // THE HUB UNDERNEATH HOLDS ITS OWN COPY OF THE LIST, so popping back to
      // it would show the network still there. Home's `gHomeStale` rebuild is
      // the precedent: the screen is REPLACED rather than asked to refresh,
      // because the list it was built from is the thing that changed.
      dispatchBack();                                     // the overlay
      gApp->replaceScreen(reader::ScreenId::WifiSettings);  // a fresh hub
      logf("[wifi] forgot %s\n", ssid.c_str());
      logFlush();
      return;
    }

    default:
      logf("[wifi] latched with %s on top\n", reader::screenName(id));
      logFlush();
      return;
  }
}

// THE SCAN AND THE JOIN, POLLED FROM loop()'s QUIET WINDOW -- the interface is
// poll-shaped precisely so this is not a callback on the system event task.
// See reader/wifi_radio.h.
static void pollWifi() {
  const reader::ScreenId id = gApp->top().id();

  if (id == reader::ScreenId::WifiPicker) {
    auto& p = static_cast<reader::WifiPickerScreen&>(gApp->top());
    // ARRIVING AT THE PICKER STARTS EXACTLY ONE SCAN. The hub pushes this
    // screen itself, so there is no press for the shell to hang a scan on --
    // the screen appearing IS the trigger.
    if (!gScanArmed) {
      gScanArmed = true;
      gScanDelivered = false;
      if (gRadio.beginScan()) p.setScanning(true);
      return;
    }
    // ONCE PER SCAN. scanState() stays Done once it is Done, so an ungated
    // setResults here runs every iteration and resets the focus to the top
    // every time -- which is exactly what it is specified to do, and is the
    // reason the list would not stay where the reader put it.
    if (gScanDelivered) return;
    if (gRadio.scanState() == reader::ScanState::Done) {
      gScanDelivered = true;
      p.setResults(reader::rankScanResults(gRadio.scanResults()));
      gApp->markDirty();
    } else if (gRadio.scanState() == reader::ScanState::Failed) {
      // AN EMPTY PICKER, NOT A JOIN FAILURE -- beginScan's own contract: a
      // radio that would not come up is not a network that rejected you.
      gScanDelivered = true;
      p.setResults({});
      gApp->markDirty();
    }
    return;
  }
  gScanArmed = false;
  gScanDelivered = false;

  if (id == reader::ScreenId::WifiConnect) {
    auto& dlg = static_cast<reader::WifiConnectScreen&>(gApp->top());
    switch (gRadio.joinState()) {
      case reader::JoinState::Ok: {
        // PERSISTED ONLY ON SUCCESS. A passphrase that did not work is not
        // worth keeping, and storing it would make the next boot's
        // dropLockedWithoutSecret keep a row that can only fail.
        gWifiNets.remember(gJoinSsid, gJoinLocked);
        if (gJoinLocked) shellwifi::putSecret(gJoinSsid, gJoinPsk);
        shellwifi::save(gWifiNets);
        primeWifi();
        // AND THE RADIO GOES DOWN AT READY, which is the point of the whole
        // on-demand design: the join existed to prove the credential.
        gRadio.down();
        if (dlg.markReady()) gApp->markDirty();
        return;
      }
      case reader::JoinState::Failed: {
        gFactory.setWifiFailure(reader::wifiFailureFor(gRadio.joinReason()));
        gFactory.setWifiTarget(gJoinSsid);
        gRadio.down();
        gApp->replaceScreen(reader::ScreenId::WifiError);
        return;
      }
      case reader::JoinState::Idle:
      case reader::JoinState::Running:
        return;
    }
  }
}

// THE USER CONFIRMED A DELETE. See Action::del() and app.h's five-step note, which
// this follows in order.
//
// THE PATH IS READ WHILE DeleteConfirm IS STILL ON TOP, because leaving the flow is
// what takes it away and after that there is no screen left to ask. Contents'
// chosenSpine() has exactly this shape and for exactly this reason.
static void handleDelete() {
  // The latch first, so a removal that fails does not re-fire on every loop.
  gApp->clearDeleteRequest();

  const reader::Screen& top = gApp->top();
  if (top.id() != reader::ScreenId::DeleteConfirm) {
    logf("[delete] latched with no confirmation on top\n");
    logFlush();
    return;
  }
  // A COPY, not a reference: the Backs below destroy the screen these live in.
  const reader::DeleteConfirmScreen::Facts facts =
      static_cast<const reader::DeleteConfirmScreen&>(top).facts();

  // KEEP SD TRAFFIC OFF THE DISPLAY BUS, exactly as handleFinish and the retry do.
  // The card shares the panel's SPI and SDCardManager does no locking at all, so a
  // transfer racing a refresh is the kind of fault that looks random. The guard is
  // recursive and SdFileSystem takes it per method; this is the whole-sequence one.
  SpiBusGuard bus;

  // THE RESULT IS NOT BRANCHED ON. FileSystem::remove reports the END STATE, so a
  // false means the file is still there -- and the list the reader is about to be
  // looking at has just been rescanned and already says which it was. An error panel
  // would be a screen with no board saying what the Library already shows.
  const bool gone = gSd.remove(facts.path);
  logf("[delete] %s -> %s\n", facts.path.c_str(), gone ? "gone" : "still there");
  logFlush();

  // HOME NEEDS NO FLAG HERE, AND THAT IS THE POINT OF #43. Its LIBRARY count and its
  // CONTINUE block are both keyed on gSd.removals(), which gSd.remove has just
  // advanced above every one of its own refusals -- so HomeRebuildGate sees the
  // removal whether or not this handler remembers to say anything, and so will the
  // next door to a removal that somebody adds. A markStale() here would be a second
  // spelling of one fact, and the first spelling is the one that cannot be forgotten.
  //
  // THE LIBRARY IS STILL TOLD, and it is not the same fact: its rows come from a
  // listing taken when it was pushed, its consumer below re-derives PERCENTAGES only,
  // and a row has gone -- so it is rescanned outright, on the press that removed the
  // book. The flag is what covers the reachable stacks this handler does not land on.
  //
  // Both consumers are BELOW this call in loop(), which is handleFinish's placement
  // and its reason: this handler LEAVES a screen, so the screen it lands on has to be
  // repainted on the very press that caused the removal rather than one press later.
  gLibraryStale = true;
  // RESCANNED, not refreshed: a row has gone, and refreshProgress only re-derives the
  // percentages of rows that are already there. The listing cache was dropped by
  // remove()'s own forgetCardFacts, so this reaches the card -- which it must.
  if (reader::LibraryScreen* lib = gFactory.library(); lib != nullptr) lib->rescan();

  // ...AND NOW LEAVE, down to whatever asked. From the actions panel that is the
  // Library; from BookError it may be Home, and the BookError under this confirmation
  // goes too -- it names a book that no longer exists.
  //
  // SYNTHESISED BACKS RATHER THAN popTo(). THE SHELL CANNOT APPLY AN Action AT ALL:
  // App::dispatch takes an InputEvent, App exposes pushScreen() and no popScreen() and
  // no apply(Action), and an Action is a value a SCREEN returns. dispatchBack() above
  // exists for precisely this, and handleFinish leaves BookEnd the same way -- whatever
  // Back means on each screen is what runs, decided by the screen, once.
  //
  // BOUNDED THREE WAYS, because a Back that does not pop would otherwise spin loop()
  // forever: the target is reached, the root is reached (popTo's own "stop at the
  // root" rule, which is what a returnTo of Library means on a stack that has none),
  // or a Back moved nothing. Every reachable stack costs two -- DeleteConfirm over
  // ItemActions over the Library, and DeleteConfirm over BookError over Home or the
  // Library.
  for (int guard = 0; guard < 8; ++guard) {
    if (gApp->top().id() == facts.returnTo || gApp->depth() <= 1) break;
    const int was = gApp->depth();
    dispatchBack();
    if (gApp->depth() == was) {
      logf("[delete] Back moved nothing on %s\n", reader::screenName(gApp->top().id()));
      break;
    }
  }
}

// PRIME THE FACTORY WITH A BOOK AND ITS SAVED POSITION. Everything the Reader needs
// before it can be pushed, in one place, because TWO paths need it and they must
// agree: a button press (the Library's selection, or Home's CONTINUE) and a WAKE.
//
// The wake is why this was extracted. App::restore pushes the record's stack, the
// Reader's push goes through the factory, and the factory refuses a Reader with no
// book -- deliberately, since falling through to the demo is how the device once woke
// into Middlemarch. So sleeping on a page and waking landed on the Library: the
// restore correctly stopped short of a screen that could not be built. Nothing was
// wrong with the restore; the book was never set.
// `push` is false for the WAKE, where App::restore does the pushing -- it walks the
// record's whole stack and the Reader is one entry in it. Everything before the push
// is identical either way, which is the point of there being one function.
static bool openBookAt(const std::string& path, uint32_t bookBytes, bool push) {
  // ONE PLACE, because this is the one function both CONTINUE and a Library row go
  // through -- and the wake restore as well. Putting the deadline at the call sites
  // instead would have been three copies of it, and the third would have been added
  // late and differently.
  SlowOperation slow(reader::kStatusOpening);
  const uint32_t t0 = millis();
  const uint32_t heapBefore = ESP.getFreeHeap();
  reader::OpenedBook opened;
  const char* why = "";
  if (!reader::openBook(gSd, path, opened, &why)) {
    // THE HEAP GOES IN THE REFUSAL LINE, because "not enough memory" is only
    // actionable next to how much there was and how fragmented it is. The largest
    // free BLOCK is the number that actually decides: the reader's allocations are
    // single contiguous buffers, and a heap with 140 KB free in 40 KB pieces cannot
    // serve a 60 KB chapter.
    logf("[open] %s REFUSED: %s (heap %u free, largest block %u)\n", path.c_str(),
         why, (unsigned)ESP.getFreeHeap(),
         (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    logFlush();
    // A REFUSAL THE READER ASKED FOR GETS A SCREEN; A REFUSAL ON THE WAKE DOES NOT.
    // `push` is false only for the session restore, where App::restore already stops
    // short of a Reader it cannot build and leaves Home or the Library standing --
    // wrong in a way the reader can see through. Waking into a modal about a book
    // nobody just asked for replaces a calm landing with an interruption, seconds
    // after pressing power and with no context for it.
    if (push && gApp != nullptr) {
      // WHICH REFUSAL, in the only vocabulary the screen has. openBook's `why` is
      // developer English and stays in the log; what reaches glass is one of three
      // bounded shapes, because "cannot open the book file" is a file that is gone
      // or a card that is -- and openRead does not call noteCardGone(), so
      // pollCardPresence takes 2-25s to notice -- while "not enough memory to ..."
      // is a book that is perfectly fine on a device that is momentarily short.
      // Telling the reader either of those is damaged would be a false claim, which
      // this firmware refuses elsewhere for the battery gauge and the charging bolt.
      //
      // THE MAPPING IS core/'s (bookErrorReasonFor), not a strcmp here. It was one,
      // against a literal this file spelled and book.cpp spelled again, and a third
      // shape would have made it two -- in the one directory with no test harness.
      // The leaf name, not the path: the board's paragraph quotes a filename.
      const size_t slash = path.find_last_of('/');
      const std::string leaf = slash == std::string::npos ? path : path.substr(slash + 1);
      // WHERE A DELETE RETURNS TO is decided here, because this is the one place that
      // knows which screen asked. Home's CONTINUE has no Library to go back to.
      const reader::ScreenId returnTo = gApp->top().id() == reader::ScreenId::Home
                                            ? reader::ScreenId::Home
                                            : reader::ScreenId::Library;
      gFactory.setBookErrorFacts({path, leaf, reader::bookErrorReasonFor(why), returnTo});
      // ...AND THE CONFIRMATION BEHIND ITS `DELETE FILE...` SLAB, PRIMED HERE TOO,
      // because the screen answers a bare `Action::push(ScreenId::DeleteConfirm)` and
      // the factory's fallback for an unprimed one is the LIBRARY'S FOCUSED ROW. From
      // the Library that fallback happens to name this same book, so the slab worked
      // by luck; from Home's CONTINUE there is no Library at all, the factory refuses,
      // and the slab does NOTHING. That is the works-only-sometimes defect the Facts
      // refactor exists to prevent -- and CONTINUE is the likeliest real corruption
      // path, because it is a book the reader was part-way through.
      //
      // The same three facts the dialog itself took: `returnTo` is decided above by
      // the one place that knows which screen asked, and the LEAF NAME is the display
      // name -- not the Library row's `title()`, because a book that will not open has
      // no OPF title to offer and the filename is the only honest name for it. It is
      // also what the dialog's own paragraph quotes one screen up, so the two agree.
      gFactory.setDeleteFacts({path, leaf, returnTo});
      if (!gApp->pushScreen(reader::ScreenId::BookError))
        logf("[open] ...and the dialog would not build\n");
    }
    return false;
  }
  const uint32_t t1 = millis();
  // mark() carries heap AND min, so these two make the open's phases visible in the
  // stage trail -- which is how a heap dip gets ATTRIBUTED rather than guessed at.
  // It was guessed at once: stb's 56 KB per-glyph edge buffer was blamed for
  // min=18,952, the buffer was cut to 3.6 KB, and min came back 18,948. Whatever
  // spends it is somewhere in here.
  mark("open-located");

  // The chapter label is the SCREEN's now: it changes when the reader pages into
  // another spine entry, so the shell cannot be the one composing it.
  // A LOCATION, not a chapter: openBook released the archive before returning, and
  // the ReaderScreen streams from these three numbers -- so nothing here holds the
  // chapter, which is the whole of 3C.
  // THE BOOK, not one chapter: the reader pages between spine entries itself, which
  // is what it needs to be a reader -- entry 0 of a real EPUB is a cover with no
  // text at all, and it showed as a blank page reading 0/0.
  // WHERE THE READER LEFT OFF, if this book has a saved position.
  //
  // The fit is GRADED rather than trusted: a book re-exported on a computer keeps
  // only its spine entry, a body size or column change keeps the block but not the
  // line, and a record found under a colliding hash keeps nothing. See
  // reading_position.h -- the point is that the top of the right chapter beats the
  // front of the book, which beats nothing.
  int startChapter = 0;
  reader::Cursor startAt{};
  // THE WAY BACK, restored only at an Exact fit -- restoreFrom decides, so this is
  // not a second place that grades it.
  reader::AnchorPos startAnchor{};
  bool haveAnchor = false;
  reader::ReadingPosition saved;
  if (reader::loadPosition(gSd, path, saved)) {
    // gBody.ppem(), NOT kBodyPpem, and this was the load-side twin of a save-side
    // bug: `p.ppem` was the same constant until Phase 7's review caught it. Both
    // directions of the constant are wrong once the size is a setting. Saved at 32
    // and reopened while the face is at 46, the record says 32, the comparison says
    // 32, and fitOf grades EXACT -- handing back a line index from a layout that
    // never existed, which is a wrong page that reads as a reader bug. Saved at 46
    // and reopened at 46 it grades RELAID and drops the exact line every time, so at
    // any non-default size a restore could never be exact. columnW was already
    // current, because the apply path updates the factory's metrics.
    const reader::PositionFit fit = reader::fitOf(saved, path, bookBytes, gBody.ppem(),
                                                  gFactory.readerMetrics().columnW);
    const reader::PositionRestore r = reader::restoreFrom(saved, fit);
    static const char* kFitWord[] = {"exact", "relaid", "rebound", "unusable"};
    logf("[progress] found a position for this book: spine=%d block=%d line=%d, fit=%s\n",
         saved.spine, saved.block, saved.line,
         kFitWord[static_cast<int>(fit)]);
    logFlush();
    // A FINISHED BOOK IS DELIBERATELY NOT RESTORED, and without this line the log
    // reports a perfectly good fit and then opens at page one -- which reads as the
    // restore having failed rather than as it having been declined. restoreFrom is
    // what decides (see its comment); this only says so out loud.
    if (saved.finished) {
      logf("[progress] ...but it is marked finished, so opening at the front\n");
      logFlush();
    }
    if (r.any) {
      startChapter = r.spine;
      startAt = r.cursor;
    }
    if (r.anchorAny) {
      startAnchor = reader::AnchorPos{r.anchorSpine, r.anchorCursor.block, r.anchorCursor.line};
      haveAnchor = true;
    }
    if (saved.hasAnchor())
      logf("[progress] and a way back: spine=%d block=%d line=%d, %s\n",
           saved.anchorSpine, saved.anchorBlock, saved.anchorLine,
           r.anchorAny ? "restored" : "DROPPED (fit below exact)");
  }
  // THE CONTENTS, HERE AND NOWHERE ELSE -- see gReading.toc for why this cannot happen
  // when the list is opened. A book with no NCX yields an empty list, which is a book
  // that reads fine and cannot name its chapters, so a failure is logged and dropped.
  gReading.toc.clear();
  {
    const uint32_t tocT = millis();
    const uint32_t tocHeap = ESP.getFreeHeap();
    const char* tocWhy = "";
    // THE STYLESHEETS RIDE THIS CALL. Both are "what the book says about itself"
    // and both are wanted here, where the archive is already open and there is heap
    // for it -- openBook has released its own and the Reader's inflater does not
    // exist yet. A second open would be ~100 ms and a second directory parse.
    std::vector<std::string> italicClasses;
    const bool tocOk = reader::loadToc(gSd, path, gReading.toc, &tocWhy, &italicClasses);
    logf("[toc] %s: %u entries in %lums, heap %u -> %u, min %u%s%s\n",
         tocOk ? "read" : "REFUSED", (unsigned)gReading.toc.size(),
         (unsigned long)(millis() - tocT), (unsigned)tocHeap,
         (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
         tocWhy[0] != '\0' ? " -- " : "", tocWhy);
    // NAMED IN THE LOG, because a book whose italics do not render has three
    // explanations and this is the one that used to be invisible. `[markup]` says
    // what the chapter CLAIMED; this says what the stylesheet ANSWERED.
    logf("[css] %u italic class(es)%s%s\n", (unsigned)italicClasses.size(),
         italicClasses.empty() ? "" : ", first: ",
         italicClasses.empty() ? "" : italicClasses.front().c_str());
    logFlush();
    gFactory.setReaderItalicClasses(std::move(italicClasses));
  }

  // WHAT A SAVE WILL NEED, captured now while it is all in hand.
  gReading.path = path;
  gReading.title = opened.title;
  gReading.author = opened.author;
  gReading.bytes = bookBytes;
  gReading.open = true;

  // THE CONTENTS GO TO THE FACTORY HERE, not when the list is opened, because the READER
  // wants them too -- its header names the chapter, and that has to be right from the
  // first paint rather than after a visit to the menu. The menu press still updates
  // which row is marked, since the reader will have moved by then.
  gFactory.setContents(gReading.toc, startChapter);
  gFactory.setReaderBook(opened, startChapter, startAt);

  // AND THE READER MENU'S HEADER, WHICH IS WHY SLEEPING ON THAT MENU USED TO WAKE
  // INTO THE BOOK.
  //
  // The factory REFUSES an unprimed ReaderMenu -- correctly, since falling back to a
  // demo name is how this device once showed MIDDLEMARCH over a real book -- and the
  // only place that primed it was the pre-dispatch input handler, gated on a Confirm
  // press with the Reader on top. The wake path never presses anything. So a record
  // reading `home;reader;reader-menu` replayed as far as the Reader, the menu push
  // was refused, and App::restore stopped there and kept what stood. Reported off the
  // device as "sleeping from the typography screen resumes to the book"; it was never
  // about Typography -- the same wake lost the reader menu and the contents, and
  // Typography reached from SETTINGS restored fine, because Settings needs no priming.
  //
  // HERE because this is the one function a button press and a wake BOTH go through,
  // which is the reason it was extracted. The percentage is the one the card's pointer
  // holds rather than a live reading, and the Confirm-press call above still refreshes
  // it -- a stale percent on a menu the user has not opened yet costs nothing, where a
  // menu that cannot be built costs them the screen they slept on.
  gFactory.setReaderMenuHeader(gReading.title.empty() ? gReading.path : gReading.title,
                               std::to_string(saved.percent) + "%");
  // ...and the way back, or explicitly NONE. Cleared rather than left alone: the
  // factory outlives one book, so a stale anchor from the previous one would offer
  // this reader a page in a book they closed.
  if (haveAnchor) {
    gFactory.setReaderAnchor(startAnchor);
  } else {
    gFactory.clearReaderAnchor();
  }

  // AND THE END-OF-BOOK SCREEN, PRIMED AT OPEN FOR THE READER MENU'S REASON.
  //
  // BookEnd is pushed from INSIDE ReaderScreen's Gesture::Next, when the walk runs out
  // of spine entries -- so there is no press the shell sees first and no moment between
  // the decision and the push. The factory refuses an unprimed BookEnd (correctly: a
  // substituted demo would put another book's title over the one just finished), and a
  // refused push leaves the last page standing with the button doing nothing, which is
  // the dead button this whole screen exists to remove.
  //
  // Everything it needs is already in hand here: the title and the author came with the
  // OPF, and the count is the spine's length -- cover included, which is exactly the
  // number Home already says `OF` in `CH. 08 OF 92`.
  reader::BookEndScreen::Facts endFacts;
  endFacts.bookTitle = opened.title;
  endFacts.author = opened.author;
  endFacts.chapterCount = opened.chapterCount();
  // WHICH DECIDES THE LEAVING SLAB'S LABEL ONLY -- the action is popTo(Library) either
  // way, and that stops at the root when there is none. Scanned rather than tracked;
  // see appHasScreen.
  //
  // IT IS EXACT ON A PRESS AND A GUESS ON A WAKE. A Confirm that opens a book leaves
  // the Library (or the overlay above it) standing, so the scan is reading the stack
  // the Reader is about to be pushed onto. The WAKE calls this with push=false and
  // BEFORE App::restore has replayed anything, so the stack is the bare root and this
  // reads false even for a record that names the Library -- the slab then says BACK TO
  // HOME and still lands on the Library. Corrected here rather than at the restore site
  // it would cost a second priming call, and this file's rule is that the second caller
  // is the extraction point rather than the first.
  endFacts.libraryBeneath = appHasScreen(*gApp, reader::ScreenId::Library);
  gFactory.setBookEndFacts(std::move(endFacts));

  const bool pushed = push && gApp->pushScreen(reader::ScreenId::Reader);
  // The push builds the screen, which locates the chapter, decodes it once to index
  // its pages, and lays out the first -- the whole expensive part.
  mark("open-paginated");

  // The page count is the expensive part, and it happened inside the push: one
  // decode of the chapter to index its pages. Reported because "how long does
  // opening a book take" is a question only the device answers, and this line is
  // the whole of the answer -- locate, paginate and heap.
  int pages = -1;
  const char* readerWhy = "";
  if (pushed) {
    const auto* rd = static_cast<const reader::ReaderScreen*>(&gApp->top());
    pages = rd->pageCount();
    readerWhy = rd->error();
    logChapterOpen(rd, millis() - t0);
  }
  // THE CROSSING DETECTOR'S STARTING POINT -- see gLastChapter. Taken from the SCREEN
  // where there is one, because `startChapter` is what was ASKED for and openChapterAt
  // skips a spine entry that paginates to nothing: three of a real book's 92 are a
  // cover and two title pages, so the two differ on exactly the opens where it matters.
  // On the wake path there is no screen yet (App::restore does the pushing), and the
  // requested chapter is the best that is known.
  gLastChapter = pushed ? static_cast<const reader::ReaderScreen*>(&gApp->top())->chapterIndex()
                        : startChapter;
  // THE STACK HIGH-WATER MARK, because a stack is the one budget this firmware had
  // no instrument for -- and the first thing to exhaust it did so on the very first
  // book. uxTaskGetStackHighWaterMark reports the SMALLEST free space the task has
  // ever had, so this is the worst case across everything the device has done since
  // boot, the inflate included. If it approaches zero, the next layer added to the
  // reader panics like the first one did.
  logf("[stack] loopTask free at worst: %u bytes of %u\n",
       (unsigned)(uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t)),
       (unsigned)getArduinoLoopTaskStackSize());
  logf("[open] %s -> \"%s\" ch=1/%d: locate=%lums total=%lums pages=%d "
       "entry=%uB heap %u -> %u (cost %ld) min=%u pushed=%d%s%s\n",
       path.c_str(), opened.title.c_str(), opened.chapterCount(),
       (unsigned long)(t1 - t0), (unsigned long)(millis() - t0), pages,
       (unsigned)opened.locate(0).compressedSize, (unsigned)heapBefore,
       (unsigned)ESP.getFreeHeap(),
       (long)heapBefore - (long)ESP.getFreeHeap(),
       (unsigned)ESP.getMinFreeHeap(), pushed ? 1 : 0,
       readerWhy[0] != '\0' ? " reader-refused: " : "", readerWhy);
  logFlush();
  return true;
}

static void handleRetry() {
  // Clear the latch FIRST, so a failed attempt cannot re-fire on every loop.
  gApp->clearRetryRequest();
  mark("sd-retry");
  if (gSdBeganOnce) {
    logf("[sd] retry: the card mounted earlier this boot and was then lost. "
         "SDCardManager::begin() short-circuits on its own `initialized` flag and "
         "the SPI path has no end()/unmount(), so it cannot be re-initialised in "
         "process -- RESTARTING, which re-runs the whole mount path\n");
    logFlush();
    mark("sd-retry-restart");
    logFlush();  // the restart is immediate; nothing buffered survives it
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
  gFactory.setSettings(gSettings);  // ...and the factory's copy is now stale
  // ...and it may have no /books either. Before buildHomeApp() below, which
  // counts what is in there for Home's LIBRARY row.
  ensureBooksDir("retry");
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
  if (!gCrumbs.probeFirstDone && (deepDue || static_cast<uint32_t>(now - gLastSdPollMs) == 0)) {
    // The FIRST probe of this boot, recorded whether it passed or not. On the
    // reported wake fault the card was present, boot mounted it and read
    // settings.json off it, and then the SD-missing screen appeared about a
    // second later -- so which of those two the first probe agrees with is the
    // whole question.
    gCrumbs.probeFirstDone = 1;
    gCrumbs.probeFirstOk = by == nullptr ? 1 : 0;
    gCrumbs.probeFirstMs = now;
    saveCrumbs();
  }
  if (!by) return;  // nothing due, or the card answered

  // A usable -> unusable edge. noteCardGone() has already said what stopped
  // answering; this says which mechanism asked, and what the UI is doing about it.
  gStorageUsable = false;
  gCrumbs.cardLostMs = now == 0 ? 1u : now;  // 0 is the "never" sentinel
  snprintf(gCrumbs.lostBy, sizeof(gCrumbs.lostBy), "%s", by);
  saveCrumbs();  // the event this whole record exists for
  logf("[sd] THE CARD IS NO LONGER ANSWERING -- pulled, or failed. Detected by %s. "
       "Routing to the SD-missing screen; RETRY will restart the device, because a "
       "card lost after a mount cannot be re-mounted in process\n",
       by);
  logFlush();
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
  logf("[detect] i2c verdict=%s (pass scores %u/%u) -> %s\n",
       verdict == freeink::XteinkVerdict::X3Confirmed    ? "X3Confirmed"
       : verdict == freeink::XteinkVerdict::X4Confirmed  ? "X4Confirmed"
                                                                  : "Inconclusive",
                s1, s2, isX3 ? "X3" : "X4");

  BoardConfig::selectDevice(isX3 ? BoardConfig::Board::XteinkX3
                                 : BoardConfig::Board::XteinkX4);

  const bool promoted = freeink::applyXteinkDisplayController();
  const auto& diag = freeink::getXteinkDisplayProbeDiag();
  logf("[detect] controller probe valid=%d promoted=%d "
       "ver=%02X %02X %02X %02X %02X flg=%02X\n",
       diag.valid, promoted, diag.ver[0], diag.ver[1], diag.ver[2],
       diag.ver[3], diag.ver[4], diag.flg);

  if (isX3 && BoardConfig::ACTIVE.displayController == BoardConfig::DisplayController::UC8279) {
    BoardConfig::selectDevice(BoardConfig::Board::XteinkX3Uc8279);
    logf("[detect] promoted profile to XteinkX3Uc8279\n");
  }
  logf("[detect] active controller=%u\n",
       (unsigned)BoardConfig::ACTIVE.displayController);
  logFlush();

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
//
// `copy` is now in the same position: the render draws straight into the
// driver's own framebuffer (gFrame views it), so there is no setFramebuffer()
// memcpy left to time and this counter is never incremented. It is kept for ONE
// reason -- the [paint] line is the only evidence available that the copy is
// actually gone on hardware, and this change was made without flashing. Once a
// A device log has now shown `copy=0` (2026-08-22), so the field is retired the
// way `rotate` was: rendering goes straight into the driver's framebuffer and
// there is no copy left to time.
static uint32_t gDrawMs = 0;
// How long setup() waited for USB CDC. See the wait loop for why it varies.
static uint32_t gSerialWaitMs = 0;

// One render pass, straight into the panel-oriented frame.
//
// The rotation is the framebuffer's, declared once in bindFrameToDriver();
// nothing here transposes anything. CCW is the correct direction, verified on X3
// hardware (CW renders the whole screen 180 degrees out -- the two differ by
// exactly half a turn, so swapping them is not the fix for a mirrored image).
// Unverified on X4: if an X4 comes out upside down, the Rotation passed to the
// constructor in bindFrameToDriver() is the line, not anything in here.
// Whether the last render pass repainted the top screen alone. For the log line
// only -- the decision is App's, and it is remade per pass.
static bool gPartialPaint = false;

// Point gFrame at the driver's framebuffer, if there is one to point at.
//
// THE POINTER IS NOT PERMANENTLY VALID, and that is the whole reason this is a
// function rather than a line in setup(). FreeInkDisplay::lendBuildStorage()
// hands the framebuffer's own bytes out as scratch for a memory-hungry phase --
// a chapter layout on a PSRAM-less part is what the SDK wrote it for -- and
// while they are lent getFrameBuffer() returns NULL and rendering is
// unavailable. Nothing in this firmware calls it today; Phase 3C's pagination is
// expected to, which is exactly the kind of change that would otherwise write a
// screen into a null pointer.
//
// The allocation itself never moves (that is lendBuildStorage's stated reason
// for existing -- free/re-malloc of 48 KB fragmented the heap), so in practice a
// return gives the same address back. This does not rely on that: it compares,
// and rebinds if it differs.
//
// Either way returnBuildStorage() memsets the frame to white, so the App's
// record of what it last painted is no longer true of these bytes -- hence
// gFrameContentsUnknown, which forces the next paint to be a full one.
//
// Returns false when there is no usable frame; the caller must not paint.
static bool bindFrameToDriver(const char* why) {
  uint8_t* bytes = display.getFrameBuffer();
  if (bytes == nullptr) {
    // Lent out. Not fatal and not a bug: it is a phase that borrowed the frame
    // and has not given it back, and the panel keeps showing its last image.
    logf("[frame] %s: driver framebuffer is lent out; nothing to paint into\n", why);
    logFlush();
    gFrame.reset();
    gFrameBytes = nullptr;
    gFrameContentsUnknown = true;
    return false;
  }
  if (gFrame && bytes == gFrameBytes) return true;  // the common case: no change
  // Logical portrait, physical landscape: the dimensions are what the screens
  // draw against (528x792 on the X3), and Rotation::Ccw maps them into the
  // panel's own 792x528 buffer as it draws. CCW is measured on X3 hardware; see
  // the note on reader::Rotation. Unverified on X4 -- if an X4 comes out upside
  // down, this line is it, not anything in the paint path.
  gFrame.emplace(bytes, display.getBufferSize(), display.getDisplayHeight(),
                 display.getDisplayWidth(), reader::Rotation::Ccw);
  gFrameBytes = bytes;
  gFrameContentsUnknown = true;
  // A view whose geometry does not fit the memory behind it is REFUSED by the
  // Framebuffer constructor rather than clamped, and reports sizeBytes() == 0 --
  // so this one check catches both "the driver's buffer is smaller than the
  // panel geometry needs" (the refusal, and the case that would otherwise write
  // past the end of the driver's allocation) and "the two disagree about the
  // size" (a rotation applied to the coordinates but not to the store would be
  // exactly this many bytes and still laid out wrong, so it is not the whole
  // proof -- test_rotate.cpp's byte-identity case against rotate90CCW is).
  if (gFrame->sizeBytes() != static_cast<int>(display.getBufferSize())) {
    logf("[fatal] frame view %d bytes != driver buffer %u (panel %dx%d)\n",
         gFrame->sizeBytes(), (unsigned)display.getBufferSize(),
         (int)display.getDisplayWidth(), (int)display.getDisplayHeight());
    logFlush();
    mark("frame-view-REFUSED");
    gFrame.reset();
    gFrameBytes = nullptr;
    return false;
  }
  logf("[frame] %s: viewing driver framebuffer %p, %d bytes, logical %dx%d CCW\n", why,
       (const void*)gFrameBytes, gFrame->sizeBytes(), gFrame->width(), gFrame->height());
  logFlush();
  return true;
}

static void paintPlane(reader::Plane plane) {
  const uint32_t t0 = millis();
  // THE PARTIAL REPAINT, and this is the ONLY place it is taken.
  //
  // App refuses unless the frame in gFrame is the one IT last painted, in this
  // plane, with this screen on top, at this depth, on a change that was not a
  // transition, and with the top screen an overlay whose own footprint has not
  // moved (App::canRenderTopOnly lists every condition and the failure each one
  // is for). So the question "is the frame still what I think it is" is answered
  // by the code that painted it, not by this function remembering to ask.
  //
  // NOTE WHERE THE CLEAR IS: inside the else, and it has to be. A partial repaint
  // over a cleared frame is the stale-pixel bug with white instead of stale
  // pixels -- an overlay panel floating on paper, the same wrong frame
  // App::render exists to prevent. Nothing else in this file writes gFrame;
  // showOnePass and the grayscale copies only read it.
  //
  // gFrameContentsUnknown IS THE ONE CONDITION App CANNOT SEE. Its record
  // compares the Framebuffer's ADDRESS, and gFrame's address never changes now
  // that it is an optional in .bss -- so a frame whose bytes were lent out and
  // handed back wiped white (returnBuildStorage) would still satisfy every one
  // of App's checks. The frame is the caller's responsibility precisely because
  // the caller is the only thing that could have clobbered it, and this is that
  // responsibility discharged.
  gPartialPaint =
      !gFrameContentsUnknown && gApp->renderTopOnly(*gFrame, *gFonts, gTheme, plane);
  if (!gPartialPaint) {
    gFrame->clear(true);
    // THROUGH App::render, NOT top().render. 2C-2's overlays are panels over a
    // still-visible parent, so the top screen alone is a panel floating on white
    // -- and this line said top() until Task 6, which would have shipped exactly
    // that to the device while the simulator (which already went through
    // App::render) and all eight goldens kept passing. One paint path is the
    // whole point of App::render existing.
    //
    // renderTopOnly above is not a second paint path in that sense: it is
    // App's, it is refused by default, and it paints only over a frame App
    // itself last filled through this line.
    gApp->render(*gFrame, *gFonts, gTheme, plane);
    // The frame now holds what App just put there, and App has recorded it.
    gFrameContentsUnknown = false;
  }
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
  //
  //    THAT MEMCPY IS GONE, because gFrame now VIEWS the driver's own
  //    frameBuffer: the render above already landed the frame exactly where
  //    displayGrayscaleBase reads from. Keeping the call would have been
  //    memcpy(p, p, n) with src == dst, which is undefined behaviour rather
  //    than a harmless no-op. The sentence above still explains why this step
  //    needs the frame to be in the driver's buffer at all, which is the part
  //    that was easy to get wrong.
  paintPlane(reader::Plane::Bw);
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
  //
  //    NOW THAT THE FRAME IS THE DRIVER'S, each render here OVERWRITES the base
  //    frame in the driver's buffer instead of leaving a private copy of it
  //    alone. Checked against the SDK rather than assumed, because this is the
  //    one behavioural difference the change makes on this path:
  //      * displayGrayscaleBase (step 1) has already read and sent the base to
  //        the controller by the time step 3 runs. It retains nothing.
  //      * displayGrayBuffer (step 4) DOES pass the driver's frameBuffer down --
  //        which now holds the MSB plane rather than the base. All three panel
  //        drivers this binary can select ignore that argument on the gray path
  //        (`(void)fb;` in Uc8279Driver, Uc8253X3Driver and Uc8279X4Driver:
  //        the waveform comes from a built-in bank and the image from the planes
  //        already in controller RAM), so the buffer they are handed does not
  //        reach the glass.
  //      * cleanupGrayscaleBuffers (step 4) already handles this exact case: it
  //        skips its own restoring memcpy when `frameBuffer == bwBuffer`.
  //    A fourth driver that reads `fb` on the gray path would be the thing to
  //    re-check here, and no screen declares Grayscale today, so this path is
  //    unexercised on hardware either way.
  paintPlane(reader::Plane::Lsb);
  display.copyGrayscaleLsbBuffers(gFrame->data());
  paintPlane(reader::Plane::Msb);
  display.copyGrayscaleMsbBuffers(gFrame->data());
  mark("gray-planes-written");

  // 4. Paint the combined 4-level image (the driver reads the planes it was
  //    handed, not any framebuffer), then put the controller back on a valid
  //    B/W baseline so the next ordinary refresh is differentially sane.
  //
  //    A CORRECTION TO THE PARENTHESIS, found while making the frame the
  //    driver's: displayGrayBuffer() does pass FreeInkDisplay::frameBuffer down
  //    to the panel driver. It is the drivers that ignore it -- all three this
  //    binary can select declare `(void)fb;` on their gray path -- so the
  //    sentence is right about the glass and wrong about the call. It matters
  //    now because that buffer is ours and holds the MSB plane at this point.
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

// Refresh the panel from the one 1-bit frame. Its bytes are already in the
// panel's own landscape orientation AND already in the driver's own buffer --
// gFrame views it -- so there is now literally nothing between the render and
// the waveform. Shared by both one-pass paths below, which differ only in the
// plane they render.
//
// The setFramebuffer() memcpy that used to be here is gone, which is what makes
// `copy=` zero in the [paint] line. `displayBuffer` reads the driver's
// frameBuffer directly in single-buffer mode (EINK_DISPLAY_SINGLE_BUFFER_MODE=1,
// which is how this firmware is built), so the frame it sends is the one just
// rendered.
//
// SPLIT INTO ITS TWO HALVES, AND THE SPLIT IS A MEASUREMENT, NOT A BEHAVIOUR
// CHANGE. `displayBuffer` is `displayStart` then `displayFinish` with nothing in
// between (Uc8279Driver::display is literally those two lines), and
// `triggerDisplay`/`completeDisplay` are the same pair with the seam exposed --
// verified against FreeInkDisplay.cpp for the configuration this firmware builds:
//
//   * EINK_DISPLAY_SINGLE_BUFFER_MODE, so both take the `prev == nullptr` branch
//     and neither swaps buffers.
//   * `_inverted` and `_inversionDirty` are false forever -- nothing here calls
//     setInverted -- so triggerDisplay's fall-back-to-displayBuffer guard and
//     displayBuffer's FAST->HALF promotion are both dead for us.
//   * completeDisplay() is syncPendingAsync(), which is the driver's
//     displayFinish() and nothing else.
//
// What it buys is the one division the log could not make: `up=` is the 52,272-byte
// plane write plus the bank load plus the trigger, which is OURS and bounded by
// the 20 MHz SPI clock; `wave=` is the BUSY wait plus the post-waveform DTM1 sync.
// Before this, both were "panel" and a slow paint could not say which.
//
// NOTE WHAT `wave=` STILL CONTAINS: displayFinish waits out the waveform and THEN
// writes the 52 KB baseline plane again, so ~21 ms of it is a second SPI upload
// happening after the image is already on glass. That part is not latency the user
// sees; it is latency the NEXT press waits behind.
static uint32_t gUploadMs = 0;
static uint32_t gWaveMs = 0;

static void showOnePass(reader::RefreshMode mode) {
  const EInkDisplay::RefreshMode m = mode == reader::RefreshMode::Full
                                         ? EInkDisplay::FULL_REFRESH
                                         : EInkDisplay::FAST_REFRESH;
  const uint32_t t0 = millis();
  display.triggerDisplay(m);
  const uint32_t t1 = millis();
  display.completeDisplay();
  gUploadMs += t1 - t0;
  gWaveMs += millis() - t1;
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

// ONE READING, from ONE call. readStatus() reports percentage, millivolts and
// charging each with its own `Known` flag, and those flags are the whole point:
// BatteryMonitor's unchecked accessors answer a FAILED read with 0, so "0%" and
// "the gauge did not answer" are the same value out of that API.
//
// Not readPercentageChecked() + isCharging(), which is one I2C transaction cheaper:
// those are two calls that can observe two different instants, and isCharging()
// DISCARDS the known flag that gChargingObservable needs. One call site that
// cannot disagree with itself is worth ~150 us.
static reader::BatteryReading readBattery() {
  const BatteryMonitor::Status s = gBatteryMonitor.readStatus();
  reader::BatteryReading r;
  r.percentKnown = s.percentageKnown;
  r.percent = static_cast<int>(s.percentage);
  r.chargingKnown = s.chargingKnown;
  r.charging = s.charging;
  // STICKY, NOT FIRST-SAMPLE. readStatus() reads SoC and charging as two
  // independent I2C transactions, so the charging half can fail on its own --
  // and deciding this from one sample would let a single glitch at the first
  // Home paint disable the plug-in poll for the rest of the session, on a
  // device that supports it perfectly well. On an X4 no reading ever reports
  // chargingKnown, so this stays false there, which is the whole point of the
  // gate. On an X3 it arms itself at the first reading that succeeds -- and the
  // paint-time read runs on every Home paint regardless of this flag, so it
  // is self-healing rather than needing a retry of its own.
  if (r.chargingKnown) gChargingObservable = true;
  if (!gBatteryEverRead) {
    gBatteryEverRead = true;
    logf("[battery] %s pct=%s charging=%s (%s backend)\n",
         s.supported ? "supported" : "UNSUPPORTED",
         s.percentageKnown ? String(s.percentage).c_str() : "unknown",
         s.chargingKnown ? (s.charging ? "yes" : "no") : "unknown",
         BoardConfig::ACTIVE.batteryGauge.gaugeAddr != 0 ? "I2C gauge" : "ADC");
    logFlush();
  }
#ifdef ENCRE_BATTERY_FAKE_PERCENT
  // THE ONLY WAY TO WALK THIS LADDER ON GLASS. Draining a real pack to 3% on demand
  // is not practical, and without this the Low banner, the critical shutdown and the
  // resume gate are all unwalkable. ENCRE_FS_SELFTEST's shape: absent by default, so
  // a normal build has neither the branch nor the log line.
  //
  // AFTER the sticky gChargingObservable arm and after the first-read line, so a
  // faked build still reports what the gauge really said and still arms the poll the
  // way a real one does -- the override is the last word on the percent and touches
  // nothing else.
  //
  // IT OVERRIDES THE PERCENT AND NOTHING ELSE. `charging` stays whatever the gauge
  // said, so an X3 on the cable still suppresses Critical -- which is one of the
  // things that needs verifying on glass and would be untestable if this faked it
  // too.
  r.percentKnown = true;
  r.percent = ENCRE_BATTERY_FAKE_PERCENT;
  static bool announced = false;
  if (!announced) {
    announced = true;
    logf("[battery] FAKE percent=%d -- this is not a real reading\n", r.percent);
    logFlush();
  }
#endif
  return r;
}

// HOME IS THE SCREEN ABOUT TO BE PAINTED. One predicate, because two sites ask it
// -- the paint-time read and the periodic poll -- and a poll that thought Home was
// showing while the paint site did not would repaint a screen with no battery on it.
//
// top() is sufficient and App::render's walk is not needed: nothing is ever pushed
// as an overlay over Home. There are exactly four overlays -- ItemActions and
// DeleteConfirm on the Library, ReaderMenu and Peek on the Reader -- and Home
// reaches neither parent without being pushed off the top first. If that changes,
// this becomes "the topmost non-overlay is Home", in one place.
static bool homeOnGlass() {
  return gApp && gApp->top().id() == reader::ScreenId::Home;
}

// COULD THE CHARGE LATCH'S REPAINT REACH THE GLASS FROM HERE. Two callers, and they
// must not be two copies: the poll's repaint site spends a refresh on it, and
// BatteryTracker::pollIntervalMs() asks it to decide whether kUnlatchMs's 60 s dwell
// is being sampled often enough to mean "continuous". Two spellings of one condition
// is the shape that has shipped a dead button twice in this project -- and here the
// drift would be silent in the worse direction: a cadence that thought the repaint
// reachable while the repaint site did not would keep the fast interval for nothing,
// which is exactly the battery #96 is about.
//
// gChargingObservable is STICKY and never arms on an X4 (no charge-status pin), so on
// that model this is false for the whole session and the band is served by the
// paint-time read alone -- which is correct, because an X4 cannot report charging and
// has no bolt to put up or take down.
static bool bandRepaintPossible() { return gChargingObservable && homeOnGlass(); }

// Take a reading and hand it to the screen. Returns whether the tracker asked for
// a repaint, which only the POLL acts on.
static bool refreshBatteryOnHome() {
  if (!homeOnGlass()) return false;
  gBattery.update(readBattery(), millis());
  static_cast<reader::HomeScreen&>(gApp->top())
      .setBattery(gBattery.percent(), gBattery.charging());
  return gBattery.takeRepaintRequest();
}

// THE LADDER'S READING, ON ANY SCREEN. refreshBatteryOnHome() cannot serve it: it
// is gated on homeOnGlass() AND on gChargingObservable, so nothing outside Home
// ever reads the gauge and on an X4 -- which has no charge-status pin -- nothing
// reads it at all. Both gates are right for what they guard (the band, and the
// charge-latch repaint); neither can be reused for a safety mechanism.
//
// It goes through the SAME gBattery.update(), so the level and the band can never
// disagree about the percent.
//
// NO SpiBusGuard, and that is what makes even the fast cadence affordable: this is
// I2C on the sensor bus and cannot race a panel refresh. Three register reads at
// 400 kHz on the X3's BQ27220 (SoC, voltage, Current), ~450 us; one ADC conversion on
// the X4.
static void pollBatteryLevel() { gBattery.update(readBattery(), millis()); }

// ARM THE BANNER ON A FRESH ENTRY INTO Low, and only while the Reader is on TOP.
//
// gWasLow is the EDGE, not the state: re-arming on every poll would put the banner
// back the moment the reader dismissed it, which is the dead-button defect with the
// sign flipped. It re-arms when the level leaves Low and comes back -- and a wake is
// a chip reset, so a low battery shows the banner again on every wake. That is the
// right behaviour and, when the Reader is what the wake restores, it rides that
// paint and costs no extra waveform.
//
// ON TOP rather than on the stack, unlike the Typography apply: the banner is drawn
// by renderReader, so with a Peek or the reader menu over it there is nothing to
// see. An armed banner under an overlay simply waits -- ReaderScreen holds the
// field and the overlay's pop reveals it.
//
// level() != Normal RATHER THAN == Low: a device that reaches Critical without a
// poll landing on Low in between must still warn. The shutdown is kCriticalDwellMs
// away and the banner is what explains it.
//
// THE EDGE IS SPENT ONLY WHEN IT IS DELIVERED, and this had it the other way round.
// `gWasLow = low` ran BEFORE the Reader test, so a crossing that happened while the
// reader was anywhere else was CONSUMED with nothing drawn -- and since the flag
// stays true for as long as the pack stays low, the banner was then lost for the
// whole session. The device boots to Home, so with a low battery the very first
// poll ate the only edge there would ever be and opening a book showed nothing.
//
// The real case is the same shape and worse: the pack crosses 10% while the reader
// is on Home or in the Library, and the warning they are owed is silently gone.
// Keeping the edge until a Reader is on top to receive it is what makes "on a fresh
// entry into Low" mean what it says. Dismissal is unaffected -- the flag is true by
// then, so the banner does not come back until the level has left Low and returned.
static bool gWasLow = false;
static void armBannerIfNewlyLow() {
  const bool low = gBattery.level() != reader::BatteryLevel::Normal;
  // Leaving Low re-arms, and does so wherever the reader is standing: this is the
  // state going away, not a notification being delivered.
  if (!low) {
    gWasLow = false;
    return;
  }
  if (gWasLow) return;  // already told them, this entry into Low
  // NOT YET DELIVERABLE -- keep the edge rather than spending it. renderReader is
  // what draws the band, so with anything else on top there is nothing to show and
  // nothing to consume.
  if (!gApp || gApp->top().id() != reader::ScreenId::Reader) return;
  gWasLow = true;
  static_cast<reader::ReaderScreen&>(gApp->top()).setBatteryLow(gBattery.percent());
  // markDirty(), not a transition: this is the same screen with one band drawn over
  // it, so it takes the 389 ms DU rather than the 693 ms GC. It also resets the
  // partial-repaint record, which is right here for the reason it was added -- the
  // Reader is not an overlay and was never eligible for the partial path anyway, so
  // this costs nothing and cannot leave the banner unpainted.
  gApp->markDirty();
  logf("[battery] low pct=%d -> banner armed\n", gBattery.percent());
  logFlush();
}

static void renderTop() {
  // BEFORE the SpiBusGuard below, and deliberately: this is I2C on the sensor bus
  // and has nothing to do with the display's SPI, so keeping the two visibly apart
  // is worth a line. Three transactions, ~450 us at 400 kHz, against a 439 ms
  // panel. What it buys is that the number on the glass was measured when the
  // glass was painted -- no timer, no staleness to reason about.
  //
  // The repaint request is TAKEN AND DISCARDED. An edge seen here -- in either
  // direction, since a confirmed unplug grants one too -- is already being
  // satisfied by the paint that is about to happen; leaving the request standing
  // would fire a second refresh at the next poll, immediately after it.
  //
  // UNTRACKED IN [i] ON PURPOSE, STATED RATHER THAN SILENT. This runs before t0
  // below, so its ~450 us is counted in `total` and attributable to no named
  // stage -- it lands in the gap between `post` (already accumulated before
  // renderTop was called) and `render` (timed from t0). Under 0.2% of a paint,
  // not worth restructuring for; the [i] line exists to eliminate exactly this
  // kind of unattributed gap, so it should be named rather than left quiet.
  if (homeOnGlass()) {
    (void)refreshBatteryOnHome();
    // Also resets the poll's own cadence timer, so a Home paint counts as a read
    // for that purpose too -- see gLastBatteryPollMs's own comment for why a boot
    // that skipped this would have the periodic poll immediately re-read what the
    // paint just read.
    //
    // ONLY WHEN HOME IS ON GLASS, and that gate is new with the ladder. The stamp
    // used to be unconditional, which was harmless while the timer's only consumer
    // was itself gated on Home: a Reader paint reset a cadence nothing outside Home
    // was waiting on. The ladder's poll is not gated, so an unconditional stamp
    // means every page turn pushes the next reading out by another poll interval
    // -- and a reader turning pages faster than that starves the safety mechanism
    // on the one screen the banner is drawn on. A paint that is not Home's takes no
    // reading at all, so it must not claim one; this makes the stamp say what the
    // comment above it always said.
    //
    // #96 DID NOT WEAKEN THAT, and the reason is that this stamp and a reading are
    // the same event: refreshBatteryOnHome() two lines up feeds gBattery.update(), so
    // a Home paint really has taken the reading it is claiming. On an X4 -- where
    // bandRepaintPossible() is false for ever and Home therefore polls at the SLOW
    // cadence -- a user pressing on Home faster than that interval pushes the periodic
    // poll out indefinitely, and the ladder is fed by these paints instead, at exactly
    // the rate the presses arrive. It is the unconditional stamp that starved it,
    // never the interval.
    gLastBatteryPollMs = millis();
  }
  // EVERY PAINT, not just the first. The frame is the driver's, and the driver
  // can take it back (bindFrameToDriver says how and why). Nothing lends it
  // today, so this is a pointer comparison that always agrees -- it is here so
  // that the day something does, the paint is skipped and logged instead of
  // written through a null pointer.
  if (!bindFrameToDriver("paint")) return;
  const reader::RefreshMode mode = gRefresh.next(gApp->transition());
  const reader::Fidelity fidelity = gApp->top().fidelity();
  // `mode` is what the POLICY decided, not necessarily what the panel does: a
  // Grayscale screen runs the full three-plane sequence regardless, because that
  // sequence has no differential form. So `fidelity=gray mode=FAST` is not a
  // contradiction -- it means the cadence had a fast slot available and this
  // screen could not use it. Nothing declares Grayscale or Dithered today, so in
  // practice every paint is `fidelity=mono`.
  // WHAT THIS PAINT IS, printed AFTER it rather than before. It used to be a
  // printf plus a flush sitting between the dispatch and the render -- inside the
  // window every latency number here is trying to measure, and with a host
  // attached that flush is real milliseconds attributed to the paint. The fields
  // are unchanged, they have just moved onto the `done` line below.
  const char* const fidelityWord = fidelity == reader::Fidelity::Grayscale  ? "gray"
                                   : fidelity == reader::Fidelity::Dithered ? "dithered"
                                                                            : "mono";
  gRenderMs = gDrawMs = 0;
  gUploadMs = gWaveMs = 0;
  reader::Profile::reset();
  gPartialPaint = false;
  // Any new paint supersedes a refinement that was owed for the old frame.
  gRefineOwed = false;
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
      // THE FAST PASS, and the four-level one follows once the buttons go quiet --
      // see kRefineQuietMs. DITHERED rather than Mono for the intermediate: the
      // whole reason the reader declares Grayscale is that hard-thresholding a
      // serif face at 32px was judged worse, so the transient frame should keep
      // what anti-aliasing one waveform can carry rather than be the thing that
      // was rejected. Same cost, closer to the final image, so the upgrade is a
      // smaller visible change.
      //
      // Its one known artifact is the em dash, which combs against the 4x4 grid at
      // body size (recorded in the roadmap). On a frame that lasts ~600 ms that is
      // a fair trade; swapping this line for paintMono(mode) is the alternative if
      // it reads badly on glass.
      paintDithered(mode);
      gRefineOwed = true;
      break;
    case reader::Fidelity::Dithered: paintDithered(mode); break;
    case reader::Fidelity::Mono: paintMono(mode); break;
  }
  // WHAT THE MARKUP CLAIMED, beside what reached the page. `emph=0` says this parser
  // found nothing it understands; these say whether there was anything to find. A
  // book whose italics are `<span class="x">` with a stylesheet shows classed>0 and
  // em=0; one using an inline style shows italicStyle>0. The two are different jobs,
  // and the counts are what decides which -- rather than a guess about what Calibre
  // emits. See reader/document.h.
  //
  // Only on a Reader paint, and it is three integers off a struct: no walk, no
  // allocation, nothing on the card.
  if (gApp->top().id() == reader::ScreenId::Reader) {
    const reader::MarkupHints& h = reader::lastMarkupHints();
    logf("[markup] em=%d styled=%d italicStyle=%d classed=%d sample='%s'\n", h.emphasisTags,
         h.styledSpans, h.italicStyles, h.classedSpans, h.sampleClass);
  }
  // WHAT THE READER'S PAGE ACTUALLY CARRIES. "This word should be italic and is not"
  // has two explanations that look identical on glass, and this is what separates
  // them: `emph` is how many emphasised runs reached LAYOUT, so zero means the PARSE
  // found none -- document.cpp reads `<em>`, `<i>` and `<cite>`, and a book that
  // marks its italics with a class and a stylesheet carries none of the three. A
  // non-zero count means the spans got as far as the page and the loss is after it.
  // `ital` catches the third case: no italic face installed, where drawTextStyled
  // falls back to the roman silently and correctly.
  if (gApp->top().id() == reader::ScreenId::Reader) {
    const auto* rd = static_cast<const reader::ReaderScreen*>(&gApp->top());
    logf("[page] %d/%d lines=%u emph=%d ital=%d\n", rd->vm().page, rd->vm().pageTotal,
         (unsigned)rd->page().lines.size(), rd->pageEmphasisRuns(), (int)rd->hasItalic());
  }
  const uint32_t total = millis() - t0;
  // render = drawing all passes (4 for gray: Bw, Lsb, Msb, then Bw again for the
  // cleanup rebase; 1 for mono and for dithered). panel = everything else, which
  // is essentially BUSY waits. `render` and `draw` are now the same number --
  // drawing is all a render pass does since the rotate went away -- and both are
  // kept so the field stays comparable against the logs that measured the
  // difference. If they ever diverge again, something new got added to the pass.
  //
  // `scope` is which of the two paint paths the LAST pass took: `stack` is the
  // whole walk over a cleared frame, `top` is the top overlay alone over the
  // frame the previous paint left. It is per-pass rather than per-paint because
  // the grayscale sequence has four, and it is in the log because a stale-pixel
  // report needs to say which path drew the frame that showed it -- guessing from
  // the screen name is exactly the wrong way round.
  // `up` and `wave` are the two halves of what used to be one `panel` figure --
  // the SPI plane upload against the waveform wait; see showOnePass. They do not
  // add up to `panel` on the grayscale path, which uses its own display calls, so
  // `panel` stays as the total of everything that is not render.
  logf("[paint] done screen=%s fidelity=%s mode=%s sinceFull=%d total=%lums "
       "render=%lums (draw=%lu) panel=%lums (up=%lu wave=%lu) scope=%s%s\n",
       reader::screenName(gApp->top().id()), fidelityWord,
       mode == reader::RefreshMode::Full ? "FULL" : "FAST", gRefresh.sinceFull(),
       (unsigned long)total, (unsigned long)gRenderMs, (unsigned long)gDrawMs,
       (unsigned long)(total - gRenderMs), (unsigned long)gUploadMs, (unsigned long)gWaveMs,
       gPartialPaint ? "top" : "stack", gRefineOwed ? " refine-owed" : "");

  // WHERE THE RENDER WENT, per PRIMITIVE. `render=` says a reader menu costs 266 ms
  // and Contents 62; this says which primitive spent it, and because the primitives
  // are shared the answer is about every screen that draws one rather than about
  // this screen. Microseconds, because the interesting slots are single-digit
  // milliseconds and rounding them to 0 would hide exactly the ones that are cheap.
  //
  // `other` is the remainder -- layout arithmetic, measuring, wrapping, eliding --
  // and it is a real slot rather than a rounding error: on the desktop the reader
  // menu spends 22% there against the actions panel's 2%, which is a question about
  // this screen's caption wrap and not about any primitive.
  {
    char line[224];
    int at = snprintf(line, sizeof(line), "[render] total=%luus", (unsigned long)(gRenderMs * 1000u));
    uint32_t accounted = 0;
    for (int i = 0; i < reader::kPhaseCount && at > 0 && at < (int)sizeof(line); ++i) {
      const auto ph = static_cast<reader::Phase>(i);
      const uint32_t us = reader::Profile::micros(ph);
      if (us == 0) continue;
      accounted += us;
      at += snprintf(line + at, sizeof(line) - (size_t)at, " %s=%lu/%lu",
                     reader::Profile::name(ph), (unsigned long)us,
                     (unsigned long)reader::Profile::calls(ph));
    }
    const uint32_t totalUs = gRenderMs * 1000u;
    logf("%s other=%lu\n", line,
         (unsigned long)(totalUs > accounted ? totalUs - accounted : 0));
  }
}

// The four-level upgrade of a frame already on glass. Runs from loop() once the
// buttons have been quiet, never from a dispatch -- see kRefineQuietMs.
static void refineNow() {
  if (!bindFrameToDriver("refine")) return;
  gRefineOwed = false;

  // THE PAGE COUNT IS FILLED IN HERE, before the repaint that was going to happen
  // anyway. A chapter opens with its total unknown -- counting it is one decode,
  // ~545 ms, and paying that before the first page appeared made a crossing twice an
  // ordinary turn -- so the footer shows an em dash until this runs. Folding it into
  // the refinement means the number arrives with the four-level upgrade and costs no
  // extra waveform.
  //
  // Then the pending check AGAIN: the count is cheap and needed, the refinement is
  // expensive and cosmetic, so a press that arrives during the count cancels the
  // refinement rather than queueing behind it.
  if (gApp->top().id() == reader::ScreenId::Reader) {
    auto* rd = static_cast<reader::ReaderScreen*>(&gApp->top());
    if (rd->indexPending()) {
      const uint32_t t = millis();
      // THE SAME PREDICATE AS THE DEFERRED SITE, and it has to be: this is the
      // second of the two places a chapter gets counted, and leaving one of them
      // uninterruptible would mean the 2-3.6 s block came back on whichever path
      // the reader happened to take. It was missed once already -- the fix went in
      // at the deferred site alone and this one kept the old blocking call.
      const bool done = rd->completeIndex([](void*) { return rawSamplesPending() != 0; }, nullptr);
      mark("index-completed");
      logf("[index] %s pages=%d in %lums\n", done ? "counted" : "abandoned",
           rd->pageCount(), (unsigned long)(millis() - t));
      logFlush();
      if (rawSamplesPending() != 0) {
        // Someone pressed while it counted. The page on glass is still correct -- the
        // count does not change it -- so leave the refinement owed and get out of the
        // way.
        gRefineOwed = true;
        return;
      }
    }
  }
  logf("[refine] screen=%s -> four levels\n", reader::screenName(gApp->top().id()));
  logFlush();
  gRenderMs = gDrawMs = 0;
  gPartialPaint = false;
  const uint32_t t0 = millis();
  SpiBusGuard bus;  // the same whole-sequence guard renderTop takes, same reason
  paintGray();
  const uint32_t total = millis() - t0;
  mark("refine-complete");
  // Reported separately from [paint] so the two costs stay distinguishable: a page
  // turn is the fast paint, and this is what the page settles into afterwards.
  logf("[refine] done total=%lums render=%lums panel=%lums\n",
       (unsigned long)total, (unsigned long)gRenderMs,
       (unsigned long)(total - gRenderMs));
  logFlush();
}

// ---------------------------------------------------------------------------
// HOLD TO WAKE
//
// design/Sleep.dc.html's badge says `ASLEEP - HOLD POWER TO WAKE`, and this
// function is the whole of what makes that claim true. THE CHIP CANNOT MAKE IT:
// PowerManager::armPowerButtonWakeup arms a LEVEL-triggered source
// (esp_deep_sleep_enable_gpio_wakeup on the C3, ext1 on Xtensa), so the SoC
// resumes the instant the power line reaches its active level and there is no
// dwell anywhere on that path -- nor any way to ask for one. The only place a
// hold can be required is AFTER the wake, by refusing one that was not held.
//
// So a refused wake is a real boot that goes straight back to sleep, and the
// entire cost of the feature is decided by WHERE THIS IS CALLED: before
// display.begin(), which is the earliest anything can reach the panel. E-ink
// holds its last image with no power, so the glass is still showing the sleep
// screen that named the hold -- a refusal repaints nothing, spends no waveform
// and is invisible. Move this one line later, past the panel bring-up, and a
// brush against the button in a bag costs a flash and several seconds.
//
// IT NEEDS THE BOARD PROFILE, which is why it is not first: the pin and its
// polarity come from BoardConfig::ACTIVE, and reading them before
// detectAndSelectBoard() would read the compile-time default. Both Xteink
// profiles happen to agree on GPIO 3 active-LOW -- a coincidence, not a design,
// and this file already records one object built on that coincidence
// (BatteryMonitor's constructor captures the ADC pins before the probe runs).
//
// THE ESCAPE HATCH IS A CONSTANT AND HAS TO BE. Every other tunable on this
// device is a row in /.reader/settings.json, and that file is on the card, which
// is mounted hundreds of lines below here -- a gate that waited for it would
// have already paid the panel bring-up it exists to avoid. kWakeHoldMs = 0
// disables the gate outright and restores wake-on-press exactly.
namespace {

// The dwell a wake must survive. Measured against millis(), whose zero is the
// RTOS timer starting -- which is AFTER the bootloader, so the button really
// went down some tens of milliseconds before t=0 and the hold this asks for is
// slightly longer than the number says. Wrong in the conservative direction, and
// `at=` on the refusal line below is what makes the real figure readable off a
// device rather than guessed at here.
constexpr uint32_t kWakeHoldMs = 600;

// SURVIVES A DEEP-SLEEP CYCLE, which is exactly the property that makes counting
// refusals free: a refused wake sleeps again, so the next wake -- refused or
// accepted -- still sees this. Deliberately NOT NVS: a refusal must not cost a
// flash write, or the gate would put wear on the part every time the device is
// jostled. The price is the one CLAUDE.md already records for .rtc.data --
// ESP_RST_USB re-initialises it, so plugging in to read the count is what erases
// it. Acceptable here because this is a curiosity, where the diagnostic record
// it warns about was a decision.
RTC_DATA_ATTR uint16_t gRefusedWakes = 0;

}  // namespace

// Returns only when the wake is accepted. A refusal re-arms the sleep flag this
// boot consumed, powers the rails back down and does not return.
static void requireHeldPowerButtonOrSleepAgain(bool fromSleep, esp_reset_reason_t rst) {
  if (kWakeHoldMs == 0) return;

  // ONLY A RESUME IS GATED, AND ONLY ONE THE BUTTON COULD HAVE CAUSED. `fromSleep`
  // alone is not enough, and the gap between the two is the one that would look
  // like a brick.
  //
  // The slept flag lives in NVS and survives ANY reset, so a chip that was asleep
  // and is then reset by something that is not the power button -- a host
  // attaching (ESP_RST_USB), esptool, an esp_restart, a panic -- still reports
  // fromSleep with no finger anywhere near the device. Gating on that alone puts
  // the device straight back to sleep after a flash, with a stale image on the
  // glass and no line on a serial log that is about to be cut: "I flashed it and
  // it is dead". CLAUDE.md records ESP_RST_USB turning a wake into a cold boot for
  // exactly this reason, in the other direction.
  //
  // The two reset reasons a power-button resume produces, both measured on an X3
  // and both recorded in session.h:
  //
  //   ESP_RST_DEEPSLEEP -- USB attached, so the chip really deep-slept
  //   ESP_RST_POWERON   -- on battery the sleep powers the chip down entirely,
  //                        which is indistinguishable from a first-ever boot
  //
  // A first-ever POWERON carries no slept flag, so `fromSleep` is what excludes
  // it and neither test is redundant.
  const bool buttonCouldHaveWokenUs =
      rst == ESP_RST_DEEPSLEEP || rst == ESP_RST_POWERON;
  if (!fromSleep || !buttonCouldHaveWokenUs) return;

  const int8_t pin = BoardConfig::ACTIVE.input.power;
  // No power pin means no hold to require, and refusing every wake on a board
  // that cannot answer the question is the one outcome worse than an accidental
  // wake. PowerManager spells the same test the same way.
  if (pin < 0) return;
  const bool activeHigh = BoardConfig::ACTIVE.input.powerActiveHigh;
  const int pressedLevel = activeHigh ? HIGH : LOW;
  // The same pull PowerManager::armPowerButtonWakeup held the line with, so this
  // reads the pin as it was armed rather than reading a float.
  pinMode(pin, activeHigh ? INPUT_PULLDOWN : INPUT_PULLUP);

  // THE PRESS BEGAN AT t=0, near enough: it is what woke the chip. So the dwell
  // needs no start time of its own -- "still down at millis() >= kWakeHoldMs" is
  // the whole test, and the loop exits the moment the button comes up.
  //
  // ONE ASSUMPTION IT RESTS ON, worth checking on glass rather than trusting:
  // that boot reaches here before the threshold. Serial's cap is 400 ms and the
  // detect passes ~66 ms, so the worst configuration (plugged into a charger with
  // no terminal open) samples first at ~470 ms of the 600. Unplugged -- which is
  // how this device lives -- it is ~215 ms. If boot ever got slower than the
  // dwell, a genuine hold released before the first sample would read as a tap;
  // `at=` on the line below is there to make that visible instead of mysterious.
  const uint32_t firstSampleMs = millis();
  bool held = digitalRead(pin) == pressedLevel;
  while (held && millis() < kWakeHoldMs) {
    delay(10);
    held = digitalRead(pin) == pressedLevel;
  }

  if (held) {
    // A REFUSAL IS OTHERWISE INVISIBLE BY DESIGN -- it paints nothing and the log
    // buffer dies with the RAM -- so the count is reported by the wake that
    // finally succeeds. Without this, a gate refusing everything and a gate never
    // being reached read identically: silence.
    if (gRefusedWakes > 0) {
      logf("[wake] held %lums, accepted; %u earlier wake(s) refused since the last "
           "accepted one\n",
           (unsigned long)millis(), (unsigned)gRefusedWakes);
      gRefusedWakes = 0;
    }
    return;
  }

  const uint32_t releasedMs = millis();
  if (gRefusedWakes != 0xFFFF) ++gRefusedWakes;
  logf("[wake] refused: power released by %lums, needs %lums (first sample at=%lums, "
       "refused=%u). Sleeping again; nothing was painted\n",
       (unsigned long)releasedMs, (unsigned long)kWakeHoldMs,
       (unsigned long)firstSampleMs, (unsigned)gRefusedWakes);

  // GIVE THE FLAG BACK. takeSleptFlag() consumed it on the way in -- reading it
  // clears it, and one flag buys exactly one resume -- and this wake did not
  // spend it, because the device is going straight back to the state that set it.
  // Without this the NEXT wake, the real one, reads as a cold start: the session
  // record is declined and the reader loses the page they were on, which is the
  // failure session.h exists to prevent and would be blamed on the restore.
  markSleeping();
  logFlush();

  // NO display.deepSleep() HERE, and its absence is deliberate rather than an
  // omission: begin() has not run, so there is no initialised driver to ask, and
  // the controller was already put into DSLP by the sleep this is returning to.
  // Cutting the rails is what actually holds the current down, and it also undoes
  // the one thing that has touched the panel since -- detectAndSelectBoard's bus
  // probe, which releases the rail hold to issue its reset pulse.
  freeink::PowerManager::powerDownRailsForSleep();
  // Opens with waitForPowerButtonRelease(), which returns at once: the only way
  // to reach here is having read the button as up.
  freeink::PowerManager::deepSleepUntilPowerButton();
}

// The CHARGE half of `CHARGE · HOLD POWER TO WAKE`, made true after the wake --
// because the SoC cannot make it true before one. The wake source is the power button
// and there is no charge-detect anywhere on that path, so the board's promise is
// enforced exactly as the HOLD half is: by refusing a resume that does not satisfy it.
// (The badge used to say a bare `CHARGE TO WAKE`, which promised a charge-detect wake
// this hardware does not have and omitted the hold this file's own gate requires
// FIRST. Reported from an X3.)
//
// Returns only when the resume is accepted. A refusal re-arms both flags, powers the
// rails back down and does not return.
//
// AFTER THE HOLD GATE, because that is the cheaper refusal and already stands: a
// brush against the button in a bag should be refused for the HOLD reason without
// spending an I2C transaction.
//
// AFTER detectAndSelectBoard(), because it needs the profile -- and safely so:
// readStatus() tests BoardConfig::ACTIVE.batteryGauge.gaugeAddr LIVE rather than
// from BatteryMonitor's cached members, which is what makes the file-scope static
// (constructed before setup() runs, from the compile-time default) correct here.
//
// AND STILL BEFORE display.begin(), which is the whole cost of the feature: e-ink
// holds its last image, so the glass is still showing the BATTERY EMPTY screen the
// shutdown painted. A refusal repaints nothing and spends no waveform. One line
// later, past the panel bring-up, and every brush against the button on a flat
// device costs a flash.
static void requireChargeOrSleepAgain() {
  if (!takeCriticalShutdownFlag()) return;

  const reader::BatteryReading r = readBattery();
  const int pct = r.percentKnown ? r.percent : -1;

  // A READING THAT DID NOT ANSWER LETS THE DEVICE BOOT. The alternative is a brick:
  // a gauge that has failed would refuse every wake for ever, and the ladder in
  // loop() will shut the device down again ten seconds later if the pack really is
  // flat. Fail open here, fail safe there.
  if (pct < 0 || pct >= reader::BatteryTracker::kResumePercent) {
    logf("[boot] battery pct=%d critShut=1 -> RESUME (needs %d)\n", pct,
         reader::BatteryTracker::kResumePercent);
    logFlush();
    return;
  }

  logf("[boot] battery pct=%d critShut=1 -> refused, needs %d. Sleeping again; nothing "
       "was painted\n",
       pct, reader::BatteryTracker::kResumePercent);

  // GIVE BOTH FLAGS BACK. takeCriticalShutdownFlag() consumed one on the way in and
  // setup() consumed `slept` a few lines above; this wake spent neither, because the
  // device is going straight back to the state that set them. Without the `slept`
  // half the NEXT wake -- the real one, once charged -- reads as a cold start and the
  // reader loses the page they were on, which would be blamed on the restore.
  markCriticalShutdown();
  markSleeping();
  logFlush();

  // NO display.deepSleep(): begin() has not run, so there is no initialised driver
  // to ask, and the controller was put into DSLP by the shutdown this is returning
  // to. Cutting the rails is what holds the current down. Identical to the hold
  // gate's refusal path, and for the same reasons.
  freeink::PowerManager::powerDownRailsForSleep();
  freeink::PowerManager::deepSleepUntilPowerButton();
}

void setup() {
  // A BIGGER TX RING, BEFORE begin() ALLOCATES IT. HWCDC::write posts what fits
  // the ring without blocking and then blocks for the remainder until the host
  // takes it, so the ring size is exactly how much log a busy stretch can emit
  // before the cable starts costing the device time. 4 KB is ~40 of this
  // firmware's lines against a default that is a fraction of that, and it is 4 KB
  // of 320. It does not make serial free -- see logf() and the `ser=` field --
  // it makes the common case not block at all.
  Serial.setTxBufferSize(4096);
  Serial.begin(115200);
  // WAIT FOR THE HOST, NOT FOR A CONSTANT. This was `delay(2500)` -- an
  // unconditional 2.5 seconds on every boot so USB CDC could enumerate before the
  // first print, which is about 60% of the time before the panel is able to show
  // anything at all. On a device that spends its life unplugged that is 2.5
  // seconds of nothing, paid so that a serial log nobody is reading is complete.
  //
  // ARDUINO_USB_CDC_ON_BOOT=1 (platformio.ini), so `Serial` is the USB
  // Serial/JTAG CDC and it has two things worth asking:
  //
  //   Serial (operator bool)   -- the CDC is up and a host has opened it
  //   isPlugged()              -- the peripheral sees a host at all. IDF's
  //                               timer-based check, not the SOF ISR, which the
  //                               core's own comment says breaks esptool uploads.
  //
  // So: leave the moment the host is actually there, and give up early when
  // nothing is. Unplugged costs the grace window instead of the full cap; plugged
  // costs however long enumeration really takes. The cap is unchanged, so the
  // worst case is exactly the old behaviour.
  //
  // What this trades away: the first few lines, on a host that is plugged in but
  // slower to report than the grace window. `mark()` flushes every stage line, so
  // the loss would be bounded and visible as a missing early stage rather than as
  // silence -- and the whole log is reproducible by resetting with the port
  // already open.
  {
    // THE CAP IS SHORT ON PURPOSE, and the first version of this got it wrong.
    //
    // Keeping the old 2500 as the ceiling looked conservative and was not:
    // `Serial` is only true once a host has OPENED the port, so a device plugged
    // into a charger -- or into a computer with no terminal running -- has
    // isPlugged() true and Serial false, and waited the entire 2500 ms. That is
    // the overnight-charging case and the wake-with-cable case, which is to say
    // most of them. It was no better than the constant it replaced and it made a
    // wake feel slow.
    //
    // So the wait is now bounded by what it is actually for: letting CDC come up
    // when somebody is already watching. A terminal already open answers in ~0 ms
    // (the common dev case, since you reset while watching). Anything else pays
    // the cap once and gets on with booting. A terminal that attaches LATER loses
    // the first few lines, which is recoverable by resetting with the port open.
    constexpr uint32_t kSerialCapMs = 400;
    constexpr uint32_t kSerialGraceMs = 150;  // for the peripheral to notice a host at all
    const uint32_t t0 = millis();
    while (millis() - t0 < kSerialCapMs) {
      if (Serial) break;  // a host has the port open; nothing left to wait for
      if (millis() - t0 >= kSerialGraceMs && !HWCDC::isPlugged()) break;  // nobody there
      delay(10);
    }
    gSerialWaitMs = millis() - t0;
  }
  mark("serial-up");
  // Reported because it is the one boot cost that varies with something outside
  // the firmware, and a slow boot with a big number here is a USB question rather
  // than a firmware one.
  logf("[boot] waited %lums for USB CDC (cap %lu, plugged=%d, open=%d)\n",
       (unsigned long)gSerialWaitMs, 400ul, (int)HWCDC::isPlugged(),
       Serial ? 1 : 0);
  logFlush();

  detectAndSelectBoard();

  const esp_sleep_wakeup_cause_t wake = esp_sleep_get_wakeup_cause();
  // TAKEN HERE, EARLY, AND ONCE: reading it clears it, so this is the only place
  // that may ask. On battery the sleep powers the chip down, so `wake` is
  // UNDEFINED on a resume and this flag is the only thing that knows otherwise.
  const bool sleptDeliberately = takeSleptFlag();
  const bool fromSleep = (wake != ESP_SLEEP_WAKEUP_UNDEFINED) || sleptDeliberately;
  // THE RESET REASON, not just the sleep cause, because `wake cause=0` has two
  // completely different meanings and this is what tells them apart:
  //
  //   ESP_RST_DEEPSLEEP  -- a real resume. If wake cause is still 0 here, the
  //                         sleep API and the reset disagree, which is a bug.
  //   ESP_RST_USB        -- the USB Serial/JTAG peripheral reset the chip. A host
  //                         attaching does this, and after deep sleep the port has
  //                         to be re-enumerated and reopened -- so CAPTURING a
  //                         wake over USB CDC can destroy the wake. Three
  //                         consecutive attempts to log a resume came back as this.
  //   ESP_RST_POWERON /
  //   ESP_RST_SW etc.    -- the device never slept, or something restarted it.
  //
  // Without this line the three are indistinguishable, and "the session restore
  // stopped working" and "the logger reset the device" look identical from the
  // serial output.
  const esp_reset_reason_t rst = esp_reset_reason();

  // MAY NOT RETURN. See the definition: a wake the user did not hold through is
  // refused here, before display.begin(), so it costs no waveform and nothing on
  // the glass changes.
  requireHeldPowerButtonOrSleepAgain(fromSleep, rst);

  // MAY NOT RETURN. See the definition: a resume on a pack that is still flat is
  // refused here, BEFORE display.begin(), so it costs no waveform and nothing on
  // the glass changes -- e-ink holds its last image, which is still the BATTERY
  // EMPTY screen the shutdown painted. This is requireHeldPowerButtonOrSleepAgain's
  // argument verbatim, one line later.
  requireChargeOrSleepAgain();

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
  const char* rstName = rst == ESP_RST_DEEPSLEEP ? "DEEPSLEEP (a real resume)"
                        : rst == ESP_RST_USB     ? "USB (a host attaching reset the chip)"
                        : rst == ESP_RST_POWERON ? "POWERON"
                        : rst == ESP_RST_SW      ? "SW (esp_restart)"
                        : rst == ESP_RST_PANIC   ? "PANIC"
                        : rst == ESP_RST_BROWNOUT ? "BROWNOUT"
                        : rst == ESP_RST_EXT     ? "EXT (reset pin)"
                                                 : "other";
  logf("[boot] reset reason=%d %s; sleep wake cause=%d; slept-flag=%d -> %s\n",
       (int)rst, rstName, (int)wake, sleptDeliberately ? 1 : 0,
       fromSleep ? "RESUME" : "cold start");
  logFlush();
  // Before anything overwrites it: this prints the PREVIOUS cycle and starts a
  // new record, so a fault that only happens unplugged is readable next time the
  // device is plugged in.
  reportAndResetCrumbs(rst, wake);
  logf("[boot] wake cause=%d -> %s\n", (int)wake,
       fromSleep ? "resumed from sleep (the panel holds our frame, but the "
                            "controller's baseline did not survive, so it is reseeded)"
                          : "cold boot, reseeding the controller's baseline (the "
                            "panel keeps its last image until the first paint)");
  logFlush();
  // BOTH branches let the driver seed its own baseline. On wake this used to call
  // skipInitialResync() instead, and that was wrong in a way worth recording.
  //
  // The reasoning was: e-ink holds its image with no power, so on wake the panel
  // already shows what we painted and there is nothing to clear. That is true of
  // the PANEL and false of the CONTROLLER, which is the distinction the call
  // actually turns on. skipInitialResync() sets Uc8279Driver::_oldPlaneValid
  // true, asserting DTM1 still holds the displayed frame -- but a wake is a chip
  // reset, initController() has just re-run, and DTM1's contents did not survive
  // the power cycle. displayStart() seeds DTM1 white only `if (!_oldPlaneValid)`,
  // so claiming validity SKIPPED that seed and left the GC waveform diffing the
  // new frame against garbage. Wrong old values give wrong per-pixel
  // transitions, which is what showed on the panel: a split second of noisy
  // banding before the image settled.
  //
  // The cost of doing it properly is one clean flash on wake, the same as a cold
  // boot. That is what Kindle and Kobo do on resume anyway, and a clean flash is
  // plainly better than a fast smear of noise.
  //
  // skipInitialResync() is not useless -- it is right for a caller that has
  // RESTORED the baseline first, which is what the reference firmware does on its
  // quick resume (`begin() clears the X3 controller RAM, so restore the saved
  // frame as the baseline`). Doing that here would give a flash-free wake: render
  // the restored screen, rebase the controller onto it, then refresh FAST. It
  // needs the restored screen to actually match what is on the glass, so it waits
  // until the session restore is trustworthy enough to bet a frame on.
  display.requestResync();

  logf("[info] panel %dx%d, buffer %u bytes\n", display.getDisplayWidth(),
       display.getDisplayHeight(), (unsigned)display.getBufferSize());
  logf("[info] free heap %u, largest block %u\n", (unsigned)ESP.getFreeHeap(),
       (unsigned)ESP.getMaxAllocHeap());
  logFlush();

  reader::FontSet& fonts = gFonts.emplace();
  // The ramp is the manifest's (reader/font_manifest.h) -- one list, three
  // loaders. Each role names its weight and FontSet::load checks the asset
  // against it, so a mis-wired entry there is a loud "font-load-FAILED" at boot
  // rather than a screen drawn in the wrong weight for the rest of the project.
  // The embedded array names match the Role names by the generator's convention,
  // which is what lets the manifest expand over them.
#define ENCRE_LOAD_ROLE(role, stem) \
  && fonts.load(reader::Role::role, kFont##role, kFont##role##Size)
  const bool fontsOk = true READER_FONT_RAMP(ENCRE_LOAD_ROLE);
#undef ENCRE_LOAD_ROLE
  if (!fontsOk || !fonts.ready()) {
    mark("font-load-FAILED");
    return;
  }
  mark("fonts-ok");

  // --- The body face, checked once at boot -----------------------------------
  //
  // Chrome's eleven faces are pre-rendered; body text is not, because book CSS
  // asks for an unbounded set of sizes (see reader/scalablefont.h). Nothing
  // DRAWS body text yet -- that is 3B and 3C -- so this is a parse-and-rasterise
  // check and nothing more, and it is here rather than deferred for two reasons.
  //
  // It is what pays the flash. kFontBodySerif is 169,144 bytes of `.rodata` and
  // an array nothing references is an array the linker never emits, so without a
  // caller the cost of the body face would read as zero in every size report
  // right up until 3B added the first draw call and it appeared all at once.
  //
  // And it is the only thing on the desktop's side of this task that the DEVICE
  // can answer: a runtime rasteriser on a part with no FPU is the assumption 3B
  // is about to build on, and the numbers below are how it is falsified early
  // rather than late.
  //
  // IT IS ALSO THE REAL INITIALISATION NOW, not just a check. gBody is what every
  // Reader draws from, at reader::kBodyPpem -- design/Reader.dc.html's `font-size:
  // 32px` -- where this used to hard-code 29 and throw the face away. A failure
  // here therefore means no book can be opened, which is why the log line below is
  // the one that says so.
  {
    reader::ScalableFont& body = gBody;
    const uint32_t t0 = micros();
    const bool bodyOk = body.init(kFontBodySerif, kFontBodySerifSize, reader::kBodyPpem);
    const uint32_t t1 = micros();
    if (!bodyOk) {
      // Not fatal, still: the chrome screens do not read this face, so a card
      // full of books the device cannot open is better than a device that does
      // not boot. openChapter's caller logs the refusal per book.
      mark("body-face-FAILED");
    } else {
      // One glyph, rasterised, so the timing is a rasterisation and not a parse.
      const uint32_t t2 = micros();
      const std::optional<reader::Glyph> g = body.glyph('a');
      const uint32_t t3 = micros();
      const reader::ScalableFont::CacheStats cs = body.cacheStats();
      // THE ITALIC IS INITIALISED HERE TOO, and a failure is logged rather than
      // fatal: emphasis falls back to roman, which is exactly what `italic == nullptr`
      // means everywhere below. A book still opens.
      const uint32_t i0 = micros();
      const bool italOk =
          gItalic.init(kFontBodySerifItalic, kFontBodySerifItalicSize, reader::kBodyPpem);
      const uint32_t i1 = micros();
      const reader::ScalableFont::CacheStats ics = gItalic.cacheStats();
      logf("[italic] %s ppem=%d line=%d init=%uus cache=%u/%u+%u\n",
           italOk ? "ok" : "FAILED", gItalic.ppem(), gItalic.lineHeight(),
           (unsigned)(i1 - i0), (unsigned)ics.usedBytes,
           (unsigned)ics.capacityBytes, (unsigned)ics.overheadBytes);
      // THE TWO FACES MUST AGREE ON THE LINE BOX. One baseline per line, so an
      // italic with a different ascent would sit off it -- and both files are
      // unitsPerEm 1000 pinned to the same coordinates, so this is a check on the
      // ASSETS rather than on the code.
      if (italOk && gItalic.lineHeight() != body.lineHeight())
        logf("[italic] LINE BOX MISMATCH roman=%d italic=%d\n", body.lineHeight(),
             gItalic.lineHeight());
      logf(
          "[body] ppem=%d weight=%d ascent=%d descent=%d line=%d init=%uus "
          "glyph_a=%dx%d raster=%uus cache=%u/%u+%u\n",
          body.ppem(), body.weight(), body.ascent(), body.descent(), body.lineHeight(),
          (unsigned)(t1 - t0), g ? g->bitmapW : -1, g ? g->bitmapH : -1,
          (unsigned)(t3 - t2), (unsigned)cs.usedBytes, (unsigned)cs.capacityBytes,
          (unsigned)cs.overheadBytes);
      logFlush();

      // THE SWEEP BELOW IS OFF BY DEFAULT: it rasterises 95 glyphs twice at
      // ~3.79 ms each, which is ~362 ms of EVERY boot for a measurement that has
      // been taken and is in the roadmap. Same mistake as the [library] probe,
      // caught the same way -- by reading a boot log and asking what each line
      // cost.
      //
      //   PLATFORMIO_BUILD_FLAGS="-DENCRE_BODY_SWEEP=1" make firmware
      //
      // The single cold glyph above stays unconditional: it is ~8 ms, it is what
      // proves the rasteriser works on this part at all, and a boot that silently
      // stopped being able to raster body text is worth 8 ms to notice.
#if defined(ENCRE_BODY_SWEEP) && ENCRE_BODY_SWEEP
      // THE ONE SAMPLE ABOVE IS NOT THE NUMBER 3B NEEDS, and reporting it as
      // though it were is how a plan gets built on a cold-path artifact. The
      // first rasterisation in the process pays for things that happen exactly
      // once -- the first faults into a 132 KB `.rodata` font over SPI flash,
      // the first malloc of stb's scratch bitmap, the first pass through code
      // that is not yet in the instruction cache -- and none of those recur.
      // Whether a runtime rasteriser is viable here depends on the STEADY cost,
      // which needs a population, so the sweep below rasterises one.
      //
      // Three separate questions, three measurements, because they have
      // different answers and 3B's design turns on which one dominates:
      //
      //   COLD  -- every distinct glyph of a page, each rasterised once. This is
      //            what the first page of a chapter actually costs, and the set
      //            is small: printable ASCII is 95 glyphs and an English page
      //            draws thousands of glyph INSTANCES but far fewer distinct
      //            ones. If this number is tolerable, on-demand rasterisation
      //            is viable and 3B needs no pre-render pass.
      //   WARM  -- the same set again, now that the cache has seen it. This is
      //            what page two costs, and the gap between it and COLD is the
      //            entire value of the cache. It is also where an undersized
      //            cache shows up: if the set does not fit, WARM comes back
      //            near COLD with evictions to prove why, and the fix is a
      //            bigger budget rather than a different architecture.
      //   MEAS  -- a line of text measured, not drawn. Pagination measures whole
      //            CHAPTERS to find page breaks, so if measuring rasterised, the
      //            cost would be the book's glyph count rather than a page's and
      //            nothing else in 3B would matter. advance() is documented as
      //            never rasterising; `rast=` here is that documentation checked
      //            against the device, and it must print 0.
      const char32_t kFirst = 0x20, kLast = 0x7E;

      body.resetCacheStats();
      int found = 0;
      const uint32_t c0 = micros();
      for (char32_t cp = kFirst; cp <= kLast; ++cp)
        if (body.glyph(cp)) ++found;
      const uint32_t c1 = micros();
      const reader::ScalableFont::CacheStats cold = body.cacheStats();

      body.resetCacheStats();
      const uint32_t w0 = micros();
      for (char32_t cp = kFirst; cp <= kLast; ++cp) (void)body.glyph(cp);
      const uint32_t w1 = micros();
      const reader::ScalableFont::CacheStats warm = body.cacheStats();

      // A real sentence rather than a synthetic run: measure() walks pairs for
      // kerning, so a string of one repeated character would measure a path the
      // reader never takes.
      static const char kLine[] =
          "Miss Brooke had that kind of beauty which seems to be thrown into "
          "relief by poor dress.";
      body.resetCacheStats();
      const uint32_t m0 = micros();
      const int lineW = body.measure(kLine);
      const uint32_t m1 = micros();
      const reader::ScalableFont::CacheStats meas = body.cacheStats();

      const uint32_t coldUs = c1 - c0, warmUs = w1 - w0;
      logf(
          "[body] sweep glyphs=%d cold=%luus (%luus/glyph) warm=%luus "
          "(%luus/glyph) speedup=%lux\n",
          found, (unsigned long)coldUs,
          (unsigned long)(found ? coldUs / (uint32_t)found : 0),
          (unsigned long)warmUs,
          (unsigned long)(found ? warmUs / (uint32_t)found : 0),
          (unsigned long)(warmUs ? coldUs / warmUs : 0));
      logf(
          "[body] cache after sweep: %u/%u bytes, %d/%d entries, "
          "cold hit/miss=%lu/%lu warm hit/miss=%lu/%lu evict=%lu wrap=%lu "
          "bypass=%lu\n",
          (unsigned)warm.usedBytes, (unsigned)warm.capacityBytes, warm.entries,
          warm.capacityEntries, cold.hits, cold.misses, warm.hits, warm.misses,
          warm.evictions, warm.wraps, warm.bypasses);
      logf("[body] measure %u chars = %dpx in %luus, rast=%lu (MUST be 0)\n",
           (unsigned)(sizeof(kLine) - 1), lineW, (unsigned long)(m1 - m0),
           meas.rasterisations);
      logFlush();
#endif  // ENCRE_BODY_SWEEP
      mark("body-face-ok");
    }
  }

  const int panelW = display.getDisplayWidth();
  const int panelH = display.getDisplayHeight();

  // ONE 1-bit frame, AND WE NO LONGER ALLOCATE IT.
  //
  // It used to be two of ours -- a portrait one to draw into and a landscape one
  // to rotate the finished frame into -- and the rotate is gone, so the portrait
  // buffer went with it: the frame is drawn against portrait coordinates and
  // stored landscape. That gave back 52272 bytes on the X3 (48000 on the X4).
  //
  // But there were still TWO frames live, because only one of them was ours.
  // FreeInkDisplay::begin() has already allocated its own frame of exactly the
  // same size -- unconditionally, at display bring-up, above -- and
  // setFramebuffer() memcpy'd ours into it on every paint. So the second 52272
  // bytes is given back here by drawing straight into that buffer instead, and
  // the copy leaves the paint path with it. Nothing is allocated below; the
  // frame is a VIEW (see bindFrameToDriver).
  //
  // Three frames was never possible on this hardware and is worth remembering,
  // because it is the same wall: measured free heap is ~233 KB but the largest
  // contiguous block is ~115 KB, so a third 52 KB allocation found no block big
  // enough even though the total would have covered it. std::vector then throws,
  // and the firmware is built -fno-exceptions, so that is an abort() and a boot
  // loop with no diagnostic. Re-rendering the B/W pass for the grayscale cleanup
  // rebase, rather than retaining a frame for it, is what avoids needing one.
  //
  // THE LARGEST-BLOCK CHECK IS GONE WITH THE ALLOCATION, and that is not a
  // weakening: it existed because a vector that cannot allocate aborts with no
  // diagnostic, and there is no vector any more. The driver's own allocation can
  // still fail, and it reports that as a null getFrameBuffer(), which
  // bindFrameToDriver turns into a logged refusal rather than a crash. The
  // headroom itself is still worth logging -- Phase 3's page cache is what will
  // want it.
  const unsigned frameBytes = display.getBufferSize();
  logf("[info] frame %u bytes x1, the driver's own; free heap %u, largest block %u\n",
       frameBytes, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
  logFlush();
  if (!bindFrameToDriver("boot")) {
    mark("frame-bind-FAILED");
    return;
  }
  // The render profiler's clock. core/ has none and must not acquire one, so the
  // owner supplies it -- see reader/profile.h. Two reads per PRIMITIVE CALL and
  // never per pixel, so ~100 calls a frame against a 100-300 ms render: it is left
  // on rather than gated, exactly as the stage marks and the interaction line are.
  reader::Profile::install([]() -> uint32_t { return static_cast<uint32_t>(micros()); });
  mark("frame-bound");

  // Which grayscale path the selected driver actually offers. Logged because
  // the sequence below is only correct for a driver that does NOT combine the
  // base frame into the gray waveform (X3/X4 do not; only Paper Mono does).
  logf("[info] gray caps: combinesBase=%d busyStaging=%d strip=%d\n",
       display.combinesGrayscaleBase(), display.supportsBusyGrayscaleStaging(),
       display.supportsStripGrayscale());
  logFlush();

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
    logf("[sd] contract self-test: %d failed assertion(s)\n", fsFailures);
    logFlush();
  }

  // Settings on either branch. With no card every FileSystem method fails, so
  // this reports "no file" and applies the compiled-in defaults -- the settings
  // must not depend on the card for the device to behave.
  loadAndApplySettings();

  // THE SAVED NETWORKS, from NVS rather than the card -- see wifi.h. It does
  // not depend on the mount, so it is safe here and would be safe earlier;
  // it sits after the settings because that is where the factory's other
  // priming is, and a factory primed in two places is a factory primed in
  // neither on the path somebody forgets.
  loadWifi();

  // AND THE BODY FACE, WHICH loadAndApplySettings CANNOT REACH.
  //
  // The face was inited ~230 lines above with the CONSTANT kBodyPpem, because it
  // has to exist before anything can measure with it and the card had not been
  // read yet. So a persisted `bodyPpem` was applied to the SETTINGS and not to the
  // FACE: margins, lead and justify survived a reboot because readerMetrics is
  // computed below this point, and Size did not -- the page came back at 15 PT
  // while both screens said 22. That is the "a setting that appears not to have
  // taken" failure this whole feature is careful about, arriving at boot instead of
  // at a press.
  //
  // Guarded on the face DISAGREEING rather than on the setting being non-default,
  // so a card that happens to hold the default costs nothing: a re-init flushes
  // both glyph caches, and the next page would re-rasterise its alphabet at
  // ~3,794 us a glyph for no reason.
  //
  // Here rather than earlier because this is the first point gSettings is real, and
  // before any book can be opened -- the session restore below is what would
  // otherwise paginate a chapter with the wrong face.
  if (gSettings.bodyPpem != gBody.ppem()) applyBodyPpem();

  // Write the settings file if the card has none, then point both card-liveness
  // probes at it. After loadAndApplySettings() on purpose: gSettings holds what
  // will actually be in force by now, so a fresh card gets a file that matches
  // the running device rather than one written before the load had a say.
  if (storage) {
    ensureBooksDir("boot");
    armCardProbes("boot");
  }

  // HOW MANY LIBRARY ROWS FIT ON THIS PANEL, asked once and carried into every
  // Library the factory builds. The theme owns the box model (panel height minus
  // the header band minus the hint bar, over the row pitch) and the shell is the
  // only side that knows the geometry, so this is the handshake between them.
  //
  // BEFORE THE FIRST LIBRARY IS BUILT, which means before this point can be
  // reached by any press: skip it and the window has no height, so the list
  // renders empty. That is the screen behaving correctly -- it must not draw a row
  // it was not given -- and it would look exactly like an empty /books, which is
  // the failure that is hard to spot.
  // ASK THE FRAME, not the driver. display.getDisplayWidth()/Height() are the
  // panel's NATIVE LANDSCAPE 792x528; the logical canvas every screen draws
  // against is portrait 528x792, which is why gFrame is constructed with the two
  // swapped. Passing panelH here handed libraryVisibleRows 528 as the height and
  // it returned 4 rows where the canvas fits 7 -- three books a screen, silently,
  // and a number nothing else in the firmware could contradict.
  //
  // gFrame->height() cannot drift from what is actually drawn into, which is the
  // whole point: this value decides scrolling, and a screen that scrolls against
  // a height it does not have is a defect no golden can see (the goldens render
  // at an explicit geometry and never consult the driver).
  const int logicalW = gFrame->width(), logicalH = gFrame->height();
  // The logical canvas and the panel's native geometry MUST be a transpose of
  // each other, because the rotation is exactly what relates them. Asserted
  // rather than trusted: the bug this replaced (4 library rows instead of 7) was
  // a silent swap that every one of 391 unit tests and every golden passed
  // through, because the goldens render at an explicit geometry and never ask the
  // driver. A log line was the only artefact in the system that knew, and reading
  // it was luck. This fails loudly instead.
  if (logicalW != panelH || logicalH != panelW) {
    logf("[fatal] logical canvas %dx%d is not the transpose of the panel's "
         "native %dx%d -- the rotation and the geometry disagree\n",
         logicalW, logicalH, panelW, panelH);
    logFlush();
    mark("frame-geometry-MISMATCH");
    return;
  }
  const int libraryRows = gTheme.libraryVisibleRows(logicalH, fonts);
  // How many contents rows fit, told ONCE -- it is a property of the panel and the type
  // ramp, exactly as the Library's is, not something to recompute per press. Setting it
  // inside the per-press priming was how it came to be missed on a path that ran too
  // late, and a list told nothing renders empty.
  gFactory.setContentsVisibleRows(gTheme.contentsVisibleRows(logicalH, fonts));
  gFactory.setLibraryVisibleRows(libraryRows);
  gFactory.setWifiPickerVisibleRows(gTheme.libraryVisibleRows(logicalH, fonts));
  logf("[boot] Library fits %d rows on this %dx%d logical canvas "
       "(panel is %dx%d native)\n",
       libraryRows, logicalW, logicalH, panelW, panelH);

  // Settings, and it takes THREE numbers rather than one because its items are not
  // all the same height -- the theme owns the box model, the screen owns the item
  // table and does the counting. Same failure mode as the Library's if it is
  // skipped: the screen correctly renders nothing, because a screen must not draw
  // a row it was not given room for.
  //
  // The LOGICAL height, not the panel's. libraryVisibleRows was handed the native
  // landscape height once and the Library showed four rows instead of seven; the
  // assertion above now makes that impossible to repeat silently, but the same
  // variable is the right one here for the same reason.
  int settingsListH = 0, settingsRowH = 0, settingsHeaderH = 0;
  gTheme.settingsMetrics(logicalH, fonts, settingsListH, settingsRowH, settingsHeaderH);
  gFactory.setSettingsMetrics(settingsListH, settingsRowH, settingsHeaderH);
  gFactory.setSettingsSink(&gSettingsSink);
  gFactory.setSettings(gSettings);
  logf("[boot] Settings list %dpx: rows %dpx, section headers %dpx\n", settingsListH,
       settingsRowH, settingsHeaderH);

  // Reader, the third screen whose box model the theme owns. The LOGICAL geometry,
  // for the reason spelled out above: libraryVisibleRows was handed the native
  // landscape height once and showed four rows instead of seven.
  reader::PageMetrics readerMetrics;
  gTheme.readerMetrics(logicalW, logicalH, fonts, gBody, gSettings, readerMetrics);
  // The WRAP measures emphasis with this, so it has to be in the metrics the factory
  // hands the reader -- not only in the draw.
  readerMetrics.italic = &gItalic;
  gFactory.setReaderMetrics(readerMetrics);
  gFactory.setReaderBody(&gBody);
  gFactory.setReaderItalic(&gItalic);
  logf("[boot] Reader column %dx%d at (%d,%d), body ppem %d\n", readerMetrics.columnW,
       readerMetrics.columnH, readerMetrics.columnLeft, readerMetrics.columnTop,
       gBody.ppem());
  logFlush();

  // The shell's own view of storage, which is what roots the app and what the
  // presence poll in loop() watches for a usable -> unusable edge.
  gStorageUsable = storage;
  // Kept across a sleep: on the wake fault the mount SUCCEEDED and the poll then
  // said the card was gone, so the record has to carry both answers or it cannot
  // tell "never mounted" from "mounted, then lost".
  gCrumbs.mountOk = storage ? 1 : 0;
  saveCrumbs();
  if (storage) {
    // WAKING, PAINTED BEFORE ANY OF THE EXPENSIVE BOOT WORK. A wake is the slowest
    // path this device has and the panel is still showing the sleep screen, which
    // says the device is ASLEEP -- so until the first real paint the glass is
    // actively wrong rather than merely stale.
    //
    // HERE AND NOT EARLIER, and the constraint is honesty rather than ordering: the
    // sleep screen names the book being read, and that comes from the card, so this
    // is the first moment a truthful one can be drawn. It is still well before the
    // costs that make a wake slow -- Home's book count, and restoring the reader.
    //
    // Unconditional on a resume rather than deadline-gated like an open, because a
    // wake is known-slow: there is no cheap case to protect.
    if (fromSleep) {
      // ONE FLASH ON A WAKE, NOT TWO, and getting there needs the driver's
      // boot-clear budget spent deliberately rather than by accident.
      //
      // Uc8279Driver::initController grants TWO forced GC refreshes after every
      // reset (_initialFullsRemaining = 2), for a consumer that paints a splash and
      // then its first real screen. A wake is a chip reset, so the budget is back --
      // and with a paint here as well as setup's, BOTH were being spent adjacently:
      // the waking line flashed, and then Home flashed. That is what was reported.
      //
      // The budget is handled AFTER this paint, in BOTH modes, and getting to
      // "both" is #94. This block used to assert a baseline before the paint when
      // there was no cover -- `if (!coverOnGlass) display.skipInitialResync();` --
      // on an argument every step of which was checkable and which was wrong twice
      // over. It read:
      //
      //   * The glass holds the SLEEP SCREEN -- e-ink keeps its image with no power.
      //     TRUE, and it is the only true step.
      //   * The CONTROLLER's DTM1 baseline does not survive; after the reset it is
      //     whatever the RAM powered up as. TRUE, and stated in the very note it
      //     contradicted (see requestResync's, ~540 lines up).
      //   * CLAUDE.md records this exact call producing "a split second of noisy
      //     banding on every wake" -- but that was a differential onto a WHOLE NEW
      //     SCREEN. With no cover the frame is the sleep screen with one line
      //     changed, so almost every pixel the garbage baseline calls unchanged
      //     really is unchanged. FALSE, AND BACKWARDS. Which pixels a refresh calls
      //     unchanged is decided by DTM1, not by the glass: with DTM1 holding
      //     power-up garbage, the set of pixels re-driven is unrelated to the set
      //     that differs, whatever the frame happens to be. Uc8279Driver.cpp says
      //     so where the bank is chosen -- BOTH banks "diff the new frame against
      //     the REAL previous frame in DTM1", and "BW_GC's WW!=KW / WK!=KK, so it
      //     clears via the true old->new transition, not a white baseline".
      //
      // AND THE SECOND ERROR IS THAT THE CALL BOUGHT NOTHING AT ALL, which is what
      // makes removing it a pure win rather than a trade. What it was for was a DU,
      // and the DU was never reachable: displayStart's
      //   useGc = (mode != Fast) || !_oldPlaneValid || _forceFullSyncNext ||
      //           _initialFullsRemaining > 0
      // is an OR, requestResync() set _forceFullSyncNext ~540 lines up, and nothing
      // refreshes the panel between there and here -- so useGc was already true and
      // the GC bank loaded either way. The ONLY effect the assertion had was to make
      // displayStart's `if (!_oldPlaneValid)` false and SKIP the DTM1 white seed. It
      // spent the one thing that makes the clear clean and got no cheaper refresh
      // for it. (The `_darkBackground` rewrite that would otherwise have covered for
      // the missing seed cannot help: setBackgroundHint() has no call site anywhere
      // in this firmware, so that flag is false for its whole life.)
      //
      // WHAT WAS ON THE GLASS, reported off an X3 after a week of use: with
      // Shows=DETAILS a wake showed noisy banding, where a cover showed the clean
      // black flash. That asymmetry was this branch and nothing else -- and it is
      // NOT the grayscale rebase at the other end of the sleep. cleanupGrayscaleBuffers
      // does leave the controller on a valid B/W baseline after a cover sleep, but a
      // wake is a chip reset: initController() re-runs and resets every one of these
      // flags, so no controller state survives a sleep in either mode.
      //
      // SO NEITHER MODE ASSERTS ANYTHING BEFORE THE PAINT. Both let displayStart see
      // !_oldPlaneValid, seed DTM1 white and take the GC -- the honest clear -- and
      // both call skipInitialResync() AFTER it, where the claim is true because we
      // have just written the frame ourselves. requestResync() goes with the branch:
      // its whole job was to force a GC at Home over a baseline we had admitted we
      // did not know, and after a real clear here we DO know it.
      //
      // WHAT THIS PAINT DRAWS OVER A COVER IS THE COVER, and that reverses what
      // this block used to say. It read "handing this paint the cover does not
      // work", on two grounds, and the DEVICE settled both against it -- a wake
      // replaced a photograph with the dithered card, which reads as the book
      // having been closed. The grounds, and what is actually true:
      //
      //   * "it would be a one-bit threshold of a four-level picture". It is, and
      //     that is the affordable rendition rather than a wrong one: renderSleep
      //     draws the cover from Plane::Bw, which IS the Msb plane (screen_sleep.h),
      //     so one pass gives the same picture at two levels for one waveform. The
      //     alternative is the full grayscale sequence -- ~2.4 s, slower than the
      //     restore this screen exists to cover for, and there is no windowed
      //     refresh on this panel to repaint the badge box alone: PanelDriver.h's
      //     displayWindow default DISCARDS the window and calls display(), and
      //     Uc8279Driver does not override it.
      //   * "fidelity() would answer Grayscale for a path that paints one pass".
      //     True and inert here: nothing on this path CONSULTS fidelity(). It is
      //     read by renderTop() for an App-owned screen and by paintSleepScreen for
      //     the sleep sequence, and this paint is neither -- it renders Plane::Bw
      //     and calls showOnePass itself, exactly as it did with no cover.
      //
      // IF THE ONE-BIT RENDITION READS BADLY ON GLASS, kWakePaintsCoverAsMono below
      // is the one-line way out. This is the half nobody has seen yet.
      //
      // WHAT IT COSTS: the wake's one allowed flash moves from Home to here, and on
      // a card with `fullOnTransition` left on, Home's own transition GC makes that
      // two. That is the honest price of a clean frame, it is the same price in both
      // modes now, and it replaces a mangled intermediate frame with a clean one.
      //
      // THE TEST IS "WOULD A COVER BE PAINTED NOW", which is the same question the
      // sleep asked, asked of the same cache and the same setting. It decides WHAT
      // is painted and no longer decides anything about the baseline, so a card that
      // changed while the device slept costs the wake its picture and cannot cost it
      // a correct refresh.
      //
      // WHAT ONLY THE PANEL CAN SAY, and it is the whole of #94's verification: that
      // a Shows=DETAILS wake now reads as one clean black flash resolving to the card
      // with WAKING on it, and not as a settling band pattern. See
      // docs/on-device-smoke-checklist.md, sleep and wake.

      // THE ONE-LINE WAY OUT FOR THE OTHER HALF, AND WHAT IT SWITCHES BETWEEN.
      //
      //   true  (shipped): a cover on the glass is REPAINTED in one bit with the
      //         waking badge over it. The picture stays, the words appear, and it
      //         costs one waveform -- the same wake cost as the no-cover case.
      //   false:           a cover on the glass is NOT REPAINTED AT ALL. The whole
      //         block below is skipped and the four-level photograph the sleep left
      //         there simply stays until the restored screen paints over it. The
      //         waking message is given up, and so is the flash: with no paint here
      //         nothing spends the boot clear budget, so Home takes the GC instead.
      //
      // FLIP IT IF THE ONE-BIT COVER READS BADLY -- and that is a real risk nobody
      // has checked, because the MSB of a Floyd-Steinberg image is a threshold
      // THROUGH a dithered picture, not a threshold of the original. The symptoms
      // to flip on: the cover coming back as coarse blotches or bands where the
      // sleep screen showed tone, a recognisable face or title going illegible, or
      // the wake reading as a visible DEGRADING of the picture rather than as the
      // same picture with words on it. Those are all "the rendition is wrong", and
      // a photograph left alone beats a photograph made worse.
      //
      // Do NOT flip it for a slow or flashy wake. The flash is the GC this paint is
      // SUPPOSED to take (see the baseline note above -- #94 is what happens without
      // it), and it is the same flash in both modes, so flipping this moves the cost
      // to Home rather than removing it.
      constexpr bool kWakePaintsCoverAsMono = true;

      // ONE CALL, AND THE POINTER IS BOTH THE DECISION AND THE PICTURE. This used
      // to ask only whether a cover WOULD be painted; it now also paints it, and
      // asking twice would be two card reads and two chances to disagree.
      //
      // Everything it reads is live by here, and it was checked rather than
      // assumed: bindFrameToDriver("boot") gave gFrame, loadAndApplySettings() set
      // gSettings.sleepShows, and gStorageUsable took `storage` -- all three
      // earlier in this same setup(), in that order. It refuses on any of them
      // being missing rather than assuming them, so a reordering degrades to
      // today's no-cover behaviour instead of misbehaving.
      reader::CoverSource* const cover = sleepCoverForPaint();
      const bool coverOnGlass = cover != nullptr;
      if (coverOnGlass && !kWakePaintsCoverAsMono) {
        // Leave the glass exactly as the sleep left it. No paint, so no baseline
        // claim either way -- Home is the next thing the panel does, over a
        // baseline nobody has asserted, which is the GC it would have taken anyway.
        logf("[power] waking paint: skipped -- a cover is on the glass and it is kept\n");
        logFlush();
        mark("waking-skipped");
      } else {
        // NOTHING ASSERTS A BASELINE BEFORE THIS PAINT, IN EITHER MODE. There used
        // to be an `if (!coverOnGlass) display.skipInitialResync();` here and it is
        // #94: with Shows=DETAILS a wake showed noisy banding where a cover showed a
        // clean black flash. See the block above for the whole mechanism.
        reader::SleepViewModel vm = sleepVmFromCard(reader::kStatusWaking);
        // WHICH SCREEN THIS IS, and it is the note's other half. COVER mode drops the
        // badge for a sleeping screen because a full-bleed cover says "asleep" by
        // itself; it cannot say "waking", so this puts the badge back -- and only the
        // badge. See SleepViewModel::waking. Without it a COVER-mode wake would paint
        // the cover with no message at all, which is the whole point of the paint.
        vm.waking = true;
        reader::SleepScreen scr(vm, cover);
        // THE SHARED-BUS INVARIANT, and it is new on this path: with a cover the
        // RENDER ITSELF reads the card, one packed row at a time, while the panel's
        // CS is in play. Same guard paintSleepScreen and renderTop take, same reason.
        SpiBusGuard bus;
        gFrame->clear(true);
        // ONE PASS, Plane::Bw, cover included -- which is the Msb plane, so this is
        // the sleep screen's own picture at two levels rather than a different image.
        scr.render(*gFrame, *gFonts, gTheme, reader::Plane::Bw);
        gFrameContentsUnknown = true;
        // FAST IS WHAT THIS ASKS FOR AND A GC IS WHAT IT GETS, IN BOTH MODES, AND
        // THAT IS NOT A FALLBACK -- IT IS THE ONLY THING THIS CALL HAS EVER DONE.
        // requestResync() ran ~540 lines up (see its own note) and nothing has
        // refreshed the panel since, so _forceFullSyncNext is still true here;
        // displayStart's useGc is that flag OR'd with three others, so the mode
        // argument cannot reach the decision. With no baseline asserted, the same
        // function also seeds DTM1 white -- so this is a clean clear from a known
        // white plane, which is what both a photograph and the card screen need.
        showOnePass(reader::RefreshMode::Fast);
        // NOW the assertion is true in both modes: the panel holds a frame we just
        // wrote, and the paint that needed the boot clear budget has spent one unit
        // of it. Zeroing the rest is what keeps a wake to ONE flash -- see the top of
        // this block. requestResync() must NOT go here: its whole job was to force a
        // GC at Home over a baseline we had admitted we did not know, and after a
        // real clear we DO know it, so it would only buy a second flash.
        display.skipInitialResync();
        // NAMES THE COVER, because "the cover did not survive the wake" has two
        // explanations that look identical on glass -- no usable cache to paint
        // from, or a cache this paint refused -- and paintSleepScreen's own line
        // makes the same distinction at the other end of the sleep.
        logf("[power] waking paint: %s\n",
             coverOnGlass ? "a cover is on the glass -- repainted in one bit, GC from a white seed"
                          : "the card screen is on the glass -- GC from a white seed");
        logFlush();
        mark("waking-painted");
      }
    }
    // The root is Home, built from the shared catalogue. Popping back to Home returns
    // this object with its focus intact -- and it is REBUILT when what it draws has
    // changed, which the line above this one used to deny outright ("nothing rebuilds
    // it"). That sentence was true when it was written and is how #43's count went
    // stale; see gHomeRebuild, whose stamp this build takes.
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
    logf("[session] cold boot: record cleared, nothing to restore\n");
    logFlush();
  } else if (storage) {
    // EVERY OUTCOME BELOW IS LOGGED, and it was not always so. This used to read
    // `if (loadSession(s) && s.screen != ScreenId::Home) { ... }` with no else at
    // all, which made the two most interesting outcomes print nothing: a
    // loadSession() that returned false, and a record that named Home. Both leave
    // the device on Home, which is also what a restore that silently failed looks
    // like, so "Settings, sleep, wake, back on Home" was indistinguishable from
    // working-as-designed in a serial log. Read these against saveWhereWeAre()'s
    // lines from the previous run to place a fault on the write side or the read
    // side.
    //
    // WHAT USED TO BE HERE was a ladder of screen names -- a branch for a record
    // naming Home (already the root, so nothing to push), a branch for one naming
    // the SD-missing screen (the card mounted, so the message is no longer true),
    // then the push. Every screen not in that ladder was handled by accident, and
    // three of them turned out to be handled wrongly. App::restore() is the same
    // decisions with no screen named: the two special cases are both "does the
    // record's root match this app's root", which it asks once.
    std::vector<reader::StackEntry> stack;
    if (!loadSession(stack)) {
      // loadSession() has already said WHICH no-record this is: no namespace, a
      // version this build does not know, or a stack it cannot decode. This line
      // is what that means from here.
      logf("[session] no usable record, so nothing to restore; staying on %s. If a "
           "'[session] stored ...' line appeared before the last sleep, the WRITE "
           "is what failed, not the restore\n",
           reader::screenName(gApp->top().id()));
      logFlush();
    } else {
      // THE READER CANNOT BE RESTORED WITHOUT ITS BOOK, and the factory is right to
      // refuse one -- falling through to the demo is how this device once woke into
      // Middlemarch. So a record naming the Reader needs the book set FIRST, from the
      // same pointer Home reads; without this, sleeping on a page woke to the Library
      // because the restore correctly stopped short of a screen that could not build.
      //
      // The position comes from the sidecar, exactly as a button press would get it:
      // the record says WHICH SCREENS, and the card says where in the book. Two
      // records, two jobs -- the session record has never known about a book.
      bool namesReader = false;
      for (const reader::StackEntry& e : stack)
        if (e.screen == reader::ScreenId::Reader) namesReader = true;
      if (namesReader && gStorageUsable) {
        reader::LastRead last;
        if (!reader::loadLastRead(gSd, last) || !gSd.exists(last.bookPath)) {
          logf("[session] the record names the Reader but no saved book is on the "
               "card; it will stop at the screen below it\n");
          logFlush();
        } else {
          uint32_t bytes = 0;
          std::unique_ptr<reader::FileHandle> h = gSd.openRead(last.bookPath);
          if (h != nullptr) bytes = h->size();
          h.reset();
          openBookAt(last.bookPath, bytes, /*push=*/false);
        }
      }
      const reader::App::RestoreReport r = gApp->restore(stack);
      if (!r.rootMatched) {
        // The record describes a different world from the one that booted -- in
        // practice a record from a session with a card, woken with none, or the
        // reverse. Restoring the no-card prompt over a working card would be
        // showing the user a message that is no longer true, and layering Home
        // over the no-card prompt would let Back walk into a library that cannot
        // be read.
        logf("[session] the record is rooted at %s and this boot is rooted at %s; "
             "nothing restored\n",
             reader::screenName(stack.front().screen),
             reader::screenName(gApp->top().id()));
        logFlush();
      } else {
        // WHERE IT LANDED is what gets logged, not what was asked for. A restore
        // that lands short is a real outcome and a common one: books deleted
        // while the device slept clamp a focus, and a screen whose parent is gone
        // stops the push. The whole stack goes on the line, so the answer to "did
        // it come back where I left it" is a string comparison rather than an
        // inference.
        const std::string landed = reader::encodeSessionStack(gApp->snapshot());
        logf("[session] restored %d of %d screen(s): %s%s\n", r.restored, r.requested,
             landed.c_str(),
             landed == reader::encodeSessionStack(stack)
                          ? ""
                          : " (not what the record named: a screen it wants no longer builds, "
                            "a folder it named is no longer on the card -- in which case its "
                            "row was dropped with it rather than applied to another "
                            "directory -- or a focused row is no longer in its list)");
        logFlush();
        mark("session-restored");
      }
    }
  } else {
    // Woke with no card. The record is left ALONE rather than cleared: it is
    // still true, and the next wake with a card in the slot can honour it.
    logf("[session] woke with no usable storage; the record is kept for next time\n");
    logFlush();
  }

  // Before the first poll, not just after each dispatch: a hold started on the
  // very first frame must be recognised too.
  syncRecognizer();
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
  gCrumbs.firstPaintMs = millis();
  mark("first-paint-complete");
  saveCrumbs();
}

// --- THE CACHED COVER: WRITING IT, AND READING IT BACK ------------------------
//
// design/SleepCover.dc.html puts the book's own cover behind the sleep screen.
// Decoding one costs seconds and, measured on the X3, up to ~81 KB of heap -- so
// it is done ONCE per book and kept at /.reader/sleep.cover, a header and two bit
// planes that reader/sleep_cover.h describes and that nothing in core/ knows how
// to reach.
//
// NOTHING ON THE DESKTOP COMPILES ANY OF THIS. The simulator has its own sink and
// its own CoverSource (sim/main.cpp), the goldens synthesise their planes from
// arithmetic, and shell/ has no harness -- so everything below is an argument
// until it is on glass.

namespace {

// HOW MUCH OF EACH PLANE IS HELD BEFORE IT GOES TO THE CARD. This is a BUS number
// rather than a memory one, and without it the write is unusably slow.
//
// The decoder hands over one destination row of BOTH planes at a time (cover.h)
// and the file holds the two planes CONTIGUOUSLY (sleep_cover.h), so the writer
// has to alternate between two regions a whole plane -- ~52 KB -- apart. SdFat
// here has exactly ONE 512-byte sector cache (FsCache holds a single
// m_buffer[512], and USE_SEPARATE_FAT_CACHE is gated on __arm__, so it is off on
// this RISC-V part) -- the same geometry the card probe's whole design rests on.
// So a 66-byte write to one region EVICTS the other region's sector: unbatched,
// 792 rows would cost ~1,600 sector write-then-read pairs on top of the ~200 the
// data itself needs.
//
// 1 KB a plane is 15 rows on the X3, which takes that to ~53 evictions a plane.
// ~2 KB of heap, taken at decode time and given straight back -- not a static
// buffer, which would come off the 42,152-byte reading floor for something that
// runs once per sleep.
constexpr int kCoverBatchBytes = 1024;

// THE COVER CACHE'S WRITER, over SdFat directly rather than through
// reader::FileSystem.
//
// ATOMICITY WITHOUT A RENAME. The header goes down with `complete = 0`, the plane
// rows stream, and only then is the header rewritten with `complete = 1`. So an
// abandoned decode, a refusal or a flat battery leaves a file that
// sleepCoverUsable declines, and the next sleep simply tries again.
//
// THE WHOLE HEADER IS REWRITTEN, not the four bytes of the flag, and that is
// deliberate: encodeSleepCoverHeader stays the ONE spelling of the wire format.
// Seeking to the flag would put a hand-computed field offset in this file, which
// a field added to the struct would silently invalidate -- and sleep_cover.h
// already records a tripwire being added because a derived-LOOKING constant was
// not derived.
//
// WHY THERE IS A FILLER PASS: SdFat REFUSES a seek past the end of a file
// (FatFile::seekSet, `if (pos > m_fileSize) goto fail`), so the first LSB row --
// which belongs one whole plane further in than anything written so far -- has
// nowhere to go until the bytes in front of it exist. One plane of filler is
// enough: after it the MSB batches seek BACK into ground that exists and the LSB
// batches land exactly at the end of the file, which is an ordinary append. The
// cost is one extra 52 KB sequential write, against holding a whole plane in RAM
// (52,272 bytes, which is most of the budget this feature has) or writing the two
// planes to two files and concatenating them (52 KB read plus 52 KB write, two
// files, and the same eviction problem while both are open).
class CardCoverSink : public reader::CoverPlaneSink {
 public:
  CardCoverSink(const std::string& bookPath, uint32_t bookBytes, int rotation)
      : bookPath_(bookPath) {
    header_.rotation = rotation;
    header_.bookBytes = bookBytes;
  }
  ~CardCoverSink() override { closeFile(); }

  CardCoverSink(const CardCoverSink&) = delete;
  CardCoverSink& operator=(const CardCoverSink&) = delete;

  bool begin(int panelW, int panelH, int planeRowBytes, int rows) override {
    if (planeRowBytes <= 0 || rows <= 0) return false;
    rowBytes_ = planeRowBytes;
    rowsExpected_ = rows;
    const size_t planeBytes =
        static_cast<size_t>(planeRowBytes) * static_cast<size_t>(rows);

    header_.panelW = panelW;
    header_.panelH = panelH;
    header_.planeBytes = static_cast<int32_t>(planeBytes);
    header_.complete = 0;
    // A PATH THAT DOES NOT FIT IS A REFUSAL HERE, not a file written and never
    // read. setSleepCoverBookPath stores EMPTY rather than truncating -- two books
    // sharing their first 127 bytes would otherwise each accept the other's
    // picture -- and sleepCoverUsable never matches empty, so the 104 KB would be
    // work nothing could ever use.
    if (!reader::setSleepCoverBookPath(header_, bookPath_)) {
      logf("[cover] the path will not fit the cache header: %s\n", bookPath_.c_str());
      return false;
    }
    // THE CACHE HAS TO BE THE SHAPE OF THE FRAME IT WILL BE READ INTO, or
    // sleepCoverUsable refuses it forever. The two derivations differ -- cover.cpp
    // sizes a plane as ceil(panelW / 8) * panelH and Framebuffer sizes its store
    // as ceil(physWidth / 8) * physHeight -- and they agree only because both
    // panels are multiples of 8. Cheaper to find that out here than after a decode
    // and 104 KB of card writes.
    if (gFrame && header_.planeBytes != gFrame->sizeBytes()) {
      logf("[cover] a %d-byte plane is not the frame's %d bytes; not caching\n",
           static_cast<int>(header_.planeBytes), gFrame->sizeBytes());
      return false;
    }

    rowsPerBatch_ = kCoverBatchBytes / rowBytes_;
    if (rowsPerBatch_ < 1) rowsPerBatch_ = 1;
    batchBytes_ = static_cast<size_t>(rowsPerBatch_) * static_cast<size_t>(rowBytes_);
    // nothrow, because -fno-exceptions makes a failed `new` an abort() with no
    // diagnostic -- this project has lost a boot to exactly that.
    batch_.reset(new (std::nothrow) uint8_t[2 * batchBytes_]);
    if (batch_ == nullptr) {
      logf("[cover] no memory for the cache's %u-byte row batch\n",
           static_cast<unsigned>(2 * batchBytes_));
      return false;
    }

    // /.reader exists on any card that has booted -- the settings file and the
    // reading state both live there -- so this is insurance rather than a step.
    // The parent is derived from the one path constant rather than spelled again.
    const char* const path = reader::kSleepCoverPath;
    const char* const slash = std::strrchr(path, '/');
    if (slash != nullptr && slash != path)
      gSd.mkdirs(std::string(path, static_cast<size_t>(slash - path)));

    file_ = SdMan.open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (!file_) {
      logf("[cover] cannot open %s to write\n", path);
      return false;
    }
    open_ = true;

    uint8_t raw[reader::kSleepCoverHeaderBytes];
    reader::encodeSleepCoverHeader(header_, raw);
    if (file_.write(raw, sizeof(raw)) != sizeof(raw)) return false;

    // The filler. 0xFF is paper, so a file cut off inside it is at least
    // paper-shaped -- though nothing will ever read it, because `complete` stays 0
    // until finish() says otherwise.
    std::memset(batch_.get(), 0xFF, 2 * batchBytes_);
    const size_t chunk = 2 * batchBytes_;
    for (size_t left = planeBytes; left > 0;) {
      const size_t n = left < chunk ? left : chunk;
      if (file_.write(batch_.get(), n) != n) return false;
      left -= n;
    }

    msbOff_ = reader::kSleepCoverHeaderBytes;
    lsbOff_ = reader::kSleepCoverHeaderBytes + planeBytes;
    return true;
  }

  bool row(const uint8_t* msb, const uint8_t* lsb) override {
    if (!open_ || batch_ == nullptr || msb == nullptr || lsb == nullptr) return false;
    // MORE ROWS THAN begin() DECLARED would run off the end of the plane regions
    // the file was sized for. cover.h promises exactly `rows` of them; this is
    // what makes that a check rather than a belief.
    if (rowsSeen_ >= rowsExpected_) return false;
    const size_t at = static_cast<size_t>(pending_) * static_cast<size_t>(rowBytes_);
    // A PAPER ROW PASSES THE SAME POINTER TWICE (cover.h), so these are two copies
    // of one row rather than a pair to compare.
    std::memcpy(batch_.get() + at, msb, static_cast<size_t>(rowBytes_));
    std::memcpy(batch_.get() + batchBytes_ + at, lsb, static_cast<size_t>(rowBytes_));
    ++pending_;
    ++rowsSeen_;
    if (pending_ >= rowsPerBatch_) return flushBatch();
    return true;
  }

  bool finish(bool ok) override {
    // ALWAYS CALLED, EVEN WHEN begin() WAS NOT (cover.h) -- a book with no cover, a
    // span that is not an image, a card that would not open. `open_` is what tells
    // those from a real write.
    bool good = ok && open_;
    // A SHORT DECODE IS NOT A GOOD FILE. cover.h promises `rows` calls unless
    // something refused, so a decode that stopped early with ok = true is a bug
    // upstream, and the one file the user stares at for hours is the wrong place
    // to be lenient about it.
    if (good && rowsSeen_ != rowsExpected_) {
      logf("[cover] the cache got %d of %d rows; not committing\n", rowsSeen_, rowsExpected_);
      good = false;
    }
    if (good) good = flushBatch();
    if (good) {
      // THE COMMIT. Sync the planes FIRST, so `complete` cannot reach the card
      // ahead of the bytes it vouches for, then rewrite the whole header with the
      // flag set and sync again.
      header_.complete = 1;
      uint8_t raw[reader::kSleepCoverHeaderBytes];
      reader::encodeSleepCoverHeader(header_, raw);
      good = file_.sync() && file_.seekSet(0) &&
             file_.write(raw, sizeof(raw)) == sizeof(raw) && file_.sync();
    }
    // NOTHING IS REMOVED ON FAILURE, and that is what the flag is for: the file
    // still says complete = 0, sleepCoverUsable refuses it, and the next sleep
    // opens it O_TRUNC and tries again. A remove would be a second failure path
    // guarding a state the first one already covers.
    closeFile();
    if (!good) logf("[cover] the cache was NOT committed\n");
    return good;
  }

 private:
  bool flushBatch() {
    if (pending_ == 0) return true;
    const size_t n = static_cast<size_t>(pending_) * static_cast<size_t>(rowBytes_);
    if (!file_.seekSet(msbOff_)) return false;
    if (file_.write(batch_.get(), n) != n) return false;
    msbOff_ += n;
    // The LSB region is written strictly forward and always lands exactly at the
    // end of the file, so this seek reaches an append rather than a rewrite. That
    // is the property the single plane of filler in begin() buys.
    if (!file_.seekSet(lsbOff_)) return false;
    if (file_.write(batch_.get() + batchBytes_, n) != n) return false;
    lsbOff_ += n;
    pending_ = 0;
    return !file_.getWriteError();
  }

  void closeFile() {
    if (open_) {
      file_.close();
      open_ = false;
    }
    batch_.reset();
  }

  // ONE GUARD FOR THE WHOLE WRITE, held for this object's lifetime -- the only
  // place in this firmware that holds one that long. sd_fs.h argues the opposite
  // for a FileHandle and is right there: a reader holds a book open for MINUTES
  // across many paints, and renderTop() would block behind it, which reads as a
  // display fault. This is the other shape -- seconds, on the loop task, with
  // nothing left to paint until it is finished. It is declared first so it is
  // taken before anything below it touches the card, and the SD card is on the
  // DISPLAY'S own bus, which is what makes it necessary at all.
  SpiBusGuard bus_;
  std::string bookPath_;
  reader::SleepCoverHeader header_;
  FsFile file_;
  std::unique_ptr<uint8_t[]> batch_;
  size_t batchBytes_ = 0;
  // Where the next batch of each plane goes. Byte offsets into the file, not row
  // numbers: one of them seeks backwards into ground the filler laid and the
  // other appends, and only bytes say that.
  size_t msbOff_ = 0, lsbOff_ = 0;
  int rowBytes_ = 0, rowsExpected_ = 0, rowsSeen_ = 0;
  int rowsPerBatch_ = 0, pending_ = 0;
  bool open_ = false;
};

// THE COVER CACHE'S READER.
//
// ONE OPEN AND ONE PASS PER PLANE, holding nothing: a resident plane is 52,272
// bytes, which is more than this whole feature's budget. That is what
// CoverSource exists to make possible -- see reader/screen_sleep.h.
//
// IT IS NOT A memcpy INTO fb.data(), AND THAT IS THE HALF ONLY THE PANEL CAN SEE.
// The file holds LOGICAL raster rows -- a streaming row-major downscale can emit
// nothing else (imagefit.h) -- and this shell binds Rotation::Ccw, under which one
// logical ROW is a physical COLUMN. Framebuffer::writePackedRow is the one
// function in this feature that knows that. A memcpy would be right in the
// simulator, right in every golden and right in every desktop test there is, and
// would smear diagonally on glass -- which is what CLAUDE.md records happening to
// the veil, fillRect, the glyph blit and ditherRect in turn.
//
// TWO PLANES SERVE THREE PASSES: Plane::Bw inks where coverage >= 2, which is
// exactly "MSB set", so the base pass and the Msb pass read the SAME plane. A
// source that answered Bw with anything else would give a base pass that
// disagrees with the refinement drawn over it.
class CardCoverSource : public reader::CoverSource {
 public:
  // WHICH BOOK THIS IS A PICTURE OF. Not optional and not derivable here: a cover
  // is not a subtle wrong when it is the wrong book's.
  void setBook(const std::string& path, uint32_t bytes) {
    bookPath_ = path;
    bookBytes_ = bytes;
  }

  bool loadPlane(reader::Plane plane, reader::Framebuffer& fb) override;

 private:
  std::string bookPath_;
  uint32_t bookBytes_ = 0;
};

// The header off an already-open handle. Two callers -- the paint's gate opens the
// file for this alone, loadPlane needs the handle open for the planes anyway -- so
// the parse lives here once rather than being spelled at both.
bool readSleepCoverHeader(reader::FileHandle& f, reader::SleepCoverHeader& out) {
  uint8_t raw[reader::kSleepCoverHeaderBytes];
  if (!f.seek(0)) return false;
  if (f.read(raw, sizeof(raw)) != sizeof(raw)) return false;
  return reader::decodeSleepCoverHeader(raw, sizeof(raw), out);
}

bool CardCoverSource::loadPlane(reader::Plane plane, reader::Framebuffer& fb) {
  // One row of one plane: 60 bytes on the X4, 66 on the X3. On the stack, which
  // has 16 KB (SET_LOOP_TASK_STACK_SIZE). The cap is CHECKED rather than assumed,
  // because every length below is derived from a number that came off a card.
  uint8_t rowBuf[128];
  const int rowBytes = (fb.width() + 7) / 8;
  if (rowBytes <= 0 || rowBytes > static_cast<int>(sizeof(rowBuf))) return false;

  std::unique_ptr<reader::FileHandle> f = gSd.openRead(reader::kSleepCoverPath);
  if (f == nullptr) return false;
  reader::SleepCoverHeader h;
  if (!readSleepCoverHeader(*f, h)) return false;
  // ASKED AGAIN, although the shell asked it before the screen was built. It is
  // one predicate and it is free here -- the header is already in hand -- and this
  // is the call that stands between a card that changed under us and a write into
  // the driver's own framebuffer.
  if (!reader::sleepCoverUsable(h, bookPath_, bookBytes_, fb.width(), fb.height(),
                                static_cast<int>(fb.rotation()), fb.sizeBytes()))
    return false;
  // sleepCoverUsable has pinned the plane's SIZE to this frame's; this pins its
  // SHAPE, which is what decides how far each read goes. Both panels are multiples
  // of 8 so the two derivations agree -- a panel that was not would land here
  // rather than on a sheared picture.
  if (static_cast<int64_t>(rowBytes) * fb.height() != h.planeBytes) return false;

  // Bw and Msb are plane 0, Lsb is plane 1. See the class comment: Bw inks where
  // coverage >= 2, which is exactly "MSB set". BwDithered cannot reach here -- the
  // screen declares Grayscale whenever it has a cover -- and takes the MSB with
  // everything else rather than being a fourth case with nothing to answer.
  const size_t planeIndex = (plane == reader::Plane::Lsb) ? 1u : 0u;
  const size_t start =
      reader::kSleepCoverHeaderBytes + planeIndex * static_cast<size_t>(h.planeBytes);
  if (!f->seek(static_cast<uint32_t>(start))) return false;
  for (int y = 0; y < fb.height(); ++y) {
    // A FALSE HERE LEAVES THE FRAME PART-WRITTEN, which the contract allows and
    // renderSleep is built for: it clears and draws the dither field whenever this
    // answers false, so a partial picture is overwritten rather than shown.
    if (f->read(rowBuf, static_cast<size_t>(rowBytes)) != static_cast<size_t>(rowBytes))
      return false;
    fb.writePackedRow(y, rowBuf);
  }
  return true;
}

}  // namespace

// NOT OWNED BY THE SCREEN AND IT MUST OUTLIVE IT (screen_sleep.h), so it lives
// here rather than on paintSleepScreen's stack.
static CardCoverSource gCoverSource;

// WHICH BOOK THE SLEEP SCREEN IS ABOUT, and how big it is.
//
// FROM last.json, which is what the sleep CARD is drawn from (sleepVmFromCard), so
// the picture and the words cannot end up naming two different books. Not from
// gReading: the device sleeps from Home and from the Library as often as from a
// page, and there is no book open on either.
//
// THE SIZE IS READ OFF THE FILE rather than remembered, because it is the cache's
// identity check and last.json does not carry it. One open, which also answers the
// question sleepVmFromCard asks with exists(): is this book still on the card.
static bool sleepCoverBook(std::string& path, uint32_t& bytes) {
  if (!gStorageUsable) return false;
  reader::LastRead last;
  if (!reader::loadLastRead(gSd, last) || last.bookPath.empty()) return false;
  std::unique_ptr<reader::FileHandle> f = gSd.openRead(last.bookPath);
  if (f == nullptr) return false;
  path = last.bookPath;
  bytes = f->size();
  return true;
}

// Whether the setting asks for a picture at all. design/Settings.dc.html's `Shows`
// row, and SleepShows::Details is the shipped screen with no cover in it.
static bool coverWanted() { return gSettings.sleepShows != reader::SleepShows::Details; }

// THE COVER TO HAND SleepScreen, OR NULL -- AND THE HEADER IS VALIDATED HERE,
// BEFORE THE SCREEN IS CONSTRUCTED.
//
// THAT ORDER IS THE CONSTRAINT, not a nicety. Fidelity is decided ONCE, from
// whether the screen has a source at all, and the three grayscale passes then each
// ask loadPlane separately -- so a source that succeeded for Msb and failed for
// Lsb would compose a frame with the cover in one plane and the dither field in
// the other. Validating up front is what keeps that theoretical: after it the only
// remaining failure is the card physically leaving mid-paint, at which point the
// sleep screen has lost more than its cover.
//
// It is also why a source that WILL refuse must never be handed over. SleepScreen
// declares Fidelity::Grayscale on the strength of holding one, so a source that
// then fell back would spend three waveforms -- ~1041 ms of panel -- drawing a
// screen one waveform could have drawn.
static reader::CoverSource* sleepCoverForPaint() {
  if (!coverWanted() || !gStorageUsable || !gFrame) return nullptr;
  std::string path;
  uint32_t bytes = 0;
  if (!sleepCoverBook(path, bytes)) return nullptr;
  std::unique_ptr<reader::FileHandle> f = gSd.openRead(reader::kSleepCoverPath);
  if (f == nullptr) return nullptr;
  reader::SleepCoverHeader h;
  if (!readSleepCoverHeader(*f, h)) return nullptr;
  if (!reader::sleepCoverUsable(h, path, bytes, gFrame->width(), gFrame->height(),
                                static_cast<int>(gFrame->rotation()), gFrame->sizeBytes()))
    return nullptr;
  gCoverSource.setBook(path, bytes);
  return &gCoverSource;
}

// Whether a paint could use the cache right now, which is the sleep path's "do I
// need to decode". ONE spelling, the same call the paint makes, so the two can
// never disagree about what counts as a usable cover.
static bool coverCacheUsable() { return sleepCoverForPaint() != nullptr; }

// Stops the sleep decode on a genuinely NEW press. See the block below for why a
// bare rawSamplesPending() cannot serve here and what it cost.
static bool sleepDecodeShouldStop(void*) {
  bool pressed = false;
  RawSample s{};
  // Drain whatever is queued: the release edge of the press that asked for this
  // sleep is in here, and it is not a reason to stop.
  while (popRawSample(s)) {
    if (s.down) pressed = true;
  }
  return pressed;
}

// DECODE THE LAST-READ BOOK'S COVER INTO THE CACHE.
//
// It reads the book itself: openBook is a central-directory parse and an OPF
// inflate, ~32 KB transient, which is why this is not done on every sleep but only
// when coverCacheUsable() says there is nothing to paint.
//
// THE STOP PREDICATE IS sleepDecodeShouldStop(), NOT rawSamplesPending(), AND THE
// DIFFERENCE IS THE WHOLE FEATURE.
//
// It was rawSamplesPending(), copied from the four idle jobs in loop(), and it
// abandoned EVERY decode a power press ever started -- which is every decode a
// user starts. Measured on the X3: `[cover] Abandoned in 353ms`, `paint2=0ms`,
// the cache never committed, and the sleep screen therefore identical to the one
// that shipped. The feature looked unimplemented.
//
// The mechanism is that rawSamplesPending() means "is there NEW input" ONLY where
// something drains the queue. loop() drains it at the top of every iteration --
// waitForRawSample's own comment says "the loop's own drain at the top of the next
// iteration is what owns these" -- so the four idle jobs read it correctly.
// sleepNow() is [[noreturn]] and never returns to loop(), so NOTHING drains it.
// POWER fires Short on the DOWN edge (that is how this sleep was triggered) and
// the input task queues the matching RELEASE while the first paint blocks for its
// ~774 ms waveform. That release then sits in the queue for the rest of sleepNow,
// and a bare count cannot tell it from a new press.
//
// So this predicate DRAINS and looks at what it drained, and only a DOWN edge --
// a genuinely new press -- stops the decode. Consuming is correct here and
// nowhere else, for the same reason the bug existed: there is no loop left to own
// these samples. Nothing downstream reads them either; deepSleepUntilPowerButton()
// waits on the GPIO, not on this queue.
//
// KEEPING IT INTERRUPTIBLE AT ALL IS DELIBERATE. The panel already shows the sleep
// screen, so the decode is invisible -- but the power button cannot WAKE the device
// until deepSleepUntilPowerButton() is reached, so an uninterruptible decode would
// leave a reader pressing power at a dead device for up to seven seconds. A new
// press means "I want it back"; abandoning gets there in milliseconds.
//
// An abandoned decode leaves `complete = 0` and is simply re-attempted at the next
// sleep -- there is no half-usable state to reason about.
//
// ONE `[cover]` LINE PER ATTEMPT, WHATEVER HAPPENS, AND IT CARRIES THE TIME.
// The verdict and the elapsed milliseconds were two separate lines while this was
// being built -- the report here, the duration at the call site -- which printed
// the result twice and let the two disagree about which attempt they were
// describing. They are one line. The early refusals below print it too: a decode
// that returned in silence is indistinguishable from one that never ran, which is
// exactly the shape this project keeps paying for (a probe answered from cache, a
// comparison sheet that skipped four screens).
static reader::CoverResult coverVerdict(reader::CoverResult r, uint32_t t0,
                                        const char* why) {
  logf("[cover] %s in %lums: %s\n", reader::coverResultName(r),
       (unsigned long)(millis() - t0), why);
  logFlush();
  return r;
}

static reader::CoverResult decodeCoverToCache() {
  // THE CLOCK STARTS BEFORE THE REFUSALS, not just around decodeCover: a decode
  // that spends 300 ms discovering the book will not open has still spent it, and
  // this number is the sleep path's whole latency budget.
  const uint32_t t0 = millis();
  if (!gStorageUsable || !gFrame)
    return coverVerdict(reader::CoverResult::ReadFailed, t0, "no card, or no frame");
  std::string path;
  uint32_t bytes = 0;
  if (!sleepCoverBook(path, bytes))
    return coverVerdict(reader::CoverResult::NoCover, t0, "no book is open");

  reader::OpenedBook opened;
  const char* why = nullptr;
  if (!reader::openBook(gSd, path, opened, &why))
    return coverVerdict(reader::CoverResult::ReadFailed, t0,
                        why != nullptr ? why : "the book will not open");

  CardCoverSink sink(path, bytes, static_cast<int>(gFrame->rotation()));
  reader::CoverReport rep;
  const reader::CoverResult r = reader::decodeCover(
      gSd, opened, gFrame->width(), gFrame->height(), gSettings.coverFit, sink,
      sleepDecodeShouldStop, nullptr, &rep);
  logf("[cover] %s in %lums src=%dx%d /%d box=%d,%d %dx%d%s%s\n",
       reader::coverResultName(r), (unsigned long)(millis() - t0), rep.sourceWidth,
       rep.sourceHeight, rep.scaleDivisor, rep.dstX, rep.dstY, rep.dstW, rep.dstH,
       rep.reason != nullptr ? ": " : "", rep.reason != nullptr ? rep.reason : "");
  logFlush();
  return r;
}

// THE SLEEP SCREEN, PAINTED WITHOUT BEING PUSHED -- and that is the whole trap this
// screen has carried a warning about since it was written. The session record names the
// top of the stack, so pushing SleepScreen would make the next wake RESTORE INTO IT: the
// user would press power and get "asleep, press power to wake" back. It wants a direct
// render after the record is saved, not a navigation.
//
// So this bypasses App entirely, which means two things App normally owns are this
// function's:
//   * the CLEAR, because nothing else is going to do it;
//   * gFrameContentsUnknown, because App's partial-repaint record now describes a frame
//     that no longer exists. Nothing will read it before the chip resets, but leaving a
//     lie in it would be a trap for whoever paints something after this one day.
//
// WHAT IT SHOWS comes from the same pointer Home reads. Its board is the reading state
// -- NOW READING, the title, the author, the bar -- and the device sleeps from Home or
// the Library as often as from a book, so with nothing open it draws the badge alone
// (design/SleepIdle.dc.html). The badge is the half that carries the screen's purpose:
// e-ink holds its last image, so without it a Library left on the glass gives no clue
// the device is asleep rather than frozen.
// THE SLEEP SCREEN'S VIEW MODEL, and both states build it here. Extracted the
// moment there was a second caller rather than the fifth: waking draws the same
// card with a different line under it, and two copies of "what the badge says about
// the book" would be two chances to disagree about a screen the user sees at both
// ends of a sleep.
static reader::SleepViewModel sleepVmFromCard(std::string note) {
  reader::SleepViewModel vm;
  vm.note = std::move(note);
  vm.nothingToContinue = true;

  reader::LastRead last;
  if (gStorageUsable && reader::loadLastRead(gSd, last) && gSd.exists(last.bookPath)) {
    vm.nothingToContinue = false;
    vm.label = "NOW READING";
    vm.title = last.title.empty() ? last.bookPath : last.title;
    vm.author = last.author;
    vm.progressPercent = last.percent;
    // THE CHAPTER'S NAME, WHERE THIS RAN `6% - CH. 01` AND THE SECOND HALF WAS A
    // SPINE POSITION. This was the last place on the device that showed a position
    // with a name available, and the whole composition is gone with it:
    //
    //   * The PERCENTAGE is no longer built here. The theme composes it from
    //     `progressPercent`, which the bar directly above it already reads, so the
    //     figure and the bar cannot disagree -- they were two spellings of one fact
    //     and this function was free to set them independently.
    //   * The NAME is `last.chapter`, the same string Home's meta line, the
    //     Reader's band and Contents' NOW row draw. Not composed, not derived: one
    //     fact, one spelling.
    //   * SO THERE IS NO LITERAL LEFT TO SPLIT. The note that stood here -- twice,
    //     verbatim, which is the tell that one copy was stale -- warned that a C++
    //     hex escape is UNBOUNDED, so `"\xC2\xB7CH."` parses `\xB7C` as one value:
    //     clang rejects it and the ESP32's GCC accepts it and emits a byte that is
    //     not U+00B7. That trap is real and it now lives where the middot still
    //     does, `screens.cpp`'s `kDot` and this file's own note literals, where the
    //     bytes are their own adjacent literal by construction.
    //
    // UNGUARDED, deliberately, exactly as Home's assignment is: an empty chapter
    // must reach the view model as empty. A `if (!last.chapter.empty())` would leave
    // whatever the field already held -- and for a pointer written before last.json
    // carried a chapter, the honest answer is a card with no chapter line, not a
    // fallback to the position this run has just stopped showing.
    vm.chapter = last.chapter;
  }

  // WHAT THIS SLEEP IS ALLOWED TO SHOW -- design/Settings.dc.html's `Shows` row --
  // AND NOTHING-OPEN FORCES DETAILS.
  //
  // design/SleepIdle.dc.html is the badge ALONE: with no book there is no cover to
  // be a picture OF, so COVER or COVER + DETAILS would be a mode asking for
  // something that cannot exist. The force lives here rather than in the theme
  // because the fallback ladder has four rungs -- the setting, then whether there
  // is a book at all, then whether the cache holds a picture of THIS book
  // (sleepCoverForPaint), then whether the load actually answered
  // (QuietTheme::renderSleep's `covered`) -- and it reads as one ladder only while
  // the rungs are not scattered across three files. The theme already gates every
  // cover-shaped decision on `covered`, so this is the rung ABOVE that, not a
  // second copy of it.
  //
  // IT IS SET IN ONE PLACE FOR THE SAME REASON: after the branch above, so the
  // nothing-open answer is final whichever way that branch went, rather than being
  // written once optimistically and corrected later.
  vm.shows = vm.nothingToContinue ? reader::SleepShows::Details : gSettings.sleepShows;
  return vm;
}

static void paintStatusBar(const char* label) {
  if (!bindFrameToDriver("status")) return;
  SpiBusGuard bus;
  // OVER THE EXISTING FRAME, not over a cleared one: what is on the panel is the
  // screen the user pressed from, and it should stay. Only the bar's own box is
  // touched, which drawStatusBar clears for itself.
  reader::drawStatusBar(*gFrame, *gFonts, label);
  // App's partial-repaint record now describes a frame that no longer matches, and
  // it cannot see this: its check compares the Framebuffer's ADDRESS, which has not
  // changed. Same reason paintSleepScreen sets it.
  gFrameContentsUnknown = true;
  showOnePass(reader::RefreshMode::Fast);
  logf("[status] %s\n", label);
  logFlush();
}

// WHAT A SLEEP PAINT COST, split the way [i] splits an interaction, because a
// sleep is no longer one waveform and nobody should have to instrument it again to
// find that out. With a cover the panel does THREE waveforms and this renders FOUR
// passes; without one it is the single ~825 ms it always was, and `paint=` is which.
//
// `panel=` is total less render, so it carries the log's own cost as well as the
// waveform's -- which is what `ser=` is for. Unplugged it reads ~0 and the rest of
// the line is the device's own, exactly as CLAUDE.md's `ser=` rule says.
static void logSleepPaintCost(const char* how, int passes, uint32_t renderMs,
                              uint32_t t0, uint32_t log0) {
  const uint32_t total = millis() - t0;
  logf("[power] sleep paint=%s passes=%d render=%lums panel=%lums total=%lums ser=%lums\n",
       how, passes, (unsigned long)renderMs, (unsigned long)(total - renderMs),
       (unsigned long)total, (unsigned long)(gLogMs - log0));
  logFlush();
}

static void paintSleepScreen() {
  const uint32_t t0 = millis();
  const uint32_t log0 = gLogMs;
  uint32_t renderMs = 0;
  int passes = 0;

  const reader::SleepViewModel vm =
      sleepVmFromCard(std::string("ASLEEP") + "\xC2\xB7" + "HOLD POWER TO WAKE");

  // THE COVER IS RESOLVED BEFORE THE SCREEN IS CONSTRUCTED, which is the whole of
  // sleepCoverForPaint's contract rather than a convenience here: it validates the
  // cache's header against THIS book and THIS frame and answers null when there is
  // nothing paintable, so fidelity() -- decided once, from whether the screen holds
  // a source at all -- can never promise four levels to a source that will then
  // refuse a plane mid-sequence. A screen that did would spend three waveforms
  // drawing something one waveform could have drawn.
  reader::CoverSource* const cover = sleepCoverForPaint();
  reader::SleepScreen scr(vm, cover);

  // THE OTHER HALF OF THE SHARED-BUS INVARIANT, and it did not matter on this path
  // until now: with a cover the RENDER ITSELF reads the card -- CardCoverSource
  // pulls one packed row at a time out of /.reader/ while the panel's CS is in play
  // -- so a paint here is exactly the mixed-traffic case renderTop() holds this
  // across its whole sequence for. Free insurance today (one task, recursive
  // guard); here to be structural rather than a rule someone remembers.
  SpiBusGuard bus;

  // BEFORE THE PANEL WORK, NOT AFTER, and it moved one statement for that reason:
  // the ~825 ms below is the last thing this device does, so a log that says which
  // sequence is about to run beats one that says which one just did. It also names
  // the cover, because "the cover did not appear" has two explanations that look
  // identical on glass -- no usable cache, or a cache the paint refused.
  logf("[power] sleep screen: %s%s\n",
       vm.nothingToContinue ? "the badge alone, nothing open" : vm.title.c_str(),
       cover != nullptr ? " +cover (four levels)" : "");
  logFlush();

  if (scr.fidelity() != reader::Fidelity::Grayscale) {
    // TODAY'S PATH, UNCHANGED: one Bw render, one FULL waveform. Reached whenever
    // there is no usable cached cover -- which is every sleep before the first
    // decode, every sleep with `Shows` on DETAILS, and every sleep with nothing
    // open (sleepVmFromCard forces DETAILS there; SleepIdle is the badge alone and
    // has no cover to show).
    const uint32_t r0 = millis();
    gFrame->clear(true);
    scr.render(*gFrame, *gFonts, gTheme, reader::Plane::Bw);
    gFrameContentsUnknown = true;
    renderMs += millis() - r0;
    ++passes;
    // FULL, not fast: this is the last thing the panel is asked to do for hours and a
    // differential update would leave the previous screen's residue under it.
    showOnePass(reader::RefreshMode::Full);
    logSleepPaintCost("mono", passes, renderMs, t0, log0);
    mark("sleep-painted");
    return;
  }

  // FOUR LEVELS, RENDERED STRAIGHT OFF `scr` -- AND paintGray() CANNOT SERVE.
  //
  // That is the trap this branch exists to avoid, not a stylistic choice. Every one
  // of paintGray()'s passes goes through paintPlane() -> gApp->render(), and this
  // screen is painted WITHOUT BEING PUSHED: there is no App on top holding it, and
  // pushing it is precisely what must not happen, since the session record names
  // the top of the stack and the next wake would restore INTO the sleep screen.
  // Worse, sleepNow() RELEASES gApp before calling this a second time, so
  // paintGray() here would be a null dereference on the one paint that carries the
  // cover.
  //
  // The sequence below is paintGray()'s, step for step, with that one substitution.
  // Every rule in it was earned by breaking the panel -- LSB before MSB, the settle
  // pass before the planes, the Bw re-render instead of a fourth frame -- and none
  // of the reasoning is restated here on purpose: two copies of it would drift, and
  // paintGray() is where it lives.
  const auto renderSleepPlane = [&scr, &renderMs, &passes](reader::Plane plane) {
    const uint32_t r0 = millis();
    gFrame->clear(true);
    scr.render(*gFrame, *gFonts, gTheme, plane);
    // App's partial-repaint record now describes a frame App did not write, and it
    // cannot see that: its check compares the Framebuffer's ADDRESS, which has not
    // moved. Set per pass rather than once, because each pass rewrites the frame.
    gFrameContentsUnknown = true;
    renderMs += millis() - r0;
    ++passes;
  };

  // 1. The B/W base frame the panel paints first.
  //
  //    A CLEAN BASE IS FORCED, and this is the ONE place this sequence departs from
  //    paintGray(). displayGrayscaleBase() takes its cheap differential settle
  //    whenever the controller's old plane is valid -- which after any ordinary
  //    paint it is -- and that is right for the READER, where the refinement
  //    replaces a page with the same page at four levels and CLAUDE.md records it
  //    as "366 ms of gray_DRF with no visible flash". It is wrong here twice over:
  //    a first sleep paint replaces a whole chrome screen, and a second replaces a
  //    card on a dither field with a PHOTOGRAPH. This is the last image the glass
  //    holds for hours, and the rule for that is already written down one branch
  //    up -- the sleep paint is FULL "because a differential update would leave the
  //    previous screen's residue under it". requestResync() is how that rule
  //    reaches the grayscale path; on Uc8279 it sets _forceFullSyncNext, which is
  //    what makes displayGrayscaleBase take its clean-base branch and drive the
  //    `fallback` mode below.
  //
  //    THE ASYMMETRY IS WHY IT IS HERE, and it is what Task 18 must settle on
  //    glass. Being wrong costs one extra GC flash (~693 ms) per grayscale sleep
  //    paint, on a device the user has already walked away from. NOT doing it and
  //    being wrong costs the card's text ghosted under a book cover for as long as
  //    the device is asleep. The reversal is this one line.
  display.requestResync();
  renderSleepPlane(reader::Plane::Bw);
  // FULL rather than paintGray()'s HALF for the same reason the mono branch above
  // asks for FULL. Note the two are the same waveform on Uc8279 -- displayStart
  // picks GC for anything that is not Fast -- so this states the intent on a driver
  // where it is free, rather than relying on that identity holding elsewhere.
  display.displayGrayscaleBase(EInkDisplay::FULL_REFRESH);
  mark("sleep-gray-base");

  // 2. The settle pass. NOT a duplicate of the settle inside displayGrayscaleBase,
  //    however identical the two command sequences look: paintGray() records what
  //    dropping it did to the panel (ink accumulating on every refresh).
  display.preconditionGrayscale();

  // 3. The two bit-planes, LSB FIRST -- the MSB copy is dropped unless the driver
  //    has seen a valid LSB. Each copy goes straight out over SPI and retains no
  //    pointer, so the one frame serves both.
  renderSleepPlane(reader::Plane::Lsb);
  display.copyGrayscaleLsbBuffers(gFrame->data());
  renderSleepPlane(reader::Plane::Msb);
  display.copyGrayscaleMsbBuffers(gFrame->data());
  mark("sleep-gray-planes");

  // 4. The combined 4-level image, then the rebase onto a valid B/W baseline.
  //
  //    THE REBASE IS NOT SKIPPABLE EVEN HERE, where the next statement is a chip
  //    reset -- and the temptation to skip it is exactly the confusion CLAUDE.md
  //    warns about. cleanupGrayscaleBuffers is what takes the controller OUT of
  //    grayscale mode, and display.deepSleep() runs against it moments later; the
  //    glass keeps its image with no power, the controller keeps nothing. Leaving
  //    the driver in a state its own invariants do not expect, on the way into the
  //    one call that cannot be observed, is not a saving worth 156 ms.
  display.displayGrayBuffer();
  renderSleepPlane(reader::Plane::Bw);
  display.cleanupGrayscaleBuffers(gFrame->data());
  logSleepPaintCost("gray", passes, renderMs, t0, log0);
  mark("sleep-painted");
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
  // AFTER the position is saved and BEFORE the panel is put to sleep. It costs one
  // full waveform (~825 ms) on every sleep, which is the price of the device looking
  // asleep rather than frozen.
  // THE READING POSITION GOES DOWN WITH THE DEVICE. Deep sleep is a chip reset, so
  // nothing in RAM survives it -- and a reader who closes the cover mid-page expects
  // that page back.
  // THE PHASE CLOCKS. A sleep used to be one waveform and is now up to five things,
  // so it gets the same treatment an interaction gets: one line at the end that adds
  // up, rather than a reader summing `[stage]` timestamps by hand. See the
  // `[power] sleep cost` line below.
  const uint32_t tSleep0 = millis();
  const uint32_t logSleep0 = gLogMs;

  saveReadingPosition("sleep");
  const uint32_t tSaved = millis();
  paintSleepScreen();
  const uint32_t tPaint1 = millis();

  // CAPTURED BEFORE ANYTHING CAN RELEASE THE App, and the log line below is the
  // reason. screenName returns a string literal, so this outlives the stack it was
  // asked from -- which by the time that line prints may not exist.
  const char* const sleptFrom = reader::screenName(gApp->top().id());

  // THE SLEEP SCREEN REFINES, EXACTLY AS A READER PAGE DOES. The card is painted
  // first because it is correct and honest immediately; the cover arrives on a
  // second sequence a few seconds later. The user has pressed power and walked
  // away, so the first sleep of a new book still ENDS with the cover on the glass
  // -- and every sleep after it paints the cover straight away, because the cache
  // is already there.
  //
  // WHY IT IS HERE AND NOT ANYWHERE ELSE IN THIS FUNCTION: the decode needs the
  // CARD and the second paint needs the PANEL, and the next two statements take
  // both away -- display.deepSleep(), then powerDownRailsForSleep() cutting the
  // X3's SD rail. So the position is forced by the hardware rather than chosen.
  //
  // AND IT IS BEFORE markSleeping(), which the ordering already gave us for free:
  // that call sits below deepSleep() and the rail cut. It matters that it stays
  // that way round. The flag is what the next boot needs and the log is only what
  // a human needs, so a decode that hangs and takes a reset must leave NO flag: an
  // unflagged boot starts cold, which is the correct answer for a sleep that never
  // completed, where a flagged one would resume from a sleep that did not happen.
  //
  // THE PROBE IS HOISTED OUT OF THE `if` SO IT CAN BE TIMED. It is two openReads and
  // a header parse -- the same pair the paint above already did -- and it is paid on
  // EVERY sleep that wants a cover, hit or miss, so it is a number worth having
  // rather than a shrug.
  const bool decodeNeeded = coverWanted() && !coverCacheUsable();
  const uint32_t tProbed = millis();
  uint32_t decodeMs = 0;
  uint32_t paint2Ms = 0;

  if (decodeNeeded) {
    // RELEASE THE WHOLE App, NOT JUST THE CHAPTER -- and the margin is why.
    //
    // ReaderScreen::releaseChapter() already exists, built for the peek, and it
    // frees the right 36,956 bytes (Inflater's private Scratch; note that
    // `inflater_` is a VALUE member, so an "obvious" release that drops the
    // BlockReader, the InflateSource wrapper, the buffer view and the file handle
    // frees none of them). It is not enough. Measured on the X3, a deflated JPEG
    // peaks at 81,088 bytes -- 17.5 KB ABOVE the desktop's 63,560 for the same
    // work -- and releasing only the chapter leaves ~87 KB when the book was
    // opened through the LIBRARY, whose 203 entries sit resident under the Reader
    // at ~59 KB. That is a margin of about SIX kilobytes on the commonest way to
    // open a book, and the failure would be SILENT: decodeCover answers
    // OutOfMemory, the screen falls back to the reading card, and it reads as
    // "covers don't work for some books" rather than as a defect anybody reports.
    // Releasing the App takes it to ~65 KB.
    //
    // NOTHING NEEDS THE App AFTER THE FIRST SLEEP PAINT, and each half of that was
    // checked rather than assumed: saveWhereWeAre wrote the session record at
    // NAVIGATION time and not here; saveReadingPosition ran above, while the stack
    // was still standing; paintSleepScreen bypasses App by design, because pushing
    // SleepScreen would make the next wake restore INTO it; and the next statement
    // after this block is a chip reset. So this is the sentence the spec always
    // carried -- sleep is the only moment in this firmware where freeing
    // everything is free -- finally spent.
    gApp.reset();
    const uint32_t d0 = millis();
    const reader::CoverResult r = decodeCoverToCache();
    decodeMs = millis() - d0;
    // The verdict, the reason and the elapsed time are one `[cover]` line inside
    // decodeCoverToCache. Printing it again here would be the same fact twice.
    if (r == reader::CoverResult::Ok) {
      const uint32_t p0 = millis();
      paintSleepScreen();
      paint2Ms = millis() - p0;
    }
    // reacquireChapter() is deliberately NOT called, and neither is anything that
    // would rebuild the App. There is nothing to come back to: the next statement
    // is deepSleep(), and the wake after it is a chip reset that runs setup() from
    // the top and restores the stack from the session record.
  }

  // WHAT THIS SLEEP COST, in one line that adds up.
  //
  // The shape a reader should expect, and why each case is what it is:
  //   * DETAILS, or nothing open      -- paint1 only, ~825 ms, probe=0 decode=0.
  //   * a cover wanted, cache warm    -- paint1 is the GRAY paint (three waveforms
  //                                      plus four render passes), decode=0.
  //   * a cover wanted, cache cold    -- paint1 MONO, then probe, then decode, then
  //                                      paint2 GRAY. This is the expensive one, and
  //                                      it happens once per book.
  // `[power] sleep paint=` above breaks each paint into render and panel, and
  // `[cover] … in Nms` names what the decode was doing. `ser=` is how much of the
  // total was this device talking to a USB host: unplugged it reads ~0 and every
  // other number on the line is the device's own.
  logf("[power] sleep cost save=%lums paint1=%lums probe=%lums decode=%lums "
       "paint2=%lums total=%lums ser=%lums\n",
       (unsigned long)(tSaved - tSleep0), (unsigned long)(tPaint1 - tSaved),
       (unsigned long)(tProbed - tPaint1), (unsigned long)decodeMs,
       (unsigned long)paint2Ms, (unsigned long)(millis() - tSleep0),
       (unsigned long)(gLogMs - logSleep0));
  logf("[power] sleeping from screen=%s; the record should name it on wake. Wake with "
       "the power button\n",
       sleptFrom);
  logFlush();
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
  // LAST THING BEFORE THE LIGHTS GO OUT. On battery this call does not return and
  // the chip loses power entirely, so the next boot's reset reason is POWERON and
  // indistinguishable from a first-ever start. The flag is what makes the next
  // boot know it was a resume; see session.h.
  markSleeping();
  // THE LAST THING BEFORE THE CHIP STOPS. Without this the buffer dies with the RAM
  // and the log ends at whatever idle flush happened last -- which on a device that
  // sleeps after five minutes is most of what you wanted to read. It is after
  // markSleeping deliberately: the flag is what the next boot needs and this is only
  // what a human needs, so the ordering says which one may not be lost.
  if (gCardLog.enabled() && gCardLog.size() > 0) {
    logf("[log] sleeping\n");
    flushLogToCard();
  }
  freeink::PowerManager::deepSleepUntilPowerButton();
}

// BYPASSES App, exactly as paintSleepScreen does and for its reason: pushing this
// screen would make the next wake RESTORE INTO IT.
//
// That moves two things App normally owns into this function -- the CLEAR, and
// gFrameContentsUnknown, because App's partial-repaint record would otherwise
// describe a frame that no longer exists. Nothing reads it before the chip stops,
// and leaving a lie there is a trap for the next person to paint after it.
static void paintBatteryEmptyScreen() {
  reader::BatteryEmptyScreen scr;
  // Free insurance today (one task, recursive guard); here to be structural rather
  // than a rule someone remembers, exactly as paintSleepScreen and renderTop take it.
  SpiBusGuard bus;
  logf("[power] battery empty: painting the shutdown screen\n");
  logFlush();
  gFrame->clear(true);
  scr.render(*gFrame, *gFonts, gTheme, reader::Plane::Bw);
  gFrameContentsUnknown = true;
  // FULL, not fast: this is the last thing the panel is asked to do, for as long as
  // the pack stays flat, and a differential update would leave the previous screen's
  // residue under it.
  showOnePass(reader::RefreshMode::Full);
}

// The pack is flat. Save, say so, and stop.
//
// [[noreturn]] like sleepNow, and reached from loop() rather than from a dispatch:
// a paint cannot be interrupted, so this must not run inside one.
[[noreturn]] static void criticalShutdown() {
  logf("[power] CRITICAL pct=%d charging=%d -> shutting down\n", gBattery.percent(),
       gBattery.charging() ? 1 : 0);
  logFlush();

  // FIRST, AND THE BOARD'S COPY DEPENDS ON IT. "Your page is saved" is a promise,
  // and this is what keeps it.
  saveReadingPosition("battery");
  paintBatteryEmptyScreen();

  display.deepSleep();
  // Cuts the X3's SD rail (GPIO13) and any other gated rail, latched so the switches
  // stay off. On a flat pack that is the difference between a device that can be
  // charged back up and one that reaches the cell's protection cut-off.
  freeink::PowerManager::powerDownRailsForSleep();

  // BOTH FLAGS. markSleeping() so that once charged the wake RESTORES the reader's
  // page rather than starting cold -- the other half of "your page is saved".
  // markCriticalShutdown() is what licenses setup()'s strict >= kResumePercent gate:
  // without it the gate would have to sit at the critical threshold itself (which
  // flaps), or refuse every boot below 15% (which would refuse a perfectly usable
  // 10% battery that never shut anything down).
  markSleeping();
  markCriticalShutdown();

  // THE LAST THING BEFORE THE CHIP STOPS, for sleepNow's reason: the buffer dies
  // with the RAM, and this is the one shutdown whose log a user will want.
  if (gCardLog.enabled() && gCardLog.size() > 0) {
    logf("[log] battery empty\n");
    flushLogToCard();
  }
  freeink::PowerManager::deepSleepUntilPowerButton();
}

void loop() {
  // setup() bails out without building the app on a font-load, heap or geometry
  // failure. Repeat the last stage reached so the hang point is visible even
  // when the host attaches late; dereferencing a null gApp below would turn a
  // diagnosable failure into a crash loop that looks like a bootloader hang.
  if (!gApp) {
    static uint32_t n = 0;
    logf("[alive] %lu last-stage=%s heap=%u (setup did not complete)\n",
         (unsigned long)++n, stage, (unsigned)ESP.getFreeHeap());
    logFlush();
    delay(2000);
    return;
  }

  // A DROPPED RAW EDGE INVALIDATES WHAT WE THINK IS HELD, so say so before
  // feeding the recognizer anything else. The input task drops transitions when
  // its 32-deep queue fills, and a FULL paint blocks this loop for ~825 ms --
  // long enough at a fast mash. A dropped PRESS costs nothing; a dropped RELEASE
  // leaves the button latched down in the recognizer, which then either invents a
  // Long for a button nobody is touching or hands the next press a stale
  // timestamp so it classifies as Long. On a list that is the actions overlay
  // instead of the item: a wrong action, from an input the user never made.
  //
  // The counter was already on the [alive] line, but a counter diagnoses after
  // the wrong action; this acts on it. Compared against the last value rather
  // than a flag because the task increments it whenever it likes.
  static uint32_t lastDropped = 0;
  const uint32_t droppedNow = rawSamplesDropped();
  if (droppedNow != lastDropped) {
    logf("[input] %lu raw edge(s) dropped -- forgetting held buttons\n",
         (unsigned long)(droppedNow - lastDropped));
    lastDropped = droppedNow;
    gPresses.forgetPresses();
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
    // BEFORE THE PRE-DISPATCH WORK, not before the dispatch: most of what this
    // measures happens above gApp->dispatch(), not inside it -- the details
    // author's archive open, the contents hand-over, the position save on the way
    // out of a book. `beforeDispatch` below is deliberately a different, later
    // mark, because logChapterOpen wants the chapter open's own cost and not the
    // press's.
    const uint32_t eventStart = millis();
    // THE FIRST EVENT OF A BURST OPENS THE RECORD and the rest only add to it, so
    // a paint that satisfied three presses is reported against the first of them
    // -- which is when the user started waiting.
    if (!gAct.pending) {
      gAct = Interaction{};
      gAct.pending = true;
      gAct.at = ev.at;
      gAct.popped = eventStart;
      gAct.logAtStart = gLogMs;
      gAct.button = ev.button;
      gAct.kind = ev.kind;
      gAct.from = reader::screenName(gApp->top().id());
    }
    ++gAct.events;
    // The `[input]` line that used to be here is gone: it named the button and the
    // kind, which the [i] line below names, and it was a print AND a flush inside
    // the window every field in that line is trying to measure. Power keeps one,
    // because sleepNow() never returns and so never reports.
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
    if (ev.button == reader::Button::Power) {
      logf("[input] POWER %s after %lums -- sleeping, so there is no [i] line for this one\n",
           ev.kind == reader::PressKind::Long ? "LONG" : "SHORT",
           (unsigned long)(eventStart - ev.at));
      sleepNow();
    }
    // LEAVING THE BOOK, saved BEFORE the dispatch -- which is the whole subtlety.
    // Back pops the Reader, and once it is popped there is no screen left to ask
    // where the reader was. This is the edge the user actually reported: going back
    // to the Library and returning lost the page.
    //
    // Back is the ONLY way out (ReaderScreen answers Action::pop() for Gesture::Back
    // and none() for everything it does not use), so this is one save on the way out
    // rather than a save on every event. If a Back ever stops popping, the cost is a
    // save that reports `unchanged`.
    if (ev.button == reader::Button::Back && gApp->top().id() == reader::ScreenId::Reader)
      saveReadingPosition("leaving");
    // THE MENU AND THE CONTENTS ARE PRIMED BEFORE THE DISPATCH THAT PUSHES THEM, and
    // this block sat AFTER it -- while this comment already said "before". The
    // consequences were both reported off the device: the menu header branch tested
    // `top == Reader` and by then the menu was on top, so the factory fell back to
    // "Middlemarch"; and the contents branch tested `top == ReaderMenu` when Contents
    // was already on top, so the row count was never set and the list drew ZERO rows.
    //
    // THIS IS THE THIRD TIME IN ONE SESSION that something needing to run before a
    // dispatch was written after it -- the position save on leaving a book, and Home's
    // rebuild, were the other two. The dispatch is what changes the top of the stack, so
    // anything that asks "what is on top" to decide what to build has to run first.
    //
    // Keyed on the gesture rather than on the screen that results, because the priming
    // has to happen BEFORE the push: Activate on the Reader opens the menu, and
    // Activate on the menu's Contents row opens the list.
    // BOOK DETAILS' AUTHOR, read BEFORE the dispatch that pushes it -- the third time
    // today that "what is on top decides what to build" had to run first, and the first
    // time it was written that way from the start.
    //
    // ONE ARCHIVE OPEN, for the one book the screen shows. The Library's scan cannot
    // learn an author: it lives in the OPF, so per row it would be ~100 ms an open and
    // ~20 s for a 203-book library. And there is heap for it here precisely because no
    // Reader is on the stack -- the same 48 KB that could not be found when the table of
    // contents tried to load from under a live one.
    if (ev.button == reader::Button::Confirm &&
        gApp->top().id() == reader::ScreenId::ItemActions) {
      gFactory.setDetailsAuthor("");
      // ...and the Library answers from its own row, so any facts a previous visit from
      // the reader menu left behind must go. Without this, opening details from the
      // Library after opening them from a book would show the BOOK.
      gFactory.clearDetailsFacts();
      // THE SAME RULE FOR THE DELETE, and it is not theoretical: the factory checks
      // `deleteFactsSet_` BEFORE its Library fallback, and openBookAt primes those
      // facts for BookError's own slab. So Confirm a book that will not open, close
      // the dialog, then `Delete...` a DIFFERENT book from this panel, and the
      // confirmation would name -- and remove -- the corrupt one. This panel's rows
      // are answered from the Library's focused row and nothing else.
      gFactory.clearDeleteFacts();
      reader::LibraryScreen* lib = gFactory.library();
      const reader::LibraryItem* sel = lib != nullptr ? lib->focusedItem() : nullptr;
      if (sel != nullptr && !sel->entry.isDir) {
        std::string p = lib->path();
        if (p.empty() || p.back() != '/') p += '/';
        p += sel->entry.name;
        reader::OpenedBook meta;
        const char* why = "";
        const uint32_t t = millis();
        if (reader::openBook(gSd, p, meta, &why)) {
          gFactory.setDetailsAuthor(meta.author);
          logf("[details] %s by \"%s\" in %lums\n", meta.title.c_str(),
               meta.author.c_str(), (unsigned long)(millis() - t));
        } else {
          logf("[details] no metadata for %s: %s\n", p.c_str(), why);
        }
        logFlush();
      }
    }
    if (gReading.open && ev.button == reader::Button::Confirm) {
      if (gApp->top().id() == reader::ScreenId::Reader) {
        const auto* rd = static_cast<const reader::ReaderScreen*>(&gApp->top());
        char pct[8];
        std::snprintf(pct, sizeof(pct), "%d%%",
                      reader::progressPercent(gFactory.readerBook(), rd->chapterIndex(),
                                              rd->vm().page, rd->vm().pageTotal,
                                              rd->chapterBytesRead()));
        gFactory.setReaderMenuHeader(gReading.title.empty() ? gReading.path : gReading.title,
                                     pct);
      } else if (gApp->top().id() == reader::ScreenId::ReaderMenu) {
        const auto* menu = static_cast<const reader::ReaderMenuScreen*>(&gApp->top());
        // THE `closing` SAVE WENT WITH `Close book`. It existed because that row popped
        // the Reader from UNDER an overlay, where the `leaving` save -- which fires on
        // Back with the Reader on TOP -- could not see it. With the row gone there is
        // one way out of a book again, Back from the page, and `leaving` covers it.
        // ABOUT THIS BOOK, answered from the book the READER has open rather than from a
        // Library row -- there may be no Library on the stack at all, which is exactly
        // why this row was inert. Everything the screen draws is already in hand: the
        // path, the metadata read at open, the file's size, and the position's own
        // percentage and chapter from the sidecar.
        if (menu->vm().focusedRow == reader::ReaderMenuScreen::kAboutBook) {
          reader::BookDetailsScreen::Facts f;
          const size_t slash = gReading.path.rfind('/');
          const std::string leaf =
              slash == std::string::npos ? gReading.path : gReading.path.substr(slash + 1);
          f.fileName = leaf;
          f.directory = slash == std::string::npos ? "" : gReading.path.substr(0, slash);
          // The OPF's title where the book gave one, and the filename otherwise -- the
          // same fallback Home's reading column makes.
          f.title = gReading.title.empty() ? reader::BookList::titleFor(leaf, true)
                                           : gReading.title;
          f.author = gReading.author;
          f.bytes = gReading.bytes;
          // FROM THE READER, not the sidecar: the reader has moved since the last save,
          // and a details screen opened from inside a book should say where the reader IS.
          if (gApp->depth() >= 2) {
            const reader::Screen& under = gApp->at(gApp->depth() - 2);
            if (under.id() == reader::ScreenId::Reader) {
              const auto& rd = static_cast<const reader::ReaderScreen&>(under);
              f.chapter = rd.vm().chapter;
              f.progress = std::to_string(reader::progressPercent(
                               gFactory.readerBook(), rd.chapterIndex(), rd.vm().page,
                               rd.vm().pageTotal, rd.chapterBytesRead())) +
                           "%";
            }
          }
          gFactory.setDetailsFacts(std::move(f));
        }
        // ENTERING THE TYPOGRAPHY PANEL: give the page ring back before the faces
        // are re-rasterised at a new size.
        //
        // Every page in it was laid at the CURRENT column and face, so a type change
        // invalidates all of them -- relayout() drops them anyway. Dropping them HERE
        // instead buys the headroom ScalableFont::init needs, because init takes the
        // new arena before releasing the old: at ppem 46 the roman alone is 24,576
        // bytes transient on top of the 16,384 it already holds.
        //
        // At kPageCacheMaxDepth the ring is ~12 KB; depth 1 is the floor
        // setPageCacheDepth clamps to, and it drops the excess immediately rather
        // than at the next insertion.
        //
        // THE DEPTH IS NOT PUT BACK HERE. The shell re-sizes it from free heap at
        // every warmPageRing, so leaving the panel restores it on the first quiet
        // window -- and a caller that restored a remembered number would be a second
        // opinion about a figure that is derived from the heap.
        if (menu->vm().focusedRow == reader::ReaderMenuScreen::kTypography) {
          if (reader::ReaderScreen* rd = readerOnStack(*gApp)) rd->setPageCacheDepth(1);
        }
        if (menu->vm().focusedRow == reader::ReaderMenuScreen::kContents) {
          // NO CARD WORK HERE. The contents were read when the book opened, where
          // there was heap for them -- see gReading.toc.
          int spine = 0;
          if (gApp->depth() >= 2) {
            // The Reader sits under this panel, and its chapter is what marks `NOW`.
            // Reached through the stack rather than remembered, because a chapter
            // crossing while the menu is closed would make a remembered one stale.
            const reader::Screen& under = gApp->at(gApp->depth() - 2);
            if (under.id() == reader::ScreenId::Reader)
              spine = static_cast<const reader::ReaderScreen&>(under).chapterIndex();
          }
          gFactory.setContents(gReading.toc, spine);
          logf("[toc] handing over %u entries, marking spine %d\n",
               (unsigned)gReading.toc.size(), spine);
          logFlush();
        }
      }
    }
    // THE CHOSEN CHAPTER, taken while Contents is still on top -- the dispatch below
    // pops it, and after that there is no screen left to ask.
    if (ev.button == reader::Button::Confirm && gApp->top().id() == reader::ScreenId::Contents)
      gPendingSpine = static_cast<const reader::ContentsScreen*>(&gApp->top())->chosenSpine();
    // WHICH BUTTON LEFT THE PEEK, taken while it is still on top. `committed()` cannot
    // serve HERE: it is set BY the dispatch, and after the dispatch the screen is gone.
    // So the spine and cursor come off the screen and the intent comes off the BUTTON --
    // the same shape the Typography apply path uses, where a flag is set by the press
    // and consumed after the pop.
    //
    // A SIDE BUTTON PAGES AND DOES NOT POP, so this runs again on the next press with
    // the cursor of whatever page the panel then shows, and `committed` is re-cleared
    // by anything that is not Confirm. The peek stays on top, so the branch that
    // consumes these is not reached until something really does pop it.
    if (gPeekOpen && gApp->top().id() == reader::ScreenId::Peek) {
      const auto* pk = static_cast<const reader::PeekScreen*>(&gApp->top());
      gPeekSpine = pk->chosenSpine();
      gPeekCursor = pk->chosenCursor();
      gPeekCommitted = (ev.button == reader::Button::Confirm);
    }
    const uint32_t beforeDispatch = millis();
    gAct.preMs += beforeDispatch - eventStart;
    gApp->dispatch(ev);
    const uint32_t afterDispatch = millis();
    gAct.dispMs += afterDispatch - beforeDispatch;
    // THE PEEK CLOSED OR COMMITTED, and both answer Pop -- so this runs after the
    // dispatch that removed it, and gPeekCommitted (set from the BUTTON, above) is the
    // difference.
    //
    // FIRST OF THE POST-DISPATCH BLOCKS, AND AHEAD OF THE CROSSING DETECTOR BELOW ON
    // PURPOSE. A commit changes the Reader's chapter, and that detector is the one
    // place on the device that instruments a chapter change -- mark(), the [chapter]
    // line and the crossing save edge. Below it, a commit would be the single chapter
    // change that gets none of the three on the press that caused it, and would then
    // be attributed to whatever press came next. The old Contents jump sat below and
    // had exactly that wart; moving this above it costs nothing and closes it, with no
    // second copy of the instrumentation.
    if (gPeekOpen && gApp->top().id() == reader::ScreenId::Reader) {
      gPeekOpen = false;
      auto* rd = static_cast<reader::ReaderScreen*>(&gApp->top());
      const uint32_t t = millis();
      // TAKEN BACK BEFORE ANYTHING ELSE, because the commit below walks the chapter and
      // cannot without a stream.
      const bool back = rd->reacquireChapter();
      if (gPeekCommitted) {
        // GO HERE. goToPosition and not goToChapter: the reader may have paged several
        // pages into the panel, and page one would be right on the first page and wrong
        // everywhere after it. The return anchor is a HIGH-WATER MARK now, so a commit
        // FORWARD carries it to the destination and leaves no way back, while one
        // BACKWARD leaves it standing where the reader was -- see return_anchor.h, which
        // prices that against the way back the old departure rule nominally offered and
        // measurably kept for one press.
        const bool ok = back && rd->goToPosition(gPeekSpine, gPeekCursor);
        logf("[peek] GO HERE spine=%d block=%d line=%d: %s in %lums\n", gPeekSpine,
             gPeekCursor.block, gPeekCursor.line, ok ? "ok" : "REFUSED",
             (unsigned long)(millis() - t));
      } else {
        // CLOSE. NO seekTo, which is where this departs from the 08-24 spec: a rewind
        // costs what page you are ON -- ~1010 ms at page 99 and ~3 s deep in a chapter --
        // so on CLOSE it would cost more than committing. The page was never disturbed,
        // and the live builder is what restreamAtCurrentPage repairs in a quiet window.
        logf("[peek] CLOSE, reader %s in %lums\n", back ? "restored" : "NOT RESTORED",
             (unsigned long)(millis() - t));
      }
      gPeekCommitted = false;
      logFlush();
    }
    // MARK THE BOOK FINISHED, and this one is NOT down with Retry and Open.
    //
    // Those two are placed just above the mask refresh because that is all they need.
    // This handler also LEAVES a screen and sets both stale flags, and the three blocks
    // that answer for that are all below here: the book-closed scan (which owns
    // gReading.open and two more things a copy would have to keep), Home's rebuild, and
    // the Library's row refresh. Placed with Retry and Open instead, each of those
    // would miss by one press -- the overlay would dismiss onto a Library row still
    // reading its old percentage, which is the state the write just changed.
    if (gApp->finishRequested()) handleFinish();
    // AND THE DELETE IS HERE FOR THE SAME REASON, not down with Retry and Open: it
    // leaves a screen and sets both stale flags, and the blocks that answer for those
    // -- Home's rebuild and the Library's row refresh -- are both below this point.
    // Placed with Open instead, the Library would be repainted one press later, still
    // listing the book that has just been removed.
    if (gApp->deleteRequested()) handleDelete();
    // BESIDE THE DELETE AND FOR ITS REASON: the outcome is read off a screen
    // that is still on top, so this has to run on the dispatch's own pass,
    // before anything pops. See App::wifiRequested().
    if (gApp->wifiRequested()) handleWifi();
    // Between the dispatch and the mask refresh below, so the refresh sees
    // whatever screen the retry left on top -- on success that is a brand new App
    // rooted at Home, whose holds are not the SD-missing screen's.
    // A CHAPTER CROSSING IS THE ONE READER PATH THE MARKS DO NOT SEE. It happens
    // inside ReaderScreen::onGesture, which the shell only observes as a redraw --
    // and it used to be the most expensive thing the reader did, re-reading the
    // archive's central directory and the OPF. That is gone, but it was also the
    // suspected cause of a heap floor 27 KB below where it now sits, so the path
    // wants a stage line of its own rather than another round trip to find out.
    if (gApp->top().id() == reader::ScreenId::Reader) {
      const auto* rd = static_cast<const reader::ReaderScreen*>(&gApp->top());
      const int was = gLastChapter;
      if (rd->chapterIndex() != was) {
        gLastChapter = rd->chapterIndex();
        mark("chapter-opened");
        // WHICH BRANCH, AND WHAT IT COST. A small chapter is counted before its
        // first paint and a big one is not, and the eager side had no line -- so a
        // device reporting "the dash never appears and the page is slow" could not
        // say whether the count ran, or how long it took. `indexPending` is the
        // branch: false means the count already happened.
        logChapterOpen(rd, millis() - beforeDispatch);
        // A CROSSING IS ONE OF THE THREE SAVE EDGES. It is also the coarsest unit a
        // power cut can cost the reader, which is what makes saving per page turn
        // unnecessary rather than merely expensive.
        // THE PREVIOUS VALUE, not the one just stored. As written this tested the
        // chapter it had assigned a line above, which is an index and so always >= 0 --
        // a guard that could not refuse. It reads the departure now, so it means what
        // it says: a crossing FROM somewhere is a save edge, and the first observation
        // of a book is not. openBookAt records the opening chapter, so this cannot be
        // negative any more, and the test is kept because that is a fact about the open
        // path rather than about this one.
        if (was >= 0) saveReadingPosition("chapter");
      }
    }
    // LEAVING THE BOOK, which is the edge the user actually reported: going back to
    // the Library and returning lost the page. Saved BEFORE the screen is gone --
    // hence the ordering here, after the dispatch that popped it but reading the
    // position captured while it still stood.
    // THE BOOK IS CLOSED WHEN NO READER IS LEFT ON THE STACK -- not when one is no
    // longer on TOP, which is what this asked and which was wrong the moment the reader
    // menu existed. The menu and the contents are pushed ABOVE the Reader, so opening
    // the menu declared the book closed, cleared gReading.open, and with it the gate on
    // the priming block: pressing Contents then primed nothing, the factory refused
    // (correctly), and the device reported "opening Contents does nothing".
    //
    // Scanned rather than tracked: a depth count would be a second copy of the stack's
    // own shape, and the stack is three deep at most here. This WAS the scan, inline;
    // the Typography apply path needed the same walk twice more, so it is one function
    // now -- see readerOnStack().
    if (gReading.open && readerOnStack(*gApp) == nullptr) {
      gReading.open = false;
      // ...and the crossing detector forgets where it was. It outlives one book
      // otherwise, so opening a second book at the same spine index the first was left
      // on would suppress the next real crossing and its save edge. See gLastChapter.
      gLastChapter = -1;
      // ...and so do the idle walks, for exactly the same reason wearing the same
      // sign: page 0 of chapter 0 is a position both books have, and the one that
      // could not restream is not the one that is about to be opened (#45).
      gRestreamStuck.forget();
      gWarmStuck.forget();
      logf("[progress] book closed\n");
      logFlush();
    }
    // A CHOSEN CHAPTER, acted on AFTER the pop that Contents' GO returns. The screen
    // cannot open the panel itself: the Reader is already on the stack under it, and a
    // screen that reached down into the stack would be a second thing that knows how a
    // Reader is shaped -- so Contents answers popTo(Reader) and names the chapter, and
    // this opens the peek over it.
    //
    // Read BEFORE the dispatch would be too early (the choice is made by the press) and
    // reading it after the pop is too late (the screen is gone), so the spine is taken
    // off the Contents screen while it is still on top, just above.
    //
    // IT USED TO JUMP HERE, with goToChapter. The jump was safe -- goToChapter sets the
    // return anchor -- and the peek is what makes being WRONG about a chapter cheap.
    if (gPendingSpine >= 0 && gApp->top().id() == reader::ScreenId::Reader) {
      auto* rd = static_cast<reader::ReaderScreen*>(&gApp->top());
      const int want = gPendingSpine;
      gPendingSpine = -1;
      if (want != rd->chapterIndex()) {
        const uint32_t t = millis();
        // THE PANEL'S COLUMN, which is not the reading column -- that is the whole
        // design. Recomputed here rather than held, because the reader's own typography
        // may have changed since the book opened and peekMetrics reads two of its fields.
        reader::PageMetrics pm;
        gTheme.peekMetrics(gFrame->width(), gFrame->height(), *gFonts, gBody, gSettings, pm);
        pm.italic = &gItalic;
        gFactory.setPeekMetrics(pm);
        // WHICH SPINE ENTRY, AND NOTHING ELSE. This used to compute the book-wide
        // percentage at the peeked chapter and hand it over -- and the panel then held
        // that one figure while its chapter label followed the reader across a boundary,
        // because paging off either end of a peek crosses into the next spine entry. The
        // panel owns a ReaderScreen and therefore the book's byte spans, so it derives
        // the number from the chapter it is showing. See PeekScreen::percentHere.
        gFactory.setPeek(want);
        // THE READER LETS GO FIRST. A live chapter peaks at 69,884 bytes with a
        // 36,956-byte single allocation against a measured 45,840-byte floor, so two do
        // not fit -- and the peek is a second one. Released BEFORE the push, because the
        // push is what allocates the second chapter.
        //
        // Its page, index, cursor and anchor all survive, which is what lets App::render
        // draw the veiled page underneath with no decode at all.
        rd->releaseChapter();
        if (gApp->pushScreen(reader::ScreenId::Peek)) {
          gPeekOpen = true;
          gPeekCommitted = false;
          logf("[peek] open spine=%d in %lums (heap %u)\n", want,
               (unsigned long)(millis() - t), (unsigned)ESP.getFreeHeap());
        } else {
          // REFUSED, so put the Reader back and leave it standing. The reader is on their
          // own page with the chapter list gone -- nothing lost but the list, the page
          // untouched and the anchor unmoved. The only reachable cause is the card going,
          // which pollCardPresence owns.
          const bool back = rd->reacquireChapter();
          logf("[peek] REFUSED spine=%d, reader %s\n", want,
               back ? "restored" : "COULD NOT BE RESTORED");
        }
        logFlush();
      }
    }

    // TYPOGRAPHY APPLIED, after the pop that its BACK returns.
    //
    // Read BEFORE the dispatch would be too early -- the last step may be the press
    // being dispatched -- and after the pop the screen is gone, so the flag is set by
    // the sink and consumed here. `gSettings` is already current: the sink applied
    // every step as it happened.
    //
    // ORDERING IS LOAD-BEARING: dispatch, then apply, then paint, in one loop
    // iteration. Between the pop and this call the Reader's page and metrics describe
    // a layout that no longer exists, and a paint in that window would draw old line
    // positions in a new face.
    //
    // A READER ANYWHERE ON THE STACK, not on top, and that distinction is load-bearing
    // twice. From SETTINGS there is no Reader at all and nothing should be
    // re-paginated. From the reader MENU the pop lands on the menu, which is an
    // overlay -- App::render walks down to the topmost non-overlay, paints the Reader,
    // then paints the overlay over it -- so the Reader's stale page IS drawn on the
    // very next frame. "On top" would never fire there and that frame would be wrong.
    // AND NOT UNTIL THE PANEL IS GONE, which is the gate that makes the flag mean
    // "apply this" rather than "something changed". TypographyScreen::cycleFocused
    // commits on EVERY change press, so without this the Reader underneath would be
    // re-paginated once per press -- a chapter crossing's walk and a card write on
    // each one, while the screen doing the drawing is the panel and the page is not
    // visible at all. The whole design is that the walk is paid on the press that
    // LEAVES, where the user already expects the screen to change.
    //
    // Scanned rather than compared against the top, for the reason readerOnStack is:
    // the stack's shape is the stack's to answer, and the panel being pushed only at
    // the top is a fact about today's callers rather than a guarantee.
    bool typographyStanding = false;
    for (int i = 0; i < gApp->depth(); ++i)
      if (gApp->at(i).id() == reader::ScreenId::Typography) typographyStanding = true;
    if (gTypographyDirty && !typographyStanding) {
      gTypographyDirty = false;
      reader::ReaderScreen* rd = readerOnStack(*gApp);
      reader::PageMetrics m;
      gTheme.readerMetrics(gFrame->width(), gFrame->height(), *gFonts, gBody, gSettings, m);
      m.italic = &gItalic;
      // THE FACTORY LEARNS THE NEW COLUMN EITHER WAY. On the no-Reader route this is
      // the whole job: without it the next book opened would be laid out at the old
      // column, and the change would look as though it had not been saved.
      gFactory.setReaderMetrics(m);
      if (rd == nullptr) {
        logf("[typo] no reader open; column=%dx%d ppem=%d for the next book\n", m.columnW,
             m.columnH, gSettings.bodyPpem);
        logFlush();
      } else {
        const uint32_t t = millis();
        rd->relayout(m);
        logf("[typo] relaid column=%dx%d ppem=%d page=%d/%d in %lums\n", m.columnW,
             m.columnH, gSettings.bodyPpem, rd->pageIndex() + 1, rd->pageCount(),
             (unsigned long)(millis() - t));
        logFlush();
        // THE POSITION IS WORTH SAVING NOW. The cursor's `line` was just dropped and
        // the record stores the ppem and columnW the line was laid at, so writing it
        // here means the NEXT boot's fitOf grades against the new numbers and reads
        // Exact rather than Relaid a second time.
        //
        // `rd` IS PASSED EXPLICITLY, because the pop landed on the reader menu and the
        // Reader is one below the top -- the default lookup would answer Unchanged and
        // store nothing at all.
        saveReadingPosition("typography", rd);
      }
    }

    // BACK AT HOME WITH A NEWER POINTER OR A BOOK FEWER: rebuild it, so the reading
    // column shows the book that was just being read instead of the state Home was born
    // in, and the LIBRARY row counts what is on the card rather than what was.
    //
    // THE SECOND HALF OF THAT SENTENCE IS #43. A delete set no flag, so Home kept the
    // pre-delete count until something else happened to set one. The gate derives that
    // half from gSd.removals() instead, so this asks the card rather than asking whether
    // a caller remembered -- and buildHomeApp() re-stamps the counter, which is what
    // stops one delete asking for a rebuild on every iteration from here on.
    //
    // The whole App is replaced rather than the view model swapped, because the two
    // Home states have different FOCUS RINGS -- WithNone where a CONTINUE block
    // exists, Noneless where it does not -- and Focus::None is a construction-time
    // property. Only ever done at depth 1, where the root is the only screen and
    // there is nothing above it to lose.
    //
    // THE FOCUS IS CARRIED ACROSS. A rebuild would otherwise drop the user on
    // whatever row the fresh view model names, so pressing Back from the Library
    // would move a selection they did not touch. setFocus clamps, which is what makes
    // this safe across a ring that changed shape.
    if (gHomeRebuild.stale(gSd.removals()) && gApp->depth() == 1 &&
        gApp->top().id() == reader::ScreenId::Home) {
      // WHICH OF THE TWO IT WAS, read BEFORE buildHomeApp() clears the latch. "Home was
      // rebuilt" has two causes and they are worth telling apart on a device: a
      // position that moved, or a book that left. A rebuild reported with no cause is
      // how a gate that has started firing every iteration would look like one working.
      const bool position = gHomeRebuild.latched();
      const int was = gApp->top().focus();
      buildHomeApp();  // ...which is also what answers the gate; see buildHomeApp()
      gApp->top().setFocus(was);
      logf("[progress] Home rebuilt: %s\n", position
                                                ? "the reading position has moved"
                                                : "a book has left the card");
      logFlush();
    }

    // BACK ON THE LIBRARY AFTER READING: re-derive the rows' percentages, so the book
    // just closed stops saying NEW. AFTER the dispatch for the same reason Home's
    // rebuild is -- the pop is what put the Library back on top, and this asks what is
    // on top to decide whether to do anything.
    //
    // Gated on the Library being ON TOP rather than done as soon as the flag is set,
    // and that is the whole of what keeps it off the reader's critical path: the
    // position also saves on chapter crossings and in a 2 s quiet window WHILE READING,
    // and card work there is exactly what the quiet window exists to avoid. The Reader
    // is on top for all of those, so the first iteration that can consume this is the
    // pop out of the book.
    //
    // It refreshes rather than rescans: only `/.reader/state` changed, and the save has
    // just dropped the listing cache, so a rescan would pay a fresh `/books` listing --
    // ~600 ms on a 203-book card -- plus one per folder for the counts. See
    // LibraryScreen::refreshProgress.
    //
    // ON THE CRITICAL PATH DELIBERATELY, because the row has to be right the moment it
    // is painted, and the paint is this same press. Timed into the log for the reason
    // `ser=` and `[log] wrote` are: an instrument that hides its cost lets you
    // attribute it to the device.
    if (gLibraryStale && gApp->top().id() == reader::ScreenId::Library) {
      reader::LibraryScreen* lib = gFactory.library();
      // A null pointer here means no Library to refresh, so the flag is LEFT SET rather
      // than cleared: it costs a pointer test an iteration and it still fires for the
      // next Library, where clearing would lose the refresh outright.
      if (lib != nullptr) {
        const uint32_t t = millis();
        const bool ok = lib->refreshProgress();
        gLibraryStale = false;
        logf("[progress] Library rows re-read: %s in %lums\n", ok ? "ok" : "REFUSED",
             (unsigned long)(millis() - t));
        logFlush();
      }
    }
    if (gApp->retryRequested()) handleRetry();
    // Same placement and the same reason: the mask refresh below must see whatever
    // screen the open left on top.
    if (gApp->openRequested()) handleOpen();
    // The mask belongs to whatever screen is now on top, which a push or pop
    // just changed. Re-reading it here is what keeps a hold bound only where a
    // ring is drawn.
    syncRecognizer();
    // Where the user is now, for a wake to restore. An unchanged record is not
    // rewritten, so this is nearly free on an event that did not move the stack.
    saveWhereWeAre();

    gAct.postMs += millis() - afterDispatch;
  }

  // BEFORE THE IDLE SLEEP, because a flat device should say why it stopped rather
  // than showing the ordinary sleep screen. Both are [[noreturn]]; whichever runs
  // first is the one the user sees.
  //
  // FROM loop() AND NEVER FROM A DISPATCH: a paint cannot be interrupted, so the
  // shutdown's own paint must not run inside one. Same placement and the same
  // reason as the idle sleep below it.
  if (gBattery.level() == reader::BatteryLevel::Critical) criticalShutdown();

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
  // BEFORE THE PAINT, AND THAT ORDERING IS THE WHOLE OF A REPORTED BUG. It
  // ran after, so the picker was pushed, painted with the boot-primed EMPTY
  // list -- "No networks found" -- and only THEN did the first poll arm the
  // scan. The reader was told the scan had finished and found nothing before
  // it had started. Up here the scan is armed and `setScanning(true)` is set
  // in the same iteration as the push, so the first frame the picker ever
  // draws is the scanning one.
  pollWifi();

  const bool settled = static_cast<uint32_t>(millis() - gLastInputMs) >= kCoalesceMs;
  const bool painted = gApp->dirty() && settled;
  if (painted) {
    renderTop();
    gApp->clearDirty();
  }

  // THE INTERACTION, CLOSED AND REPORTED. Gated on nothing being owed to the panel
  // rather than on `painted`, so the two cases that are not a paint are still
  // reported rather than silently dropped:
  //   * a press that changed nothing -- a refused push, a Retry that failed, a
  //     gesture the screen answered none() to. It reads `paint=none`, and a press
  //     with no visible result is exactly the thing worth having a line for.
  //   * a press held back by the coalescing window, which stays pending until the
  //     paint it is waiting for actually happens.
  if (gAct.pending && !gApp->dirty()) {
    const uint32_t now = millis();
    const uint32_t total = now - gAct.at;
    const uint32_t ser = gLogMs - gAct.logAtStart;
    logf("[i] #%lu %s %s from=%s to=%s ev=%d | wait=%lu pre=%lu disp=%lu post=%lu "
         "render=%lu up=%lu wave=%lu | total=%lums ser=%lu net=%lu%s\n",
         (unsigned long)++gInteractionSeq, reader::buttonName(gAct.button),
         gAct.kind == reader::PressKind::Long     ? "LONG"
         : gAct.kind == reader::PressKind::Repeat ? "REPEAT"
                                                  : "SHORT",
         gAct.from, reader::screenName(gApp->top().id()), gAct.events,
         (unsigned long)(gAct.popped - gAct.at), (unsigned long)gAct.preMs,
         (unsigned long)gAct.dispMs, (unsigned long)gAct.postMs,
         (unsigned long)(painted ? gRenderMs : 0), (unsigned long)(painted ? gUploadMs : 0),
         (unsigned long)(painted ? gWaveMs : 0), (unsigned long)total, (unsigned long)ser,
         (unsigned long)(total >= ser ? total - ser : 0), painted ? "" : " paint=none");
    // Flushed AFTER the measurement is taken, so it cannot be part of it. This is
    // the line the analysis reads, and a reset that eats the last one loses the
    // interaction that most likely caused the reset.
    logFlush();
    gAct.pending = false;
  } else if (painted) {
    // A PAINT NOBODY PRESSED FOR, named rather than left to look like a missing
    // interaction: the first frame after boot, the card-lost rebuild, the deferred
    // page count's repaint. Same fields so the two line shapes parse alike.
    logf("[i] #-- (no press) to=%s | render=%lu up=%lu wave=%lu\n",
         reader::screenName(gApp->top().id()), (unsigned long)gRenderMs,
         (unsigned long)gUploadMs, (unsigned long)gWaveMs);
    logFlush();
  }

  // THE READING POSITION, SAVED BECAUSE THE READER MOVED RATHER THAN BECAUSE THEY LEFT.
  //
  // FIRST OF THE QUIET-WINDOW JOBS, AND THE ONLY ONE THAT PROTECTS DATA. The page
  // count, the refinement and the ring warm are a number, some grey and a latency;
  // this is the reader's place in the book. It is also by far the cheapest of the four
  // -- ~20-100 ms against 400 ms to 3.6 s -- so putting it in front of them costs them
  // little and buys the guarantee that a chapter-long count cannot sit between a page
  // turn and the record of it.
  //
  // GATED ON THE READER HAVING MOVED, which is the whole reason ProgressSaveGate
  // exists. This block is reached on EVERY loop iteration once the buttons go quiet,
  // and `savePosition` answers Unchanged by reading both sidecars back off the card
  // first -- so without the gate an idle device would sit on a page doing two file
  // reads per iteration, forever, on the panel's own SPI bus. Three int comparisons
  // replace all of it.
  //
  // AND ON THE CARD NOT HAVING REFUSED THREE TIMES. A card can be readable and refuse
  // writes, and `writeAll` calls noteCardGone() on a write that fails after opening,
  // which pollCardPresence turns into an App rooted at SdMissingScreen. Saving a
  // hundred times more often would make that a hundred times more likely, so the gate
  // gives up for the session after kGiveUpAfterFailures -- at which point the
  // behaviour is exactly the three edges that shipped.
  if (!gApp->dirty() && rawSamplesPending() == 0 && gReading.open &&
      static_cast<uint32_t>(millis() - gLastInputMs) >= kSaveQuietMs &&
      gApp->top().id() == reader::ScreenId::Reader) {
    // A DIFFERENT BOOK CAN SIT AT THE SAME COORDINATES. spine 0 / block 0 / line 0 is
    // the opening page of every book on the card, so without this the first page of a
    // newly opened book would look to the gate exactly like the page it last stored
    // for the previous one. Tracked here rather than hooked into the open path so the
    // whole mechanism stays inside this block.
    static std::string gateBook;
    if (gateBook != gReading.path) {
      gateBook = gReading.path;
      gSaveGate.forget();
    }
    const auto* rd = static_cast<const reader::ReaderScreen*>(&gApp->top());
    const reader::SavePoint where(rd->chapterIndex(), rd->currentCursor());
    if (gSaveGate.wants(where, millis())) {
      // ONE ACQUISITION FOR THE WHOLE SAVE. Each SdFileSystem call takes the guard
      // itself and it is recursive, so this changes no locking -- it holds the bus
      // across all four operations instead of releasing it three times in the middle
      // of a save, which is the same rule renderTop() applies to a paint.
      SpiBusGuard bus;
      const reader::SaveResult r = saveReadingPosition("page");
      if (r == reader::SaveResult::Failed) {
        gSaveGate.noteFailed(millis());
        // SAID OUT LOUD, AND THE GIVE-UP SAID SEPARATELY. The reader sees nothing
        // either way, and a durability feature that has switched itself off looks
        // exactly like one that is working -- which this file records as a defect
        // shape several times over. saveReadingPosition has already logged WHICH half
        // failed; this says what it means for the mechanism.
        if (gSaveGate.givenUp())
          logf("[progress] the card refused it %d times -- THE PER-TURN SAVE IS OFF for "
               "the rest of this session. Reading is unaffected and so are the "
               "leaving/chapter/sleep edges, which still try every time. A card that is "
               "readable but write-protected is the usual cause\n",
               gSaveGate.failures());
        else
          logf("[progress] the card refused it (%d of %d, retrying in %lus)\n",
               gSaveGate.failures(), reader::ProgressSaveGate::kGiveUpAfterFailures,
               (unsigned long)(reader::ProgressSaveGate::kRetryBackoffMs / 1000u));
        logFlush();
      } else {
        gSaveGate.noteStored(where);
      }
    }
  }

  // THE FOUR-LEVEL UPGRADE, once the buttons have been quiet and nothing is owed
  // to the panel. Placed here rather than after a dispatch because the whole point
  // is that it must NOT happen while the reader is still turning pages: a paint
  // cannot be interrupted, so refining between two turns would put its full cost
  // in front of the second one.
  //
  // `!gApp->dirty()` as well as the quiet window: a screen change already queued
  // supersedes the refinement, and renderTop clears the flag anyway.
  // THE PAGE COUNT FIRST, on its own shorter window -- see kCountQuietMs -- AND IT
  // PAINTS. This said "no paint: the number lands in the view model and shows on the
  // next thing that draws", and the device showed what that next thing really is.
  // Measured on a chapter crossing: the count finished 1.56 s after the press and the
  // 40 was not on glass until the REFINEMENT's paint at 6.34 s. The reasoning assumed
  // a page turn would come first and repaint it, but the refinement's 5 s window
  // almost always wins that race -- so "no extra waveform" bought nothing and cost
  // four seconds of a footer reading "1 / -" with the answer already in memory.
  //
  // So it repaints on the FAST path (one waveform, ~596 ms) and leaves the refinement
  // owed, which renderTop sets for a grayscale screen anyway. The number lands at
  // ~2.2 s and the four-level upgrade still arrives on its own schedule.
  if (!gApp->dirty() && rawSamplesPending() == 0 &&
      static_cast<uint32_t>(millis() - gLastInputMs) >= kCountQuietMs &&
      gApp->top().id() == reader::ScreenId::Reader) {
    auto* rd = static_cast<reader::ReaderScreen*>(&gApp->top());
    if (rd->indexPending()) {
      const uint32_t t = millis();
      // ABANDONED THE MOMENT A BUTTON IS PRESSED. The window above only makes the
      // count rarer; this is what stops it blocking the loop for the 2-3.6 s the
      // device measured. A capture-less lambda IS the `bool(*)(void*)` the count
      // takes -- see ReaderScreen::StopFn for why it is not std::function.
      const bool done = rd->completeIndex([](void*) { return rawSamplesPending() != 0; }, nullptr);
      mark("index-completed");
      // `done` IS IN THE LINE, because `pageCount()` on an abandoned count is the
      // OLD partial figure and the line would otherwise report it as the answer --
      // "a check that reports on less than it claims", which this file records as a
      // defect shape in three other places. An abandoned count is the normal case
      // now, not an error, so it needs to be legible rather than silent.
      logf("[index] %s pages=%d in %lums (deferred: chapter over %uB)\n",
           done ? "counted" : "abandoned", rd->pageCount(), (unsigned long)(millis() - t),
           (unsigned)reader::ReaderScreen::kEagerCountBytes);
      logFlush();
      // A press during the count wins: the page on glass is already correct -- the
      // count changes one number in the footer, not the text -- so getting out of the
      // way beats putting a ~596 ms paint in front of a page turn. The same rule
      // refineNow applies to itself, for the same reason.
      if (done && rawSamplesPending() == 0) renderTop();
    }
  }

  // `rawSamplesPending()` as well as the clock: a transition already queued means
  // the user is still going, and starting something the panel cannot interrupt in
  // front of it is the whole defect this window exists to avoid. The queue is
  // drained at the top of the loop, so anything here arrived during the paint.
  if (gRefineOwed && !gApp->dirty() && rawSamplesPending() == 0 &&
      static_cast<uint32_t>(millis() - gLastInputMs) >= kRefineQuietMs) {
    refineNow();
  }

  // PUTTING BACK THE STREAM THE COUNT SPENT -- see kRestreamQuietMs.
  //
  // WHERE IT SITS IN THE QUIET-WINDOW ORDER, AND WHY, because a job that just takes
  // a position rather than arguing for one makes the whole ordering accidental:
  //
  //   * AFTER BOTH COUNT SITES. completeIndex rewinds the stream and drops the
  //     builder, so a restream above it is a full walk thrown away by the very next
  //     block. BOTH, not one: there is the deferred site above and one inside
  //     refineNow(), and this file already records what fixing only one of them
  //     costs. Sitting below the refinement block puts this below both.
  //     IT IS AN EFFICIENCY CONSTRAINT AND NOT A CORRECTNESS ONE, which is worth
  //     knowing before anyone reorders these: the gate is `hasLiveStream()`, asked
  //     afresh every iteration, so a restream that ran above a count would waste one
  //     interruptible walk and be re-run ten milliseconds later. Wrong order, right
  //     state.
  //   * BEFORE THE RING WARM, because they are the SAME WALK with two gates
  //     (ReaderScreen::rewalkToCurrentPage), and a restream that lands leaves exactly
  //     the backward headroom a warm would have left -- so the warm below correctly
  //     finds nothing to do and the pair costs one rewind rather than two. Put the
  //     other way round, the warm's rewind would leave a live builder and this would
  //     no-op, which reaches the same state; but the warm refuses page 0 and needs
  //     spent headroom, so it is the narrower gate and must not be the one that
  //     decides whether the stream comes back.
  //   * ITS WINDOW IS THE SHORTEST OF THE GROUP and that is deliberate rather than
  //     impatient: it is the only one of these jobs that loses nothing when it is
  //     interrupted, so it is the only one that can afford to try early and often.
  //
  // NOT GATED ON `!gRefineOwed`, which the warm below is. The refinement is 1408 ms
  // and cannot be interrupted; this is interruptible at one block, so making it wait
  // for the refinement's 5 s window would cost it every chance it has -- `gRefineOwed`
  // is set by every grayscale paint, so it is true for essentially the whole time the
  // reader is on a page.
  if (!gApp->dirty() && rawSamplesPending() == 0 &&
      static_cast<uint32_t>(millis() - gLastInputMs) >= kRestreamQuietMs &&
      gApp->top().id() == reader::ScreenId::Reader) {
    auto* rd = static_cast<reader::ReaderScreen*>(&gApp->top());
    // ASKED HERE AS WELL AS INSIDE, so the common case -- a stream that stands, which
    // is every ordinary page turn -- costs a pointer test and no log line at all.
    // ...AND NOT WHERE THE WALK HAS ALREADY SAID IT CANNOT (#45 -- see IdleWalkStuck).
    // `hasLiveStream()` alone is a gate with no memory, so the last page of a chapter
    // re-ran this every iteration for as long as the reader sat there.
    if (!rd->hasLiveStream() && !gRestreamStuck.at(rd->chapterIndex(), rd->pageIndex())) {
      const uint32_t t = millis();
      const bool done = rd->restreamAtCurrentPage(
          [](void*) { return rawSamplesPending() != 0; }, nullptr);
      // AN EMPTY QUEUE AFTER THE CALL MEANS NOTHING INTERRUPTED IT, so a `false` here
      // is the walk's answer about this page rather than a press taking the loop
      // back. Asked immediately, before anything else can enqueue.
      const bool structural = !done && rawSamplesPending() == 0;
      if (structural) {
        gRestreamStuck.note(rd->chapterIndex(), rd->pageIndex());
      } else if (done) {
        gRestreamStuck.forget();
      }
      // LOGGED THOUGH NOTHING IS VISIBLE, for the reason [warm] is: an idle
      // optimisation that silently stops working looks exactly like one that is
      // working. `done` is the whole point of the line -- an abandoned restream is
      // free but it also did not help, and only the pair of counts says which is
      // happening. If this reads `abandoned` most of the time, kRestreamQuietMs is
      // too short for this reader; if it never appears at all, the stream is never
      // being spent and the count is not deferring.
      //
      // THE SUPPRESSED RETRIES PRINT NOTHING -- a line every 13 ms is the same spin
      // wearing a different hat, and on a card log it is what erases the history a
      // diagnostic session is collecting. But the FIRST failure at a position still
      // says so, and says that it is the last one: silence with no explanation is
      // exactly the shape this line exists to prevent.
      logf("[restream] %s page=%d in %lums%s\n", done ? "ready" : "abandoned",
           rd->pageIndex(), (unsigned long)(millis() - t),
           structural ? " (structural -- not retried at this page)" : "");
      logFlush();
    }
  }

  // THE READER'S LAST SLOW INTERACTION, MOVED OFF THE BUTTON.
  //
  // A backward turn that misses the page ring rewinds and decodes from the chapter
  // start, and that costs what page you are ON -- the device measured ~1010 ms at
  // page 99 of a 248 KB chapter, and it is worse deeper in. Nothing makes it
  // cheaper: a DEFLATE stream cannot be seeked, and a second one is a 32 KB window
  // against a 42 KB floor, which this file has already refused twice. So it is not
  // made cheaper, it is made to happen while the user is reading. A page takes ~23 s
  // to read and the rewind takes one to three; it fits.
  //
  // LAST OF THE THREE QUIET-WINDOW JOBS, deliberately: the page count puts a number
  // on the glass and the refinement puts grey on it, and both are things the user
  // can see. This one is invisible by construction, so it goes behind them.
  //
  // THE SAME WINDOW AS THE REFINEMENT, and it KEEPS it where the count no longer
  // does. Abandoning this one really does spend the live PageBuilder -- it is
  // reached with a stream standing, which is exactly what the restream above is not
  // -- so the cost of firing too early lands on the next FORWARD turn. That is the
  // asymmetry the two constants encode: kRestreamQuietMs is short because an
  // interrupted restream loses nothing, and this stays long because an interrupted
  // warm loses the stream.
  if (!gApp->dirty() && rawSamplesPending() == 0 && !gRefineOwed &&
      static_cast<uint32_t>(millis() - gLastInputMs) >= kRefineQuietMs &&
      gApp->top().id() == reader::ScreenId::Reader) {
    auto* rd = static_cast<reader::ReaderScreen*>(&gApp->top());
    // SIZED FROM THE HEAP THAT IS ACTUALLY THERE, every time, because it moves by
    // 34 KB depending on nothing but which button opened the book: a Reader reached
    // THROUGH THE LIBRARY has 203 books resident underneath it at ~59 KB and leaves
    // 42,152 free, where the same book through Home's CONTINUE leaves 76,476. A
    // constant would have to be sized for the first and would then waste the second.
    //
    // An eighth of what is free, at ~1.5 KB a page. The default of 3 is the floor,
    // so this can only ever raise it -- a heap under pressure keeps the behaviour
    // that shipped rather than getting something worse.
    const int affordable = static_cast<int>(ESP.getFreeHeap() / 8u / 1536u);
    rd->setPageCacheDepth(affordable);
    // THE SAME MEMORY THE RESTREAM ABOVE HAS, and for a sharper reason (#45): this
    // job REFUSES page 0 outright, so on page 0 the headroom gate below can never
    // stop being true and the walk is re-run for as long as the reader sits there.
    // On the device it interleaved 1:1 with the restream's own spin, forever.
    if (rd->backwardHeadroom() < rd->pageCacheDepth() - 1 &&
        !gWarmStuck.at(rd->chapterIndex(), rd->pageIndex())) {
      const uint32_t t = millis();
      const int was = rd->backwardHeadroom();
      const bool done = rd->warmPageRing([](void*) { return rawSamplesPending() != 0; }, nullptr);
      // AN EMPTY QUEUE AFTER THE CALL MEANS NOTHING INTERRUPTED IT -- see
      // IdleWalkStuck for why the two meanings of `false` must not share a fate. An
      // interrupted warm is the expensive one of the pair (it spends the live
      // builder), which is all the more reason it must stay retryable.
      const bool structural = !done && rawSamplesPending() == 0;
      if (structural) {
        gWarmStuck.note(rd->chapterIndex(), rd->pageIndex());
      } else if (done) {
        gWarmStuck.forget();
      }
      // LOGGED EVEN THOUGH NOTHING IS VISIBLE -- especially because nothing is
      // visible. An idle optimisation that silently stops working looks exactly like
      // one that is working, which this file records as a defect shape three times
      // over. depth/headroom is what says whether the ring is actually deeper. The
      // suppressed retries are silent and the first refusal at a position says that
      // it is the last one, for the reason [restream] does.
      logf("[warm] %s depth=%d headroom %d->%d in %lums (heap %u)%s\n",
           done ? "ready" : "abandoned", rd->pageCacheDepth(), was, rd->backwardHeadroom(),
           (unsigned long)(millis() - t), (unsigned)ESP.getFreeHeap(),
           structural ? " (structural -- not retried at this page)" : "");
      logFlush();
    }
  }

  // ONE GATE, AND IT IS LITERALLY ONE EXPRESSION -- deliberately not naming how
  // many things read it below, because that count has already gone stale once
  // (the card log's flush, then pollCardPresence, then the battery poll) and
  // will again. What matters is that each of them reads THIS local rather than
  // carrying its own copy of the condition that can drift away from it. The
  // card log's flush is specified as running "under the same gate the
  // card-presence poll uses", and the battery poll below follows the same rule
  // for a different bus.
  const bool quiet = !gApp->dirty() && rawSamplesPending() == 0;

  // THE CARD LOG'S IDLE FLUSH. This is the call kLogFlushAtBytes was declared for
  // and did not have: the tee filled the 4 KB buffer, logTee then began counting
  // the overflow as dropped, and the log grew a HOLE -- which is the one thing the
  // design note above says a diagnostic must never do, because a gap in the log is
  // indistinguishable from the device having gone quiet. Only the sleep path ever
  // wrote the card, so a device that was being used and had not yet slept lost
  // everything past the first 4 KB.
  //
  // THE THRESHOLD IS THE TRIGGER, NOT THE CEILING. Flushing at three quarters
  // leaves a kilobyte of headroom to carry the log across the gap between crossing
  // the threshold and the next quiet window, so the lines a burst drops are the
  // ones that would have overflowed anyway rather than the newest ones. It is also
  // what rate-limits this: a flush empties the buffer, so the next one cannot
  // happen until another 3 KB has been logged, and the ~40 ms card write can never
  // become the per-line cost the buffer exists to avoid.
  //
  // BEFORE the poll rather than after it, although either order is correct for the
  // bus: the poll can find the card gone and rebuild the App, and a 40 ms write
  // must not be sitting in front of the SD-missing screen that discovery owes the
  // user. It deliberately does NOT ask whether the card is believed present, for
  // appendToCard's own reason -- the reason we think it has gone is exactly the
  // kind of thing worth having in the log.
  //
  // AND IT REPORTS ITS OWN WEIGHT, for the reason `ser=` exists: an instrument
  // that hides its cost lets you attribute it to the device. The buffered figure is
  // read BEFORE the flush, which zeroes it.
  //
  // wantsFlush() ANSWERS THE ARMING TOO, so a card whose settings said no -- or a
  // boot that never got as far as reading them -- can never put a byte in
  // /encre.log. That is one question rather than the two this used to spell as
  // `gLogToCard && gLogLen >= ...`, and two spellings of one condition is how this
  // project has shipped a dead button twice.
  if (quiet && gCardLog.wantsFlush(kLogFlushAtBytes)) {
    // Recursive, and appendToCard takes one of its own -- same reason the poll
    // takes one below: the write and the line reporting it are one atomic use of
    // the bus rather than two that could straddle a paint.
    SpiBusGuard bus;
    const unsigned buffered = static_cast<unsigned>(gCardLog.size());
    bool landed = false;
    const uint32_t took = flushLogToCard(&landed);
    logf("[log] %s %uB in %lums\n", landed ? "wrote" : "COULD NOT WRITE", buffered,
         (unsigned long)took);
    logFlush();
  }

  // AFTER the paint block and only with nothing owed to the panel. The poll is
  // SPI traffic on the display's bus (see pollCardPresence), so a frame the user
  // is waiting for goes out first; and if the poll does find the card gone, the
  // fresh App it builds is dirty, so the SD-missing screen paints on the next
  // iteration ten milliseconds later.
  //
  // AND NOT WITH A PRESS ALREADY QUEUED, which is the same rule the refinement and
  // the deferred page count apply to themselves and for the same reason: this is
  // SPI work the user's next paint has to wait behind. The fast probe is a few
  // sector reads, so it is small -- but the FAT-scan backstop is measured at 14 s
  // on the user's own card, and putting that in front of a button press is the
  // difference between a device that feels slow and one that looks broken. The
  // backstop is normally disarmed (armCardProbes explains when it is not), so this
  // guard is for the case where it is the only card check there is.
  //
  // Deferring a probe costs at most one poll interval of detection latency on a
  // card that was pulled WHILE the user was pressing buttons -- and the press they
  // are making will hit the card itself soon enough.
  if (quiet) pollCardPresence(millis());

  // TWO JOBS OFF ONE TIMER, and the gates are what separate them. The timer, the
  // `quiet` gate and the I2C transaction are shared; what is NOT shared is who may
  // be refused. The safety ladder must be read on every screen and on both models,
  // so it sits in the outer block with no gate but the clock; the band's repaint
  // keeps the two gates it has always had, below.
  //
  // AND SINCE #96 THE TIMER'S INTERVAL IS ASKED FOR RATHER THAN FIXED. The two jobs
  // want different cadences -- the band's repaint has to feel immediate and its dwell
  // has to stay a run, while the ladder tolerates minutes for as long as the pack is
  // Normal -- so the tracker is asked which one is due, from the rung it is on plus
  // the one predicate that says whether the band's repaint could fire at all. See
  // BatteryTracker::pollIntervalMs(), which carries the derivation and the structural
  // argument that the critical dwell is never sampled slowly.
  //
  // IT IS ASKED FOR AFRESH AND NOT CACHED, deliberately: the rung and the screen both
  // change under this loop, and a remembered interval would be a second copy of a
  // state that is a load and a virtual call away.
  //
  // AND IT SITS LAST IN THE CONDITION rather than in a local above it, so `quiet`
  // short-circuits it. `bandRepaintPossible()` reaches App::top(), which is a virtual
  // id() -- nothing beside a 439 ms panel, but this loop runs every 10 ms and a
  // battery-life change that spent a virtual call per iteration to save an I2C
  // transaction per 30 s would be an odd trade to make silently.
  //
  // Same gate as pollCardPresence -- after the paint block, nothing owed to the
  // panel -- but for a different reason: this needs no SpiBusGuard, because it is
  // I2C on the sensor bus and cannot race a refresh. What the gate buys is only
  // that a repaint it asks for does not jump a frame the user is waiting for.
  if (quiet && static_cast<uint32_t>(millis() - gLastBatteryPollMs) >=
                   gBattery.pollIntervalMs(bandRepaintPossible())) {
    gLastBatteryPollMs = millis();
    ++gBatteryPolls;
    // THE LADDER FIRST AND UNCONDITIONALLY. It is the safety mechanism and must not
    // sit behind either of the band's two gates.
    pollBatteryLevel();
    // Immediately after, so the edge is tested against the reading just taken.
    armBannerIfNewlyLow();

    // THE BAND'S REPAINT, still behind its own two gates -- what it drives is the
    // bolt on Home, which is a Home question. refreshBatteryOnHome takes its own
    // reading; that is one extra I2C transaction on Home only, and one call site
    // that cannot disagree with itself is worth ~150 us (readBattery's own
    // comment).
    //
    // PLUGGING IN SHOULD SHOW THE BOLT WITHOUT A BUTTON BEING PRESSED, and nothing
    // else will make that happen: e-ink holds its image, no input arrives, and
    // Home's view model is otherwise only rebuilt at boot, on a wake and on a Back
    // out of a book.
    //
    // UNPLUGGING HAS TO REACH THE GLASS TOO, and the first version of this did not.
    // It fired on a rising edge only, on the stated grounds that a stale bolt would
    // be corrected by the next Home paint -- which assumed a button press that never
    // came. Reported off the device: the bolt appeared on plug-in and then stayed
    // for ever. A mark claiming the device is charging when it is not is the same
    // class of lie as a 0% for a gauge that did not answer.
    //
    // Skipped entirely where charging cannot be observed, which is every X4. That
    // condition is bandRepaintPossible(), the SAME predicate the interval above was
    // chosen with -- so the cadence and the repaint cannot disagree about whether this
    // can fire, which since #96 is what keeps the fast interval from being held for a
    // repaint that could never happen.
    // BatteryTracker owns everything that makes this safe: an edge in either
    // direction, a first reading that seeds without firing, a latch that clears only
    // after 60 s of continuous not-charging, and three grants a session. The dwell is
    // what stops a device sitting at 100% on the charger -- where the gauge's
    // Current() sign dithers around zero -- repainting the panel all night, and it is
    // also what tells a real unplug from that dither, which is why CLEARING the bolt
    // rides the same timer rather than a second constant.
    if (bandRepaintPossible() && refreshBatteryOnHome()) {
      gApp->markDirty();
      // WHICH EDGE, because both grant a repaint now and a line that says only
      // "charging" would misreport half of them -- on glass this is the one
      // record of what the panel was asked to do and why.
      logf("[battery] charging=%d -> repainting Home\n", gBattery.charging() ? 1 : 0);
      logFlush();
    }
  }

  static uint32_t beat = 0;
  if (++beat % 200 == 0) {
    // minHeap is the HIGH-WATER mark, and it is the number Phase 3 actually needs:
    // `heap` is whatever is free at this instant, while a pagination peak happens
    // BETWEEN two [alive] lines and would otherwise never be seen. 3A's entire
    // memory case rested on a figure nothing was measuring.
    // THE LISTING CACHE IS ON THIS LINE BECAUSE A HIT IS OTHERWISE INVISIBLE.
    // A miss prints `[fs] list ... 590ms`; a hit prints nothing at all, which on a
    // device reads exactly like the call never having happened -- so "it got
    // faster" and "it stopped being called" look identical, and this file records
    // that shape as a defect three times over. hits/misses is what tells a cache
    // that is working from one that is merely quiet.
    // BATTERY, ON THE SAME LINE AND FOR THE SAME REASON AS listings= ABOVE. The
    // poll's only other trace is the one-shot [battery] boot line and an
    // occasional "-> repainting Home", so a disarmed poll, one pinned by
    // kMaxGrantsPerSession and one quietly seeing no change all look identical
    // otherwise -- indistinguishable from "I plugged in and nothing happened."
    // observable/pct/charging are read straight off gChargingObservable/gBattery
    // rather than re-derived, so this can never disagree with what setBattery()
    // just handed the screen.
    //
    // level= IS THE SAFETY LADDER'S OWN RUNG, and it matters here more than the
    // rest: a ladder that has quietly stopped being polled and one that is fine
    // read IDENTICALLY, because nothing happens in either case. Without this a
    // shutdown that never came, and one that came with no [power] CRITICAL line
    // in front of it, would both be unattributable. Straight off level() -- 0
    // Normal, 1 Low, 2 Critical -- never re-thresholded from pct, which would be
    // a second spelling of the ladder free to disagree with the first.
    //
    // pollMs= IS THE CADENCE THAT IS CURRENTLY DUE (#96), and polls= cannot stand
    // without it: there are two intervals now, 15x apart, so a device wrongly pinned
    // to the fast one and a device correctly on the slow one differ in polls= and in
    // NOTHING ELSE that reaches a log. Read straight off pollIntervalMs() with the
    // same predicate the loop uses, never re-derived from level= -- which would be a
    // second spelling of the choice, free to disagree with the one actually made.
    logf("[alive] last-stage=%s heap=%u minHeap=%u screen=%s depth=%d "
         "dropped=%lu/%lu listings=%u slots/%uB hit=%u miss=%u "
         "battery observable=%d pct=%d charging=%d level=%d polls=%lu pollMs=%lu\n",
         stage, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
         reader::screenName(gApp->top().id()), gApp->depth(),
         (unsigned long)rawSamplesDropped(), (unsigned long)gPresses.dropped(),
         (unsigned)gSd.listings().slotsHeld(), (unsigned)gSd.listings().residentBytes(),
         (unsigned)gSd.listings().hits(), (unsigned)gSd.listings().misses(),
         (int)gChargingObservable, gBattery.percent(), (int)gBattery.charging(),
         (int)gBattery.level(), (unsigned long)gBatteryPolls,
         (unsigned long)gBattery.pollIntervalMs(bandRepaintPossible()));
    // WHAT THE CARD LOG HAS COST AND WHAT IT HAS LOST, on the heartbeat rather than
    // per flush. `dropped` non-zero means the buffer overran between two idle
    // windows and the log has a HOLE in it -- which must never be mistaken for the
    // device having gone quiet. `sdMs` is the instrument's own weight; subtract it
    // before believing any total measured with logging on.
    if (gCardLog.enabled())
      logf("[log] buffered=%uB dropped=%luB sdTotal=%lums\n", (unsigned)gCardLog.size(),
           (unsigned long)gCardLog.dropped(), (unsigned long)gLogSdMs);
    logFlush();
  }
  // IDLE ON THE QUEUE, NOT ON THE CLOCK. Identical to the delay(10) this replaces
  // when nothing arrives, and the difference is the whole point when something
  // does: an edge queued one millisecond into a delay() sat there for the other
  // nine before this loop looked at it. A peek returns the moment the input task
  // posts, so the ~5 ms this used to average in front of every press is gone --
  // and the 10 ms ceiling is unchanged, which is what keeps tick(), the idle
  // timer and both quiet windows running at the cadence they were tuned at.
  waitForRawSample(10);
}
