#pragma once

class SdFileSystem;

// Drives every clause in reader/fs_contract.h against the real SD card and
// reports each assertion over Serial. This is the on-device half of the contract
// seam: `shell/` has no test harness, so without this SdFileSystem is the one
// implementation of reader::FileSystem that nothing checks, and the clauses it
// would fail are exactly the ones that read like pedantry until a file browser
// starts deleting the wrong book (remove() on an absent file, list() clearing
// `out`, a name that does not fit the buffer).
//
// The desktop runner is test/unit/test_filesystem.cpp. Both run the same clauses
// in the same order, so a failure here that does not reproduce there is a
// difference between the card and the fake -- which is the whole thing worth
// knowing.
//
// Runs in a scratch directory (/.reader/fstest), wiped before each clause and
// removed afterwards: a clause needs storage that is mounted and EMPTY, and the
// user's card is neither. Nothing outside that directory is written or deleted.
//
// With no card in the slot it runs the unmounted clause instead -- which is worth
// doing deliberately, because "every operation fails and nothing quietly creates
// storage" is the promise the SD-missing screen depends on.
//
// BUILT IN ONLY WITH -DENCRE_FS_SELFTEST=1. Verified recipe:
//
//   PLATFORMIO_BUILD_FLAGS="-DENCRE_FS_SELFTEST=1" make firmware
//
// That environment variable APPENDS to platformio.ini's build_flags (checked --
// -DFREEINK_DEVICE_X3=1 and the rest survive). Do NOT reach for
// `--project-option="build_flags=..."` instead: that REPLACES the list, so the
// device defines vanish and the binary silently builds for the wrong board.
//
// Without the flag this is a stub and the firmware pays nothing for it. With it
// enabled the self-test costs ~29.7 KB of flash (measured at Phase 3A task 3:
// 742648 bytes without, 773028 with) and 8 bytes of RAM, because the clauses
// carry a string per assertion. That figure roughly doubled when the ten
// openRead/FileHandle clauses landed, which is the cost of the one thing that
// checks the handle against real hardware -- and worth it for exactly that.
//
// Returns the number of FAILED assertions (0 = the device obeys the contract), or
// -1 when the self-test was not compiled in. -1 rather than 0 on purpose: a build
// without it must not be mistakable for a build that passed.
int runSdFsContractSelfTest(SdFileSystem& fs);
