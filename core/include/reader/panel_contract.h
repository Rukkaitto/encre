#pragma once
#include <cstddef>
#include <cstdint>

// WHAT A PANEL MUST DO, AS CLAUSES TWO RUNNERS CAN DRIVE.
//
// fs_contract.h's shape, for its reason: `test/shell/fake_arduino/FreeInkDisplay.h`
// is a fake of hardware this project does not own, and `make firmware` checks its
// SIGNATURES while nothing checks its SEMANTICS. This is the part of the semantics
// that can be checked -- by running the same clauses against the fake on every
// `make test` and against the real driver on a real panel.
//
// TEMPLATED, WHERE fs_contract TAKES A REFERENCE, and the difference is not a
// preference. `reader::FileSystem` is an interface with six adapters; `EInkDisplay`
// is a concrete class and the fake is a different concrete class, so there is no
// common base to take. A template is what stands in, and it means this header names
// no panel type at all.
//
// WHAT THESE CLAUSES DELIBERATELY DO NOT ASSERT, because asserting something the
// real driver does not do would fail a healthy device:
//
//   * that a second triggerDisplay() before completeDisplay() is refused. Unknown,
//     and inventing a refusal neither side makes would be a contract about nothing.
//   * that an out-of-order grayscale sequence is refused. It is NOT: the driver
//     drops an MSB copy it has not seen a valid LSB for, SILENTLY. A clause
//     claiming otherwise would be false on both sides.
//   * anything about _oldPlaneValid, _forceFullSyncNext or the other flags. They
//     are private in Uc8279Driver.h with no accessor and freeink-sdk/ must never be
//     edited, so NO runner can read them back. That is this directory's largest
//     fidelity gap and a contract cannot close it. The shell-side invariant --
//     no skipInitialResync() without a completed refresh -- is what covers it
//     instead, and it lives in the fake because it is about the CALLER.
namespace reader {

class PanelContractReport {
 public:
  virtual ~PanelContractReport() = default;
  virtual void check(bool passed, const char* expr) = 0;
  // AN OBSERVATION RATHER THAN AN ASSERTION, and the distinction earns its place.
  // `supportsStripGrayscale()` is a fact about which controller is in front of you:
  // the SDK's own docs say false for this panel and Uc8279Driver.h returns true,
  // and a clause cannot decide which is right. What settles it is running this on
  // the glass and READING the value, so the report carries it rather than judging
  // it.
  virtual void note(const char* what, long value) = 0;
};

#define PC_CHECK(cond) r.check(static_cast<bool>(cond), #cond)

namespace panel_contract {

// THE GEOMETRY IS SELF-CONSISTENT, and this is the clause bindFrameToDriver rests
// on: it refuses a framebuffer view whose size disagrees with getBufferSize(), so a
// driver whose arithmetic did not hold would be refused at boot rather than drawing
// wrongly.
//
// `widthBytes * height` IS THE DRIVER'S OWN DEFINITION rather than `w * h / 8`,
// which is only equal when the width divides by eight. It does on both Xteink
// panels (792 and 800), and asserting the weaker relation is what keeps this clause
// true on a panel whose rows are padded.
template <class Panel>
void geometry(Panel& p, PanelContractReport& r) {
  const uint32_t w = p.getDisplayWidth();
  const uint32_t h = p.getDisplayHeight();
  const uint32_t bytes = p.getBufferSize();
  r.note("width", static_cast<long>(w));
  r.note("height", static_cast<long>(h));
  r.note("bufferSize", static_cast<long>(bytes));
  PC_CHECK(w > 0);
  PC_CHECK(h > 0);
  PC_CHECK(bytes > 0);
  // A row must hold at least one bit per pixel, and the buffer must hold every row.
  const uint32_t minRowBytes = (w + 7u) / 8u;
  PC_CHECK(bytes >= minRowBytes * h);
  // ...and not more than a row's padding could explain. Four bytes is generous for
  // any alignment a controller asks for and still catches a buffer sized for the
  // wrong panel, which is the failure that matters: an X4 buffer on an X3 is
  // 800x480 against 792x528, and the product is SMALLER, not larger.
  PC_CHECK(bytes <= (minRowBytes + 4u) * h);
}

// THE FRAMEBUFFER IS REAL AND IT STAYS PUT.
//
// Both halves are bindFrameToDriver's premise. It takes the driver's own buffer and
// hands core/ a view of it, so a null would be a refusal at boot -- and an address
// that moved between paints would leave that view pointing at freed memory, which
// is a fault that draws garbage rather than crashing.
//
// CALL ONLY AFTER begin(). Before it the real driver has not allocated, and a
// clause that asserted non-null there would be asserting the opposite of the truth.
template <class Panel>
void framebufferStable(Panel& p, PanelContractReport& r) {
  uint8_t* first = p.getFrameBuffer();
  PC_CHECK(first != nullptr);
  if (first == nullptr) return;
  // Writing to it is part of the claim: a buffer that cannot be written is not a
  // framebuffer, and every render this firmware does writes here.
  first[0] = 0x00;
  first[p.getBufferSize() - 1] = 0x00;
  PC_CHECK(p.getFrameBuffer() == first);
}

// THE CAPABILITY QUERIES ARE STABLE, which is all a contract can say about them.
//
// Their VALUES are facts about the controller in front of you and differ by panel,
// so they are noted rather than checked -- and reading them off a real device is
// what settles a disagreement this project has recorded: the SDK's
// docs/xteink-x3-uc8279-support.md says supportsStripGrayscale() is false here and
// Uc8279Driver.h returns true. Neither line is the authority; the driver body is,
// and this is how you ask it.
template <class Panel>
void capabilitiesStable(Panel& p, PanelContractReport& r) {
  const bool strip = p.supportsStripGrayscale();
  const bool staging = p.supportsBusyGrayscaleStaging();
  const bool combines = p.combinesGrayscaleBase();
  r.note("supportsStripGrayscale", strip ? 1 : 0);
  r.note("supportsBusyGrayscaleStaging", staging ? 1 : 0);
  r.note("combinesGrayscaleBase", combines ? 1 : 0);
  // A query that answered differently twice would make every decision built on it
  // depend on when it was asked.
  PC_CHECK(p.supportsStripGrayscale() == strip);
  PC_CHECK(p.supportsBusyGrayscaleStaging() == staging);
  PC_CHECK(p.combinesGrayscaleBase() == combines);
}

// A REFRESH LEAVES THE GEOMETRY AND THE BUFFER WHERE THEY WERE.
//
// The one clause that issues a waveform, so it costs ~700 ms on glass and is the
// reason the device-side runner is opt-in. What it pins is that a paint does not
// move the frame under core/'s view of it -- the same claim as framebufferStable,
// made across the operation most likely to break it.
template <class Panel>
void refreshPreservesFrame(Panel& p, PanelContractReport& r) {
  uint8_t* before = p.getFrameBuffer();
  const uint32_t bytes = p.getBufferSize();
  const uint32_t w = p.getDisplayWidth();
  PC_CHECK(before != nullptr);
  if (before == nullptr) return;
  p.triggerDisplay(Panel::FAST_REFRESH);
  p.completeDisplay();
  PC_CHECK(p.getFrameBuffer() == before);
  PC_CHECK(p.getBufferSize() == bytes);
  PC_CHECK(p.getDisplayWidth() == w);
}

}  // namespace panel_contract

#undef PC_CHECK

// THE CLAUSES THAT NEED ONLY A begin()'d PANEL, in the order a runner should take
// them. `refreshPreservesFrame` is NOT here: it spends a waveform, so a runner asks
// for it separately and the desktop takes it while the device may decline.
template <class Panel>
struct PanelContractClause {
  const char* name;
  void (*run)(Panel&, PanelContractReport&);
};

template <class Panel>
const PanelContractClause<Panel>* panelContractClauses(size_t& count) {
  static const PanelContractClause<Panel> kClauses[] = {
      {"geometry is self-consistent", &panel_contract::geometry<Panel>},
      {"the framebuffer is real and stays put", &panel_contract::framebufferStable<Panel>},
      {"the capability queries are stable", &panel_contract::capabilitiesStable<Panel>},
  };
  count = sizeof(kClauses) / sizeof(kClauses[0]);
  return kClauses;
}

}  // namespace reader
