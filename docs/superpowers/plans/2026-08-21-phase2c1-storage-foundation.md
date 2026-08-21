# Phase 2C-1 — Storage Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the firmware a filesystem it can read and write, a settings file
that survives a reboot, an honest screen for when the SD card is missing, and the
ability to wake up where you left off.

**Architecture:** `core/` gets a `FileSystem` interface and never learns what
backs it. Three implementations: an in-memory fake for unit tests, a host one for
the simulator and desktop tests, and an SdFat one in the shell. Settings are a
flat JSON file on the card. The last screen survives a wake through NVS, not the
card, because a wake must work with no card in the slot.

**Tech Stack:** C++20, doctest, CMake (desktop) / PlatformIO + Arduino-ESP32
(device), freeink-sdk `SDCardManager` (SdFat), ESP32 `Preferences` (NVS).

---

## Why this is its own plan

2C's scope is Library over real files, the settings store and screen, Sleep,
SD-missing, and Home's empty and missing-book variants — plus six items 2B
deferred. That is more than one plan's worth, and it splits cleanly:

- **2C-1 (this plan)** — storage, settings, SD-missing, wake restore.
- **2C-2** — Library over real files, item actions, delete confirm, book details.
- **2C-3** — Settings screen, Sleep screen, Home's empty and missing variants,
  and deleting `StubScreen` / `renderStub`.

Everything in 2C-2 and 2C-3 reads or writes through what this plan builds, so it
goes first. It ends with working software: the device mounts the card, reads and
writes its settings, shows a real designed screen when there is no card, and
resumes the screen you left.

## Design decisions this plan locks in

### 1. `core/` gets an interface, not a filesystem

The prime directive is that `core/` compiles on a desktop with no Arduino
dependency. So `core/` declares `FileSystem` and every screen and store talks to
it. Three implementations:

| Implementation | Lives in | For |
|---|---|---|
| `FakeFileSystem` | `test/unit/` | Unit tests. In-memory, no disk, injectable failures. |
| `HostFileSystem` | `core/` (desktop-only TU) | The simulator, and tests that want real files. |
| `SdFileSystem` | `shell/` | The device, over `SDCardManager`. |

`HostFileSystem` uses `<filesystem>`, which is a host-OS dependency — so it is
excluded from the firmware build the same way `png.cpp` already is, via
`core/library.json`'s `srcFilter`. **Check that exclusion works** rather than
assuming: a `<filesystem>` include reaching the ESP32 toolchain is a confusing
failure.

### 2. Paths are strings and directories are listed, not walked

The interface is deliberately small — `exists`, `list`, `readAll`, `writeAll`,
`mkdirs`, `remove`. No open file handles, no seeking, no streaming. Every V1 need
is "read this small file" or "list this directory".

Phase 3 will need streaming for EPUBs, which are megabytes and cannot be read
into a `std::string` on a device with ~230 KB of heap. **That is a deliberate
gap, not an oversight**: streaming wants a different shape (a handle with
`read(buf, n)`), and designing it now with no consumer would mean guessing. Note
it at the interface so Phase 3 does not mistake the omission for an oversight.

### 3. Settings are flat JSON, and a bad file is replaced rather than trusted

Spec §5 puts settings at `/.reader/settings.json`. There is no JSON library
vendored and no network to fetch one, so this plan writes a **minimal flat**
reader and writer in `core/`: one object, keys to numbers, bools and strings, no
nesting, no arrays. That covers settings completely. Per-book state with its
bookmark array is Phase 3's, and will need arrays — note that at the parser.

A settings file that is missing, unparseable, or carries an unknown version is
**replaced with defaults**, and the firmware says so on serial. An out-of-range
*value* is different: that field is **clamped** and the rest of the file still
loads, because clamping and carrying on beats refusing to boot over one bad
number. Either way `loadSettings` returns false so a caller can warn. Spec §6 requires
cache entries be "versioned + checksummed; a bad entry is discarded and rebuilt,
never trusted" — the same principle applies to settings, and the failure mode it
prevents is a device that will not boot because a half-written file has a
trailing comma.

Every setting carries a `version`. An unknown version is a bad file.

### 4. The wake pointer lives in NVS, not on the card

