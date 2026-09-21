#pragma once

// THE REAL TYPE, NOT THE ALIAS. `EInkDisplay` is a `using` for
// freeink::FreeInkDisplay -- the SDK's own compatibility shim, which the fake
// reproduces -- so `class EInkDisplay;` is ill-formed wherever the shim has been
// seen. sd_selftest.h gets away with `class SdFileSystem;` because that IS a class.
// Forward-declaring the underlying type costs a namespace and pulls in no header.
namespace freeink {
class FreeInkDisplay;
}

// Drives reader/panel_contract.h's clauses against the REAL panel and reports each
// assertion over Serial. This is the on-device half of the panel contract, and it
// exists for the reason sd_selftest.h does one seam over: the fake in
// test/shell/fake_arduino/ is a model of hardware this project does not own, and
// `make firmware` checks its SIGNATURES on every PR while nothing checks its
// SEMANTICS.
//
// The desktop runner is test/shell/panel_contract_test.cpp. Both run the same
// clauses in the same order, so a failure here that does not reproduce there is a
// difference between the panel and the fake -- which is the whole thing worth
// knowing, and the only way to find it.
//
// WHAT IT CANNOT REACH, and this is the limit to state rather than discover:
// _oldPlaneValid, _forceFullSyncNext and the rest are private in Uc8279Driver.h
// with no accessor, and freeink-sdk/ must never be edited. No runner can read them
// back, so the fake's model of them is a second copy nothing verifies. The
// caller-side invariant in the fake -- no skipInitialResync() without a completed
// refresh -- is what covers that gap instead, and it needs no model.
//
// MUST RUN AFTER display.begin(). The framebuffer clause asserts a real allocation
// and the real driver has none before it.
//
// Returns the number of FAILED assertions, or -1 when the build did not include it.
// -1 rather than 0, so "did not run" cannot read as "passed" -- sd_selftest's rule,
// and the one that matters most in a number nobody looks at twice.
//
// Build it with:
//   PLATFORMIO_BUILD_FLAGS="-DENCRE_PANEL_SELFTEST=1" make firmware
//
// OPT-IN BECAUSE ONE CLAUSE SPENDS A WAVEFORM. refreshPreservesFrame issues a real
// FAST refresh -- ~390 ms and a visible flash -- on a device whose whole boot is
// budgeted. The other three are free, so the flag takes all four and the default
// build takes none.
int runPanelContractSelfTest(freeink::FreeInkDisplay& display);
