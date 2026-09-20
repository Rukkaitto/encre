# What the scenarios actually catch

A tracked record, because an untracked proof is a claim. Each row is a change made
to `shell/src/main.cpp`, confirmed to have landed and to have recompiled, and the
scenarios that failed because of it.

**A green suite is not evidence until a mutation has made it red.** This file is
where that evidence lives for `shell/`, which until #179 had no harness at all.

## The mutations

| # | Change | Site | Scenarios that fail |
|---|---|---|---|
| 1 | delete `gApp->dispatch(ev)` | `main.cpp`, inside `loop()` | `press_down_on_home` |
| 2 | `BTN_LEFT` maps to `Button::Left` instead of `Up` | the 7-entry switch in `loop()` | `every_button_maps` |
| 3 | `display.skipInitialResync()` before the boot's `requestResync()` | `setup()` | every boot scenario |
| 4 | the wake refusal does not call `markSleeping()` | `requireHeldPowerButtonOrSleepAgain` | `wake_refused_short_press` |
| 5 | `kWakeHoldMs = 0` | the wake gate's constant | both wake scenarios |

### 1 — the recorded defect this epic exists for

A deleted `gApp->dispatch(ev)` made every button on every screen do nothing, and
the firmware still built while all 803 desktop tests passed, because nothing on the
desktop reached that loop. It now fails a scenario.

### 2 — CLOSED by `every_button_maps`

This row was a **known hole** when it was first written: no scenario pressed
`BTN_LEFT`, so nothing could notice where it pointed — the same shape as the
original defect, where the side buttons did nothing for two phases while
`test_gesture.cpp` never mentioned `Left` or `Right` at all.

`every_button_maps` presses six of the seven and asserts six distinct names in the
`[i]` line. The mutation now fails it with `#3 UP` becoming `#3 LEFT`, and fails
**nothing else** — `press_down_on_home` still passes, because it presses a
different button. That discrimination is the point: a scenario set that failed
everything on every mutation would say only that it runs.

`BTN_POWER` is deliberately not in it. It sleeps, which ends the scenario; that is
`power_press_sleeps`' job, and mixing them would make one transcript assert the
mapping of six buttons and the sleep of a seventh.

### 4 and 5 — the wake gate, which nothing on the desktop had ever executed

CLAUDE.md: *"not one line of this gate is executed by the desktop suite — 1250
green test cases say nothing about it"*, and the only way to exercise it was a
finger on a device.

`wake_refused_short_press` is 23 lines and pins all of it: `takeSleptFlag()`
consuming the flag, the pin read as active-LOW, the refusal, the flag **given
back**, and then sleeping again — with **no `<panel> begin` and no refresh
anywhere**, because the gate sits before `display.begin()` and a refusal must
spend no waveform.

Both mutations discriminate correctly. Dropping the re-arm fails only the refusal
scenario; dropping the dwell to zero fails both wake scenarios and neither boot.

### 3 — #94, and it is caught without modelling anything

The panel fake refuses a `skipInitialResync()` with no completed refresh since
`begin()`. That is a fact about what the SHELL owns, so it needs no model of the
driver's private flags and survives any SDK change.

## Three ways these mutations lied before they told the truth

All three happened while producing the table above, in one sitting. CLAUDE.md
records the first two; the third is a variant worth adding.

**The build was stale.** `cp` restoring a backup within the same second as the
previous compile leaves make thinking the object is current, so the first whole
batch ran against a binary that had none of the mutations in it — and reported
that every one of them "passed". `touch` is not reliably enough either. **Delete
the object file**:

```
rm -f build/CMakeFiles/shell_under_test.dir/shell/src/main.cpp.o
```

and then confirm the rebuild happened before believing a result.

**The mutation landed somewhere the input never reaches.** There are two
`gApp->dispatch(ev);` calls — one in `dispatchBack()` and one in `loop()` — and a
`perl -pi` without `/g` took the first. `dispatchBack()` is not on any of these
scenarios' paths, so the run passed and said nothing. **Anchor on surrounding
context, not on the statement**, and check the line number you hit.

**The candidate file was stale.** The runner writes
`build/<scenario>_candidate.txt` **only on failure**, so a passing run leaves the
previous one in place. Grepping it after a pass reads the last failure's output and
looks like a live result. **`rm -f build/*_candidate.txt` before a batch.**

## The rule that stands above all of this

**Commit before you mutate**, and restore with `cp` from a backup taken before the
edit — never `git checkout`, which reverts the mutation *and the change being
tested* and makes the next run report numbers for code that no longer exists.