2B established that deep sleep is a chip reset, so restoring the last screen
needs persistence. Two candidates, and the card is the wrong one: **a wake must
work with no card in the slot**, and the whole point of the SD-missing screen is
that the firmware handles that state. Spec §5 already puts the "last-open
pointer" in NVS for the same reason.

So NVS holds a tiny session record — which screen, and its focus index. NVS is a
shell concern (`Preferences`), so `core/` never sees it: the shell reads it at
boot and hands the App a starting screen.

**Only restore across a genuine wake.** On a cold boot, start at Home: a device
that boots into a Settings sub-screen after being off for a week is confusing,
and 2B already distinguishes the two cases via `esp_sleep_get_wakeup_cause()`.

### 5. The SD-missing screen is designed, so build it from its board

`design/SdMissing.dc.html` exists. Unlike 2B's placeholders, this is a real
product screen and gets a real theme method and a golden at both geometries.

Spec §6: "No/failed SD card: full-screen prompt with retry; device never boots
into a broken UI." So it needs a working retry that re-attempts the mount — not
a dead-end message.

## File structure

**New in `core/`:**

| File | Responsibility |
|---|---|
| `core/include/reader/filesystem.h` | `DirEntry`, `FileSystem` interface. Header only. |
| `core/include/reader/host_fs.h` + `core/src/host_fs.cpp` | `HostFileSystem`. **Desktop only** — excluded from the firmware build. |
| `core/include/reader/json.h` + `core/src/json.cpp` | Minimal flat-object JSON read/write. |
| `core/include/reader/settings.h` + `core/src/settings.cpp` | `Settings`, defaults, validation, load/save. |
| `core/include/reader/screen_sd_missing.h` + `core/src/screen_sd_missing.cpp` | `SdMissingScreen`. |

**Modified:** `core/include/reader/viewmodel.h` (an `SdMissingViewModel`),
`theme.h` / `theme_quiet.h` / `theme_quiet.cpp` (`renderSdMissing`),
`core/include/reader/app.h` (`ScreenId::SdMissing`), `core/include/reader/screens.h`
+ `core/src/screens.cpp` (the catalogue), `core/library.json` (exclude `host_fs.cpp`),
`sim/main.cpp`, `shell/src/main.cpp`, `platformio.ini` (nothing new expected —
verify).

**New in `shell/`:** `shell/src/sd_fs.h` + `.cpp` (`SdFileSystem`),
`shell/src/session.h` + `.cpp` (the NVS session record).

**Tests:** `test/unit/fake_fs.h`, `test_filesystem.cpp`, `test_json.cpp`,
`test_settings.cpp`, `test_screen_sd_missing.cpp`, and goldens
`sd_missing.png` / `sd_missing_x3.png`.

> **CMake uses `file(GLOB ...)`.** Re-run `cmake -S . -B build` after adding a
> source file, or it is silently ignored. `make test` does it for you.

---

## Task 1: The `FileSystem` interface and an in-memory fake

**Files:**
- Create: `core/include/reader/filesystem.h`
- Create: `test/unit/fake_fs.h`
- Test: `test/unit/test_filesystem.cpp`

- [ ] **Step 1: Write the interface**

`core/include/reader/filesystem.h`:

```cpp
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace reader {

struct DirEntry {
  std::string name;  // leaf name, not a path
  bool isDir = false;
  uint32_t size = 0;  // 0 for directories
};

// Everything core/ knows about storage.
//
// Deliberately small: exists / list / readAll / writeAll / mkdirs / remove. Every
// V1 need is "read this small file" or "list this directory", and an interface
// that stops there can be faked in a test with a std::map.
//
// NOT here, on purpose: open handles, seeking, streaming reads. Phase 3's EPUBs
// are megabytes against ~230 KB of heap, so they cannot use readAll and will need
// a handle with read(buf, n). That is a different shape and designing it now,
// with no consumer to check it against, would mean guessing. Add it when the
// EPUB reader exists -- the omission is a decision, not an oversight.
//
// Paths are absolute, '/'-separated, and never end in '/'.
class FileSystem {
 public:
  virtual ~FileSystem() = default;

  // False when there is no usable storage -- no card, or a card that would not
  // mount. Every other method may fail in that state.
  virtual bool mounted() const = 0;

  virtual bool exists(std::string_view path) = 0;

  // Appends `path`'s entries to `out` (does not clear it). False if `path` is
  // not a readable directory. Order is unspecified: callers that care sort.
  virtual bool list(std::string_view path, std::vector<DirEntry>& out) = 0;

  // Whole-file read. Intended for the small JSON files V1 stores; see the note
  // above about why this is not the EPUB path.
  virtual bool readAll(std::string_view path, std::string& out) = 0;

  // Creates or truncates. Creates parent directories as needed, so a caller
  // saving settings does not have to mkdirs first.
  virtual bool writeAll(std::string_view path, std::string_view data) = 0;

  virtual bool mkdirs(std::string_view path) = 0;

  // Files only. Returns true if the file is gone afterwards, including when it
  // was already absent -- callers deleting a book care about the end state, not
  // about racing something else that deleted it first.
  virtual bool remove(std::string_view path) = 0;
};

}  // namespace reader
```

