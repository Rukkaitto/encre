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
| 2 | `BTN_LEFT` maps to `Button::Left` instead of `Up` | the 7-entry switch in `loop()` | **none — see below** |
| 3 | `display.skipInitialResync()` before the boot's `requestResync()` | `setup()` | all three |

### 1 — the recorded defect this epic exists for

A deleted `gApp->dispatch(ev)` made every button on every screen do nothing, and
the firmware still built while all 803 desktop tests passed, because nothing on the
desktop reached that loop. It now fails a scenario.

### 2 — NOT CAUGHT, and the gap is in the SCENARIO SET rather than the harness

No scenario presses `BTN_LEFT`, so nothing can notice where it points. That is the
same shape as the original defect: the side buttons did nothing for two phases
while `test_gesture.cpp` never mentioned `Left` or `Right` at all.

The scenario that closes it is `every_button_maps` — seven presses, one per
`BTN_*`, asserting seven distinct names in the `[i]` line. Until that exists this
row is a **known hole**, written down rather than left to be rediscovered.

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
