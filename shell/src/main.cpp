#include <Arduino.h>
#include <BoardConfig.h>
#include <EInkDisplay.h>
#include <InputManager.h>
#include <PowerManager.h>
#include <SPI.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <Preferences.h>  // esp_restart(), for the RETRY-after-a-pull branch
#include <XteinkDetect.h>

#include <cstdarg>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <vector>

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
#include "reader/booklist.h"
#include "reader/font_manifest.h"
#include "reader/fontset.h"
#include "reader/framebuffer.h"
#include "reader/input.h"
#include "reader/json.h"
#include "reader/power.h"
#include "reader/profile.h"
#include "reader/progress.h"
#include "reader/refresh.h"
#include "reader/screen_home.h"
#include "reader/screen_sd_missing.h"
#include "reader/book.h"
#include "reader/screen_reader.h"
#include "reader/layout.h"
#include "reader/scalablefont.h"
#include "reader/reading_store.h"
#include "reader/screen_sleep.h"
#include "reader/screen_contents.h"
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
// AND IT STAYS AT THE REFINEMENT'S NUMBER NOW THAT THE COUNT IS INTERRUPTIBLE, which
// is the opposite of what interruptibility first suggests. The argument for putting
// it back to 1200 ms is that abandoning is now nearly free -- one block, ~6 ms -- so
// the reason for widening it has gone. It has not, and the reason is a cost that
// belongs to the press AFTER the one that interrupted:
//
//   ReaderScreen::completeIndex resets `pb_` BEFORE it walks, because the walk
//   rewinds the ChapterReader the builder reads from. An abandoned count restores
//   `starts_`, `at_` and the page -- but not the builder, and there is no second
//   stream to rebuild it from (that is another 32 KB inflate window against a
//   42,152-byte floor). So the next FORWARD turn misses the page ring, which holds
//   pages already visited and not the one ahead, and pays a full seekTo: ~376 ms on
//   the device against ~20 ms with the stream standing.
//
// So a short window does not cost the interrupting press any more; it costs the one
// after it, once per abandon. At 1200 ms a reader who pauses to think and then turns
// the page pays that routinely, and every abandon also throws away the walk it had
// done. At 5000 ms the count runs when the reader has really stopped -- and a reader
// spends ~23 s on a page, so it gets its chance -- and usually completes, which
// leaves a live builder behind it.
//
// WHAT WOULD ACTUALLY EARN THE SHORTER WINDOW is re-establishing the spent stream in
// a later quiet window, so an abandon costs nothing at all. That needs a
// needStream-only entry point on ReaderScreen and a call site here; it is the honest
// version of this trade and it is not written yet.
constexpr uint32_t kCountQuietMs = kRefineQuietMs;
static bool gRefineOwed = false;

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
// which is the property ScalableFont's fixed budget exists to give. Against a
// measured heap floor of 45,840 bytes with a page on glass, taking 16 KB here for a
// face that sets 3% of the text would have been the easy wrong answer.
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
// So it is set exactly when the thing Home draws has changed: a reading position was
// saved. Nothing else on the device moves that block.
static bool gHomeStale = false;

// The spine Contents chose, or -1. Held for exactly one dispatch: the choice is made
// while Contents is on top and acted on once the pop has put the Reader back.
static int gPendingSpine = -1;

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

static void logf(const char* fmt, ...) {
  const uint32_t t0 = millis();
  va_list args;
  va_start(args, fmt);
  // vprintf rather than a formatted buffer: Print::printf builds into a stack
  // buffer of its own and this part is not what costs anything.
  char line[512];
  const int n = vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  if (n > 0) Serial.write(reinterpret_cast<const uint8_t*>(line),
                          static_cast<size_t>(n) < sizeof(line) ? static_cast<size_t>(n)
                                                                : sizeof(line) - 1);
  gLogMs += millis() - t0;
}