- [ ] **Step 2: Write the fake**

`test/unit/fake_fs.h`. An in-memory `FileSystem` backed by
`std::map<std::string, std::string>` for files plus a `std::set<std::string>` of
directories. It must support:

- `mounted()` returning a settable flag, so the SD-missing path is testable
- a settable "fail every write" flag, so save-failure handling is testable
- `writeAll` creating parents, matching the interface's contract

Keep it in `test/unit/` — it is test scaffolding, not product code.

- [ ] **Step 3: Write the tests**

`test/unit/test_filesystem.cpp`, against the fake. These pin the **contract**,
so the same cases can later be pointed at `HostFileSystem` (Task 2):

- a written file exists and reads back byte-identical, including embedded newlines
  and an empty file
- `writeAll` truncates rather than appending
- `writeAll` creates missing parents
- `list` appends rather than clearing, and reports `isDir` correctly
- `list` on a file, or on a missing path, returns false
- `readAll` on a missing file returns false and leaves `out` untouched
- `remove` returns true for an absent file (end-state contract), and a removed
  file stops existing
- `remove` on a directory returns false
- with `mounted()` false, every operation fails rather than pretending

- [ ] **Step 4: Run, confirm failures are about the fake only, then implement**

Run: `make test`. The interface is pure virtual so nothing links until the fake
exists; write the fake to make them pass.

- [ ] **Step 5: Commit**

```bash
git add core/include/reader/filesystem.h test/unit/fake_fs.h test/unit/test_filesystem.cpp
git commit -m "feat(core): a FileSystem interface, and an in-memory fake for tests"
```

---

## Task 2: `HostFileSystem`, and proving it obeys the same contract

**Files:**
- Create: `core/include/reader/host_fs.h`, `core/src/host_fs.cpp`
- Modify: `core/library.json`
- Test: `test/unit/test_filesystem.cpp` (run the contract cases against it too)

- [ ] **Step 1: Implement it over `<filesystem>`**

`HostFileSystem(std::string root)` prefixes every path with `root`, so a test or
the simulator can point it at a temp directory and an absolute reader path like
`/.reader/settings.json` lands inside it. `mounted()` is whether `root` exists.

- [ ] **Step 2: Exclude it from the firmware build**

`core/library.json` already has `"srcFilter": ["+<*>", "-<png.cpp>"]`. Add
`-<host_fs.cpp>` for the same reason: `<filesystem>` is a host-OS dependency and
must never reach the ESP32 toolchain.

- [ ] **Step 3: Run the contract cases against both implementations**

Refactor `test_filesystem.cpp` so the contract cases are a function taking a
`FileSystem&`, called once with the fake and once with a `HostFileSystem` rooted
at a temp directory under `BUILD_DIR`. **This is the point of the task**: a fake
that passes tests the real one fails is worse than no fake, because every test
above it then proves nothing.

Clean the temp directory up, and make the test independent of leftovers from a
previous run.

- [ ] **Step 4: Verify the exclusion actually holds**

Run: `make firmware`
Expected: SUCCESS. Then confirm `host_fs.cpp` is genuinely absent from the image:

```bash
ls .pio/build/xteink/libfce/core/ | grep host_fs
```

Expected: **no output.** If it is there, the `srcFilter` is not doing what it
looks like it does, and the plan's assumption is wrong — report that rather than
working around it.

- [ ] **Step 5: Commit**

```bash
git add core/include/reader/host_fs.h core/src/host_fs.cpp core/library.json test/unit/test_filesystem.cpp
git commit -m "feat(core): a host filesystem, held to the same contract as the fake"
```

