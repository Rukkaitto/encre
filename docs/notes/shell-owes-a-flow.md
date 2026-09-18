# What the shell owes a flow, and two ways it silently owes nothing

Extracted from `CLAUDE.md`, which keeps a stub under this heading and is where
the cross-references to it point. Same standing as anything in that file.

**A REFUSED PUSH IS SILENT BY DESIGN, AND THAT IS INDISTINGUISHABLE FROM A DEAD
BUTTON.** `App::dispatch`'s Push case ignores `pushScreen`'s `false`, so a
factory that refuses marks nothing dirty and nothing reaches the glass. That is
right for a wake restore — it stops short of a screen it cannot build and
leaves what stands — and it is how V1.1's Wi-Fi flow shipped with **three
separate dead controls**, each reported off the device as "pressing X does
nothing":

| the press | what was not primed |
|---|---|
| Settings' `Wi-Fi` row | `setWifiNetworks` — `shell/` had no Wi-Fi code at all |
| the hub's SETUP row | `setWifiScan`, which is why an EMPTY scan is primed at boot |
| the HOLD on a saved network | `setWifiNetworkFacts`, re-primed every iteration the hub is on top |

**THE COMMON SHAPE IS A SCREEN THAT PUSHES DIRECTLY.** Each of those returns
`Action::push(...)` from its own `onGesture`, so the shell never sees the press
and cannot prime in response to it — whatever the factory needs has to be there
**before** the gesture. A latch would have let the shell prime and then push,
and that is the trade: a push is one line in the screen, a latch is a handler
in the shell. Where the payload is cheap and stateless, prime it continuously.

**AND A SCREEN'S OWN TEST CANNOT SEE ANY OF IT.** `CHECK(a.kind == Push && a.target == X)`
asserts what the screen RETURNS, which was correct in all three cases. Whether
the push SUCCEEDS is a fact about the factory and the shell, and `shell/` has no
harness. The catalogue guard in `test_focus_restore.cpp` requires every screen
to be CONSTRUCTIBLE, not reachable.

**`dispatchBack()` SENDS A PRESS, SO WHAT IT DOES IS WHATEVER THAT SCREEN'S
`onGesture` DOES WITH BACK.** That is right for `DeleteConfirm`, `BookEnd` and
the reader menu, whose Backs return a pop — the screen decides, once, and
nothing in the shell can drift from it. It is wrong for a screen whose Back
LATCHES: every connect-flow screen answers Back with `Action::wifi()`, so a
cancel handler that synthesised a Back **re-latched the request it was
serving** and the screen never left. An infinite loop, reaching the glass as a
hint that does nothing. `App::popScreen()` is the other tool, and
`dispatchBack`'s own header carries the rule for choosing. The note beside
`handleDelete` warning that a Back which does not pop "would spin loop()
forever" was written before either existed, and is exactly what this cost.

**A HEADER NAMED `wifi.h` IN `shell/src/` SHADOWS ARDUINO'S `<WiFi.h>`.** macOS's
filesystem is case-INSENSITIVE by default, so the sibling translation unit's
`#include <WiFi.h>` resolved to ours and the build failed with `'WiFi' was not
declared` against a header the compiler had happily opened. **It would have
built correctly on a case-sensitive volume**, which is the worse half. The file
is `wifi_store_nvs.h`.