// The flush half, timed the same way. SEPARATE FROM logf ON PURPOSE: this file has
// 92 print sites and 63 flushes, so folding the flush into logf would ADD one at
// the 29 sites that deliberately do not have it -- a behaviour change smuggled in
// under a measurement change, and a slower device than the one being measured.
// Every existing Serial.flush() became one of these and nothing else moved.
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

// Push `gSettings` into the two objects that act on it. Factored out of
// loadAndApplySettings because the Settings SCREEN needs exactly this and nothing
// else: it has already changed the struct, and re-reading the file would undo the
// change it is trying to make.
static void applySettings() {
  gRefresh.setCadence(gSettings.fullRefreshEvery);
  gRefresh.setFullOnTransition(gSettings.fullOnTransition);
  gIdle.setTimeout(gSettings.sleepAfterMs);
}

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
    gSettings = s;
    applySettings();
    // The factory holds a COPY, because it is what constructs the screen and the
    // screen is handed its starting values. Without this, closing Settings and
    // reopening it would show the values from before the change -- the struct
    // would be right, the refresh policy would be right, and the screen would be
    // the one thing still lying.
    gFactory.setSettings(gSettings);
    const bool wrote = reader::saveSettings(gSd, gSettings);
    logf("[settings] sleepAfterMs=%lu fullRefreshEvery=%d fullOnTransition=%d -> %s\n",
         (unsigned long)gSettings.sleepAfterMs, gSettings.fullRefreshEvery,
         (int)gSettings.fullOnTransition,
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
  logf("[boot] settings in force: sleepAfterMs=%lu fullRefreshEvery=%d "
       "fullOnTransition=%d\n",
       (unsigned long)gSettings.sleepAfterMs, gSettings.fullRefreshEvery,
       (int)gSettings.fullOnTransition);
  logFlush();
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
// to rebuild itself: gHomeStale puts this on the critical path of a Back from a
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
  logf("[library] counted %d book(s) in %s in %lums\n", gLibraryCount,
       reader::kBooksRoot, (unsigned long)(millis() - t0));
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
  if (books > 0 && gStorageUsable) {
    reader::LastRead last;
    if (reader::loadLastRead(gSd, last)) {
      if (!gSd.exists(last.bookPath)) {
        logf("[progress] the last book is gone from the card: %s\n",
             last.bookPath.c_str());
      } else {
        vm = reader::demoHomeVm();     // the reading-column shape, then every field
        vm.nothingToContinue = false;  // ...replaced, because none of it is this book
        vm.title = last.title.empty() ? last.bookPath : last.title;
        vm.author = last.author;
        vm.percent = last.percent;
        // THE BOARD'S COUNTER: spine position of spine count, which is what
        // Main.dc.html draws now -- a page counter for the book would mean
        // paginating all of it. A book whose count is unknown says just the
        // position rather than inventing a total.
        char label[24];
        if (last.spineCount > 0)
          std::snprintf(label, sizeof(label), "CH. %02d OF %d", last.spine + 1,
                        last.spineCount);
        else
          std::snprintf(label, sizeof(label), "CH. %02d", last.spine + 1);
        vm.chapterLabel = label;
        vm.focusedMenuIndex = -1;  // the CONTINUE block, which exists again
        vm.hints = {"READ", "SELECT", "UP", "DOWN"};
        logf("[progress] Home continues \"%s\" at %d%%, spine %d of %d\n",
             vm.title.c_str(), last.percent, last.spine + 1, last.spineCount);
      }
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
// SAVE WHERE THE READER IS, to the card, if a book is open.
//
// Called on the three edges that change the answer: leaving the book, crossing into
// another chapter, and going to sleep. NOT on every page turn -- a turn is ~570 ms
// of panel and a card write on top of each one would be felt, and the three edges
// above already bound how much reading a power cut can lose to one chapter.
//
// A FAILURE HERE IS LOGGED AND NOTHING ELSE, which is the one hazard in this whole
// feature. A card can be readable and refuse writes -- a physical write-protect tab
// does exactly that -- and writeAll calls noteCardGone() when a write it had already
// opened goes wrong, which pollCardPresence turns into an App rooted at
// SdMissingScreen. So treating a failed save as an error to act on would throw the
// reader out of a book they can still perfectly well read. There is nothing to do
// about it and nothing worth telling the user, so it goes in the log and the reader
// keeps reading.
static void saveReadingPosition(const char* why) {
  if (!gReading.open || gApp == nullptr) return;
  if (gApp->top().id() != reader::ScreenId::Reader) return;
  const auto* rd = static_cast<const reader::ReaderScreen*>(&gApp->top());

  reader::ReadingPosition p;
  p.bookPath = gReading.path;
  p.spine = rd->chapterIndex();
  const reader::Cursor at = rd->currentCursor();
  p.block = at.block;
  p.line = at.line;
  p.bookBytes = gReading.bytes;
  // THE GEOMETRY THE LINE WAS MEASURED AT, which is what makes `line` reusable or
  // not. Read from the live metrics rather than assumed, so a future type-size
  // setting invalidates exactly the field it should.
  p.ppem = reader::kBodyPpem;
  p.columnW = gFactory.readerMetrics().columnW;
  // The percentage goes IN the sidecar, so the Library can show it per row without
  // opening every book's archive to recompute one. Computed once, just below, and
  // shared with the Home pointer.
  p.percent = reader::progressPercent(gFactory.readerBook(), rd->chapterIndex(), rd->vm().page,
                                      rd->vm().pageTotal);
  // THE CHAPTER'S NAME AS THE READER SEES IT, which is the header's own label -- so a
  // book with no contents stores the `CH. 08` fallback and Book details' "Current story"
  // says that, rather than inventing a name or leaving the row blank.
  p.chapter = rd->vm().chapter;
  // THE WAY BACK, riding this record's edges and adding none of its own. Losing an
  // anchor to a power cut costs a shortcut and nothing else -- the reader is still
  // sitting on a real page -- so it does not justify a write on an edge that does not
  // already take one.
  if (rd->anchor().isSet()) {
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
  last.spineCount = rd->chapterCount();
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
  // Home now has something different to say, whether or not the card took the write:
  // the pointer in hand is newer than the one Home was built from either way.
  gHomeStale = true;
  logf("[progress] %s: spine=%d block=%d line=%d %d%% -- position %s, pointer %s\n", why,
       p.spine, p.block, p.line, last.percent, outcome(a), outcome(b));
  logFlush();
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
constexpr uint32_t kStatusAfterMs = 1000;

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
    const reader::PositionFit fit = reader::fitOf(saved, path, bookBytes, reader::kBodyPpem,
                                                  gFactory.readerMetrics().columnW);
    const reader::PositionRestore r = reader::restoreFrom(saved, fit);
    static const char* kFitWord[] = {"exact", "relaid", "rebound", "unusable"};
    logf("[progress] found a position for this book: spine=%d block=%d line=%d, fit=%s\n",
         saved.spine, saved.block, saved.line,
         kFitWord[static_cast<int>(fit)]);
    logFlush();
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
    const bool tocOk = reader::loadToc(gSd, path, gReading.toc, &tocWhy);
    logf("[toc] %s: %u entries in %lums, heap %u -> %u, min %u%s%s\n",
         tocOk ? "read" : "REFUSED", (unsigned)gReading.toc.size(),
         (unsigned long)(millis() - tocT), (unsigned)tocHeap,
         (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
         tocWhy[0] != '\0' ? " -- " : "", tocWhy);
    logFlush();
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
  // ...and the way back, or explicitly NONE. Cleared rather than left alone: the
  // factory outlives one book, so a stale anchor from the previous one would offer
  // this reader a page in a book they closed.
  if (haveAnchor) {
    gFactory.setReaderAnchor(startAnchor);
  } else {
    gFactory.clearReaderAnchor();
  }
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

static void renderTop() {
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
  gTheme.readerMetrics(logicalW, logicalH, fonts, gBody, readerMetrics);
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
      // The budget is already handled AFTER setup's first paint (see
      // skipInitialResync below, "the panel now holds a frame we just wrote"). This
      // is the same assertion made one paint earlier, and on a wake it is a
      // DIFFERENT and weaker claim, which is the part to understand before touching
      // it:
      //
      //   * The glass holds the SLEEP SCREEN -- e-ink keeps its image with no power.
      //   * The CONTROLLER's DTM1 baseline does not survive; after the reset it is
      //     whatever the RAM powered up as. skipInitialResync asserts it is valid,
      //     so the DU below diffs against that.
      //   * CLAUDE.md records this exact call producing "a split second of noisy
      //     banding on every wake" -- but that was a differential onto a WHOLE NEW
      //     SCREEN. Here the frame being painted is the sleep screen with one line
      //     changed, so almost every pixel the garbage baseline calls unchanged
      //     really is unchanged, and keeping what the glass holds is correct.
      //
      // IF THAT IS WRONG ON GLASS the symptom is specific and worth naming: the
      // WAKING line faint, banded, or absent, with the rest of the card intact. The
      // fallback is one line -- move skipInitialResync() to AFTER the paint and drop
      // requestResync(). The wake then flashes once, here, instead of once at Home,
      // which is still better than the two it started with.
      display.skipInitialResync();
      const reader::SleepViewModel vm = sleepVmFromCard(reader::kStatusWaking);
      reader::SleepScreen scr(vm);
      gFrame->clear(true);
      scr.render(*gFrame, *gFonts, gTheme, reader::Plane::Bw);
      gFrameContentsUnknown = true;
      // FAST, so this is a DU and the badge's words change without a flash.
      showOnePass(reader::RefreshMode::Fast);
      // ...and the NEXT paint is the strong one. Home is a whole new screen over a
      // baseline we have just admitted we do not know, so it takes the GC -- which
      // is both the honest refresh and the one that clears anything the DU above got
      // wrong. This is the flash a screen change is allowed to have.
      display.requestResync();
      mark("waking-painted");
    }
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
                            "or a focused row is no longer in its list)");
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
    // The board's `6% - CH. 01`, from the two facts the pointer has. A chapter NAME
    // would need a table of contents, which is not built -- the same reason the
    // Reader's own footer says a bare `CH. 03`.
    // THE LITERAL IS SPLIT, and it has to be: a C++ hex escape is UNBOUNDED, so
    // "\xC2\xB7CH." parses \xB7C as one value -- clang rejects it outright and the
    // ESP32's GCC accepted it as something that is not U+00B7. This project already
    // recorded the same trap once ("\xA0b" is 0xA0B); adjacent literals end the escape.
    char line[32];
    std::snprintf(line, sizeof(line), "%d%%\xC2\xB7" "CH. %02d", last.percent,
                  last.spine + 1);
    vm.progress = line;
  }
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

static void paintSleepScreen() {
  const reader::SleepViewModel vm =
      sleepVmFromCard(std::string("ASLEEP") + "\xC2\xB7" + "PRESS POWER TO WAKE");

  reader::SleepScreen scr(vm);
  gFrame->clear(true);
  scr.render(*gFrame, *gFonts, gTheme, reader::Plane::Bw);
  gFrameContentsUnknown = true;
  logf("[power] sleep screen: %s\n",
       vm.nothingToContinue ? "the badge alone, nothing open" : vm.title.c_str());
  logFlush();
  // FULL, not fast: this is the last thing the panel is asked to do for hours and a
  // differential update would leave the previous screen's residue under it.
  showOnePass(reader::RefreshMode::Full);
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
  saveReadingPosition("sleep");
  paintSleepScreen();
  logf("[power] sleeping from screen=%s; the record should name it on wake. Wake with "
       "the power button\n",
       reader::screenName(gApp->top().id()));
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
                                              rd->vm().page, rd->vm().pageTotal));
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
                               rd.vm().pageTotal)) +
                           "%";
            }
          }
          gFactory.setDetailsFacts(std::move(f));
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
    const uint32_t beforeDispatch = millis();
    gAct.preMs += beforeDispatch - eventStart;
    gApp->dispatch(ev);
    const uint32_t afterDispatch = millis();
    gAct.dispMs += afterDispatch - beforeDispatch;
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
      static int lastChapter = -1;
      if (rd->chapterIndex() != lastChapter) {
        lastChapter = rd->chapterIndex();
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
        if (lastChapter >= 0) saveReadingPosition("chapter");
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
    // own shape, and the stack is three deep at most here.
    bool readerOnStack = false;
    for (int i = 0; i < gApp->depth(); ++i)
      if (gApp->at(i).id() == reader::ScreenId::Reader) readerOnStack = true;
    if (gReading.open && !readerOnStack) {
      gReading.open = false;
      logf("[progress] book closed\n");
      logFlush();
    }
    // A CHOSEN CHAPTER, acted on AFTER the pop that Contents' GO returns. The screen
    // cannot jump the Reader itself: the Reader is already on the stack under it, and
    // pushing a second one would leave the first below with its own position -- so
    // Contents answers popTo(Reader) and names the chapter, and this moves it.
    //
    // Read BEFORE the dispatch would be too early (the choice is made by the press) and
    // reading it after the pop is too late (the screen is gone), so the spine is taken
    // off the Contents screen while it is still on top, just above.
    if (gPendingSpine >= 0 && gApp->top().id() == reader::ScreenId::Reader) {
      auto* rd = static_cast<reader::ReaderScreen*>(&gApp->top());
      const int want = gPendingSpine;
      gPendingSpine = -1;
      if (want != rd->chapterIndex()) {
        const uint32_t t = millis();
        const bool ok = rd->goToChapter(want);
        logf("[toc] jump to spine %d: %s in %lums\n", want, ok ? "ok" : "REFUSED",
             (unsigned long)(millis() - t));
        logFlush();
      }
    }

    // BACK AT HOME WITH A NEWER POINTER: rebuild it, so the reading column shows the
    // book that was just being read instead of the state Home was born in.
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
    if (gHomeStale && gApp->depth() == 1 && gApp->top().id() == reader::ScreenId::Home) {
      const int was = gApp->top().focus();
      buildHomeApp();
      gApp->top().setFocus(was);
      gHomeStale = false;
      logf("[progress] Home rebuilt with the current reading position\n");
      logFlush();
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
  // THE SAME WINDOW AS THE REFINEMENT, and for the reason the count's constant sets
  // out at length: abandoning spends the live PageBuilder, so the cost of firing too
  // early lands on the next FORWARD turn rather than on the press that interrupted.
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
    if (rd->backwardHeadroom() < rd->pageCacheDepth() - 1) {
      const uint32_t t = millis();
      const int was = rd->backwardHeadroom();
      const bool done = rd->warmPageRing([](void*) { return rawSamplesPending() != 0; }, nullptr);
      // LOGGED EVEN THOUGH NOTHING IS VISIBLE -- especially because nothing is
      // visible. An idle optimisation that silently stops working looks exactly like
      // one that is working, which this file records as a defect shape three times
      // over. depth/headroom is what says whether the ring is actually deeper.
      logf("[warm] %s depth=%d headroom %d->%d in %lums (heap %u)\n",
           done ? "ready" : "abandoned", rd->pageCacheDepth(), was, rd->backwardHeadroom(),
           (unsigned long)(millis() - t), (unsigned)ESP.getFreeHeap());
      logFlush();
    }
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
  if (!gApp->dirty() && rawSamplesPending() == 0) pollCardPresence(millis());

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
    logf("[alive] last-stage=%s heap=%u minHeap=%u screen=%s depth=%d "
         "dropped=%lu/%lu listings=%u slots/%uB hit=%u miss=%u\n",
         stage, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
         reader::screenName(gApp->top().id()), gApp->depth(),
         (unsigned long)rawSamplesDropped(), (unsigned long)gPresses.dropped(),
         (unsigned)gSd.listings().slotsHeld(), (unsigned)gSd.listings().residentBytes(),
         (unsigned)gSd.listings().hits(), (unsigned)gSd.listings().misses());
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