---

## Task 3: Minimal flat JSON

**Files:**
- Create: `core/include/reader/json.h`, `core/src/json.cpp`
- Test: `test/unit/test_json.cpp`

- [ ] **Step 1: Write the header**

```cpp
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace reader {

// A deliberately tiny JSON subset: ONE object, keys to numbers, bools and
// strings. No nesting, no arrays, no null, no exponents.
//
// That is the whole of what settings need, and it is written here rather than
// vendored because nothing is vendored and there is no network to fetch one.
// Phase 3's per-book state carries a bookmark ARRAY and will outgrow this --
// extend it there, with the consumer in hand.
//
// Parsing is total: any malformed input is a clean `false`, never a partial
// result and never a crash. A settings file is attacker-adjacent only in the
// sense that a user can hand-edit it and a half-finished write can truncate it,
// and both must be survivable.
class JsonObject {
 public:
  bool parse(std::string_view text);

  bool getInt(std::string_view key, int64_t& out) const;
  bool getBool(std::string_view key, bool& out) const;
  bool getString(std::string_view key, std::string& out) const;

  void setInt(std::string_view key, int64_t v);
  void setBool(std::string_view key, bool v);
  void setString(std::string_view key, std::string_view v);

  // Serialised with keys in sorted order, so saving unchanged settings produces
  // a byte-identical file and a diff of two dumps is readable.
  std::string dump() const;

 private:
  enum class Kind : uint8_t { Int, Bool, String };
  struct Value {
    Kind kind = Kind::Int;
    int64_t i = 0;
    bool b = false;
    std::string s;
  };
  std::map<std::string, Value, std::less<>> values_;
};

}  // namespace reader
```

- [ ] **Step 2: Write the tests first**

`test/unit/test_json.cpp`. Round-trip and rejection both matter:

- round-trip: set each type, `dump()`, `parse()` the result, read the same values
  back; `dump()` is stable across two calls and key order is sorted
- whitespace tolerance: newlines and spaces around `{`, `:`, `,`, `}`
- negative and zero integers; the string escapes `\"` and `\\`
- **rejected**, each a clean `false` with no crash: empty input; `{` alone; a
  trailing comma; a missing colon; an unquoted key; a nested object; an array
  value; `null`; an unterminated string; a bare number outside an object; two
  keys the same (**decide** which wins, then pin it — do not leave it undefined)
- a truncated file, byte by byte: for a valid dump, `parse()` on every prefix
  either succeeds or fails cleanly. **This is the case that matters most** — a
  half-written settings file is the realistic corruption on a device that can
  lose power mid-write, and this test is what proves it cannot take the firmware
  down.
- getters on the wrong type return false rather than converting

- [ ] **Step 3: Run and confirm they fail, then implement**

Run: `make test`. Expected: link errors on `JsonObject::parse`.

- [ ] **Step 4: Confirm passing, and fuzz it briefly**

Beyond the cases above, add a loop that feeds a few hundred deterministic
pseudo-random byte strings (fixed seed, no `rand()` — the project bans
non-deterministic sources in tests) through `parse()` and asserts it always
returns without crashing. Under `-fsanitize=address,undefined` if the harness
supports it.

- [ ] **Step 5: Commit**

```bash
git add core/include/reader/json.h core/src/json.cpp test/unit/test_json.cpp
git commit -m "feat(core): a minimal flat-object JSON reader and writer"
```

---

## Task 4: The settings store

**Files:**
- Create: `core/include/reader/settings.h`, `core/src/settings.cpp`
- Test: `test/unit/test_settings.cpp`

- [ ] **Step 1: Write the header**

```cpp
#pragma once
#include <cstdint>

namespace reader {
class FileSystem;

// Where settings live (spec §5).
inline constexpr char kSettingsPath[] = "/.reader/settings.json";

// Bumped when a field's MEANING changes, not when one is added: an added field
// simply takes its default from an older file. An unrecognised version is a bad
// file and gets replaced by defaults.
inline constexpr int kSettingsVersion = 1;

// Every knob the shell hardcoded through 2B. Defaults here are the values that
// were compiled in, so behaviour is unchanged until a user changes something.
struct Settings {
  uint32_t sleepAfterMs = 5u * 60u * 1000u;
  // Periodic FULL refresh cadence. 0 = never; see RefreshPolicy::kNever.
  int fullRefreshEvery = 0;
  // A screen change takes the FULL waveform so the outgoing screen cannot ghost
  // through. Diverges from the reference firmware deliberately -- see the
  // roadmap. Costs ~825 ms against ~520 ms.
  bool fullOnTransition = true;

  // Clamps every field into a sane range, returning false if anything had to be
  // clamped. A file that needs clamping is a file to distrust, but clamping and
  // carrying on beats refusing to boot.
  bool validate();
};

// Reads kSettingsPath. Returns false when the file is absent, unreadable,
// unparseable, or carries an unknown version -- in every one of those cases
// `out` is left holding DEFAULTS, so a caller that ignores the return value
// still gets a working device. Callers that want to warn check the result.
bool loadSettings(FileSystem& fs, Settings& out);

// Writes kSettingsPath, creating /.reader if needed.
bool saveSettings(FileSystem& fs, const Settings& in);

}  // namespace reader
```

- [ ] **Step 2: Write the tests first**

`test/unit/test_settings.cpp`, against `FakeFileSystem`:

- a fresh filesystem: `loadSettings` returns false and `out` holds defaults
- save then load round-trips every field
- **saving unchanged defaults and loading them back is a fixed point** — the
  round trip cannot drift
- a file with a missing key keeps that field's default and still loads
- a file with an unknown extra key loads fine (forward compatibility: a newer
  firmware's file must not brick an older one)
- a wrong `version` returns false with defaults
- a truncated / malformed file returns false with defaults — and specifically
  **`out` must not be left half-populated** from a partial parse
- out-of-range values are clamped: negative `sleepAfterMs`-equivalents, a
  `fullRefreshEvery` below 0 or absurdly large
- `saveSettings` creates `/.reader` when absent
- `saveSettings` returns false when the filesystem refuses the write, and does
  not report success

- [ ] **Step 3: Run, confirm failure, implement, confirm passing**

- [ ] **Step 4: Commit**

```bash
git add core/include/reader/settings.h core/src/settings.cpp test/unit/test_settings.cpp
git commit -m "feat(core): a settings store that replaces a bad file rather than trusting it"
```

---

## Task 5: The SD-missing screen

**Files:**
- Modify: `core/include/reader/viewmodel.h`, `theme.h`, `theme_quiet.h`, `core/src/theme_quiet.cpp`, `core/include/reader/app.h`
- Create: `core/include/reader/screen_sd_missing.h`, `core/src/screen_sd_missing.cpp`
- Test: `test/unit/test_screen_sd_missing.cpp`, goldens `sd_missing.png` / `sd_missing_x3.png`

- [ ] **Step 1: Read the board**

`design/SdMissing.dc.html` is the authority. Note its type roles (a run with no
`font-weight` is CSS default **400**, a different role from 500), its structural
padding, its icons, and its hint bar's four slots. **If the board is wrong, fix
the board first** — never only the implementation.

- [ ] **Step 2: Add `SdMissingViewModel` and the theme method**

Semantic content plus interaction state, no geometry. Add `renderSdMissing` to
`Theme` and implement it in `QuietTheme` from existing primitives
(`drawHeaderBand`, `drawRow`, `drawHintBar`, `drawIcon`, `drawText`, `baselineIn`,
`iconTopIn`, `trackingEm`). **If a primitive cannot express what the board does,
extend the primitive** — every fidelity defect in this project belonged in the
shared layer, and a screen-local workaround means the next screen inherits it.

- [ ] **Step 3: Add `ScreenId::SdMissing` and the screen**

Spec §6 requires a working **retry**, not a dead end. So `Confirm` re-attempts
the mount. The screen cannot do that itself — mounting is the shell's — so it
returns an action the shell handles. Add `Action::Kind::Retry` (or an equivalent),
and have the shell re-run the mount and replace the screen on success.

Whatever shape you choose, **the retry must be reachable and must actually
re-mount**; a button that redraws the same screen is worse than no button.

- [ ] **Step 4: Golden at both geometries**

480×800 and 528×792, through the shipping path (`Fidelity::Mono` →
`Plane::Bw`, 1-bit, `golden::checkGolden`). Render the candidates, **open both
with the Read tool**, and walk them against the board item by item before
blessing. Report honestly what you see, including anything that looks wrong even
if you bless it.

Check the recurring defects: text optically centred via `baselineIn`; icons
aligned via `iconTopIn`; no pinned height the board computes; correct role *and
weight* per run; tracking carried as em not pre-rounded pixels; no grey on any
rule, fill or dither cell (icon edges may carry grey — they are 2 bpp by design);
and nothing overflowing at 480 wide, which fails first.

- [ ] **Step 5: Commit**

```bash
git add core/include/reader/screen_sd_missing.h core/src/screen_sd_missing.cpp \
        core/include/reader/viewmodel.h core/include/reader/theme.h \
        core/include/reader/theme_quiet.h core/src/theme_quiet.cpp \
        core/include/reader/app.h test/unit/test_screen_sd_missing.cpp \
        test/golden/sd_missing.png test/golden/sd_missing_x3.png
git commit -m "feat(screens): the SD-missing screen, with a retry that re-mounts"
```

---

## Task 6: `SdFileSystem` on the device

**Files:**
- Create: `shell/src/sd_fs.h`, `shell/src/sd_fs.cpp`

- [ ] **Step 1: Implement `FileSystem` over `SDCardManager`**

`SDCardManager` gives `begin()`, `exists()`, `open(path, oflag)` returning an
SdFat `FsFile`, and `vol()` for volume operations (`remove`, `mkdir`, `rmdir`).

**Use the SDK's own directory idiom, verified in `SDCardManager.cpp:174-186` —
not the names you might expect from SdFat docs:**

```cpp
char name[128];
for (auto f = dir.openNextFile(); f; f = dir.openNextFile()) {
  f.getName(name, sizeof(name));      // char* + size, not a String
  const bool isDir = f.isDirectory(); // isDirectory(), NOT isDir()
  const uint32_t size = f.fileSize();
  f.close();                          // every handle, every iteration
}
dir.close();
```

`openNextFile()` / `isDirectory()` are the methods that exist here. **Every
handle must be closed, including in the early-continue path** — the SDK's own
loop closes before `continue`, and leaking handles on a long directory is how a
file browser starts failing to open anything.

Points to get right:

- **`mounted()` must reflect reality**, not just whether `begin()` was once
  called. A card can be pulled.
- `list` must not recurse — one level, matching the interface.
- `getName(char*, size_t)` needs a buffer and the SDK uses 128 bytes. A name
  that does not fit must not be silently truncated into a *different valid
  filename* — that is how a file browser opens the wrong book, or deletes it.
  Decide the behaviour (skip the entry, or mark it unopenable) and say so in a
  comment.
- `writeAll` creates parents (`mkdirs`) then writes with `O_WRONLY | O_CREAT |
  O_TRUNC`, and **verifies the byte count written**. A short write on a full card
  that reports success is how a settings file gets truncated.
- **The caller must serialise SD traffic against panel refreshes.** This was
  checked: `SDCardManager` contains no mutex or semaphore at all. Its only
  shared-bus handling is in `begin()`, which drives display CS high before probing
  because a powered, never-deselected panel breaks detection. A transfer racing a
  refresh is the kind of fault that looks random, so keep them apart explicitly.

- **Nothing will hold `SdFileSystem` to the contract unless you build the seam.**
  `test_filesystem.cpp`'s contract cases bind only the two desktop
  implementations; `shell/` has no test harness, so the device implementation is
  the one place the contract is unenforced. Extract the contract body into a
  header that reports through a callback, so the desktop tests drive it with
  doctest and an on-device routine can drive the same assertions over serial.
  Then the easy-to-miss clauses are actually checked on hardware: `remove`
  returns **true** for an already-absent file; `list` **appends** rather than
  clearing; `mkdirs` is idempotent; `writeAll` refuses a directory and creates
  parents; paths normalise (`//`, trailing `/`); and `DirEntry.size` is
  `uint32_t` while SdFat's `fileSize()` is 64-bit.

- [ ] **Step 2: Build**

Run: `make firmware`. Nothing calls it yet, so device behaviour is unchanged.

- [ ] **Step 3: Commit**

```bash
git add shell/src/sd_fs.h shell/src/sd_fs.cpp
git commit -m "feat(shell): a FileSystem over the SD card"
```

---

## Task 7: The NVS session record

**Files:**
- Create: `shell/src/session.h`, `shell/src/session.cpp`

- [ ] **Step 1: Implement it over `Preferences`**

A tiny record — the `ScreenId` on top and its focus index — with
`loadSession(Session&)`, `saveSession(const Session&)` and `clearSession()`.

Why NVS and not the card: **a wake must work with no card in the slot.** Spec §5
puts the last-open pointer in NVS for the same reason.

Keep the NVS namespace and keys short (NVS keys are capped at 15 characters) and
name them in a comment so a future reader can find them with `nvs_get`.

An unrecognised `ScreenId` in NVS must be treated as "no session", not
dereferenced — the enum can gain values between firmware versions.

- [ ] **Step 2: Build, then commit**

```bash
git add shell/src/session.h shell/src/session.cpp
git commit -m "feat(shell): persist the last screen in NVS, which survives a missing card"
```

---

## Task 8: Wire it all into the shell

**Files:** Modify `shell/src/main.cpp`

This is the task with no desktop test. Build after each step.

- [ ] **Step 1: Mount the card and load settings**

In `setup()`, after the display is up and before the app is built:

1. `SdFileSystem` mount attempt, logged as a `[stage]` mark either way.
2. If mounted, `loadSettings`. Log whether the file was read or defaults were
   used, **and why** — that log line is the only way a user will ever learn their
   settings file was rejected.
3. Feed the settings into `gRefresh` and `gIdle` in place of the hardcoded
   constants. Keep the constants as the `Settings` struct's defaults so
   behaviour is unchanged when there is no file.

- [ ] **Step 2: Route to the SD-missing screen when there is no card**

If the mount fails, the app's root becomes `SdMissingScreen` instead of Home, and
`Confirm` retries the mount. On a successful retry, replace the root with Home
and load settings.

**The device must never boot into a broken UI** (spec §6) — so a missing card is
a designed screen, not a hang and not a Home screen with no books.

- [ ] **Step 3: Restore the session on a wake**

2B already computes `fromSleep` from `esp_sleep_get_wakeup_cause()`. On a wake
**and only on a wake**, read the session and start on that screen; on a cold boot
start at Home and clear the record. Save the session whenever the top of the
stack changes.

Guard it: a restored `ScreenId` the factory cannot build must fall back to Home
rather than pushing a null.

- [ ] **Step 4: Build and report**

Run: `make firmware` — SUCCESS. Report RAM and Flash, and the delta.

- [ ] **Step 5: Commit**

```bash
git add shell/src/main.cpp
git commit -m "feat(shell): mount the card, load settings, and resume where you left off"
```

---

## Task 9: Verify, document, hand off the flash

- [ ] **Step 1: Full verification**

```bash
make test
make sim
make firmware
make compare COMPARE_ARGS="--only home,sd_missing"
```

All green; the four existing Home goldens and `text_sample.png` **unchanged**.

- [ ] **Step 2: Point the simulator at a real directory**

Give `reader_sim` a `--root DIR` option that backs a `HostFileSystem`, so the
storage path is exercised on the desktop rather than only on the device. Confirm
`reader_sim sd_missing out.png` renders the screen without a root.

- [ ] **Step 3: Update `CLAUDE.md` and the roadmap**

`CLAUDE.md` gains a short **Storage** section: `core/` sees only `FileSystem`;
three implementations and which builds each is in; `host_fs.cpp` is excluded from
the firmware like `png.cpp`; settings are flat JSON at `/.reader/settings.json`
and a bad file is replaced, not trusted; the wake pointer is in NVS because a
wake must work with no card.

The roadmap: mark 2C-1 done, note what 2C-2 and 2C-3 inherit, and record the
`FileSystem` streaming gap as Phase 3's.

- [ ] **Step 4: Commit and hand off**

Give the user the flash command and name what only the panel can answer:

```bash
cd ~/dev/encre && ~/.platformio/penv/bin/pio run -e xteink -t upload --upload-port /dev/cu.usbmodem1101
```

1. **With the card in:** does it boot to Home as before, and does the log show
   settings loaded or defaults used?
2. **With the card out:** does the SD-missing screen appear, and does Confirm
   retry — insert the card, press Confirm, and it should go to Home?
3. **Sleep and wake:** navigate to Settings, sleep, wake — does it come back to
   Settings rather than Home?
4. **Cold boot after that:** does it start at Home rather than resuming?
