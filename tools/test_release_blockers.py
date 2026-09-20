#!/usr/bin/env python3
"""Tests for release_blockers.py, against a stubbed `gh`.

    python3 tools/test_release_blockers.py

DELIBERATELY NOT WIRED INTO `make test`, for tools/test_release_notes.py's and
tools/test_check_conventions.py's reason: the fast loop builds on a bare
checkout with no Python, and every generated asset is committed precisely so it
can. These are the tests for a tool, run when the tool changes.

WHY `gh` IS STUBBED AND THE BOARD IS NOT READ. Every defect this file guards
against is a REFUSAL -- a truncated answer, a release name that does not exist,
a tracker that cannot be reached -- and not one of them can be produced on
demand against the real board without filing or deleting cards. A test that
asked the live board would also change its verdict the day somebody moves a
card, which is the drift the query exists to avoid, reintroduced in the thing
checking it. So the board is a fixture and `gh` is a shell script on PATH.

WHAT THE FIXTURE HAS TO CONTAIN, and it took two goes. The obvious board --
every card in the release under test -- cannot see the truncation refusal at
all: if every item is the release's own, a prefix of the board is still a prefix
of that release and the row count merely looks smaller, which is what the
shipped defect looked like for a board-year. The fixture therefore puts the
release's cards at the END, after the cards of another release, so that a
truncating fetch returns a COMPLETE-LOOKING answer for the release being gated
-- zero blockers, exit 0, a clean pass. That is the actual shape of the bug and
the only fixture that reproduces it.
"""
import importlib.util
import json
import os
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
SCRIPT = ROOT / "tools" / "release_blockers.py"

FAILURES = []


def check(ok, label, detail=""):
    print(("  ok   " if ok else "  FAIL ") + label
          + (f"   [{detail}]" if detail and not ok else ""))
    if not ok:
        FAILURES.append(label)


def load():
    spec = importlib.util.spec_from_file_location("release_blockers", SCRIPT)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


# --------------------------------------------------------------------------
# The fixture board.
# --------------------------------------------------------------------------

def card(release, status, number=None, kind="Engine", title="a card"):
    it = {"release": release, "status": status, "kind": kind, "title": title}
    if number is not None:
        it["content"] = {"number": number, "title": title}
    return it


def board():
    """Ten cards. The gated release's are LAST -- see the module docstring.

    V1 is finished, which is also the real board's state, so it doubles as the
    "a clear release is not an error" case.
    """
    return [
        card("V1", "Done", 30, "Docs", "Tag v0.1.0"),
        card("V1", "Done", 1, "Screen", "Peek overlay"),
        card("V1.2", "Todo", 20, "Engine", "TXT importer"),
        card("V1.2", "Todo", 18, "Engine", "Hyphenation"),
        card("Someday", "Todo", None, "Engine", "OPDS"),
        card("Someday", "Boarded", None, "Screen", "Theme picker"),
        # Everything from here is V1.1 and is what a truncated fetch drops.
        card("V1.1", "Building", 116, "Tooling", "The release blocker query"),
        card("V1.1", "Boarded", 7, "Screen", "Home / missing book"),
        card("V1.1", "Todo", 112, "Engine", "Instapaper sync"),
        card("V1.1", "Done", 120, "Screen", "SleepCoverDetails"),
    ]


FIELDS = {"fields": [
    {"name": "Title", "id": "PVTF_x"},
    {"name": "Status", "id": "PVTSSF_s",
     "options": [{"name": n, "id": n} for n in
                 ["Todo", "Needs a board", "Boarded", "Building",
                  "On glass", "Done"]]},
    {"name": "Release", "id": "PVTSSF_r",
     "options": [{"name": n, "id": n} for n in
                 ["V1", "V1.1", "V1.2", "V2", "Someday"]]},
]}


def gh_stub(tmp, items, fields=None, broken=None, cap=None):
    """A directory holding a fake `gh` answering item-list and field-list.

    `cap` truncates item-list to that many items while still reporting the true
    totalCount, which is exactly what the real CLI does and is the whole defect.
    `broken` replaces gh with a failure: None means a working stub, "missing"
    empties the PATH of it, "scope" is the token refusal, "garbage" is non-JSON.
    """
    bindir = tmp / "bin"
    bindir.mkdir(exist_ok=True)
    gh = bindir / "gh"

    if broken == "missing":
        # Not merely a failing gh: the PATH has no gh at all, which is what a
        # machine without the CLI actually looks like and is a different branch.
        gh.unlink(missing_ok=True)
        return bindir
    if broken == "scope":
        gh.write_text("#!/bin/sh\n"
                      "echo 'your authentication token is missing required "
                      "scopes [project]' >&2\nexit 1\n")
        gh.chmod(0o755)
        return bindir
    if broken == "garbage":
        gh.write_text("#!/bin/sh\nprintf 'not json at all'\n")
        gh.chmod(0o755)
        return bindir

    (tmp / "items.json").write_text(json.dumps(items))
    (tmp / "fields.json").write_text(json.dumps(fields or FIELDS))
    # The limit is argv's last word for item-list. A real `gh` returns the
    # first N and reports the board's own totalCount regardless -- measured on
    # the live board at limits 5, 50, 100, 114 and 200, all reporting 114.
    gh.write_text(f"""#!/bin/sh
if [ "$2" = "field-list" ]; then cat {tmp}/fields.json; exit 0; fi
# The LAST argument, POSIX. `${{@: -1}}` is a bash array slice: it works under
# macOS's bash-as-sh and is `Bad substitution` under Ubuntu's dash, so this
# file passed on every developer machine and failed the first time CI ran it.
for limit; do :; done
[ -n "{cap or ''}" ] && [ "$limit" -gt "{cap or 0}" ] && limit={cap or 0}
python3 -c '
import json,sys
items=json.load(open(sys.argv[1]))
n=int(sys.argv[2])
print(json.dumps({{"items":items[:n],"totalCount":len(items)}}))
' {tmp}/items.json "$limit"
""")
    gh.chmod(0o755)
    return bindir


def run(*args, items=None, fields=None, broken=None, cap=None):
    """(returncode, stdout, stderr) for a real invocation of the script."""
    with tempfile.TemporaryDirectory() as d:
        tmp = pathlib.Path(d)
        bindir = gh_stub(tmp, board() if items is None else items,
                         fields, broken, cap)
        env = dict(os.environ)
        if broken == "missing":
            # An empty PATH but for the stub dir, so `gh` cannot be found and
            # `python3`/`sh` are still reachable by absolute path in the script.
            env["PATH"] = str(bindir)
        else:
            env["PATH"] = str(bindir) + os.pathsep + env["PATH"]
        p = subprocess.run([sys.executable, str(SCRIPT), *args],
                           capture_output=True, text=True, env=env)
        return p.returncode, p.stdout, p.stderr


# --------------------------------------------------------------------------
# The selection: which cards are a release's blockers.
# --------------------------------------------------------------------------

def test_selects_not_done_in_one_release():
    print("\nthe gate is 'not Done, in this release'")
    mod = load()
    rows = mod.blockers(board(), "V1.1")
    check(len(rows) == 3, "three of the four V1.1 cards are not Done", rows)
    check(all("\t" in r for r in rows), "TSV, as the doc's query always was")
    check(not any("SleepCoverDetails" in r for r in rows),
          "the Done one is excluded", rows)
    check(not any("TXT importer" in r for r in rows),
          "another release's cards are excluded", rows)
    check(rows == sorted(rows), "sorted, so status groups together", rows)


def test_every_status_but_done_blocks():
    print("\n`On glass` is NOT `Done` -- an agent may never make that move")
    mod = load()
    for status in ["Todo", "Needs a board", "Boarded", "Building", "On glass"]:
        rows = mod.blockers([card("V1.1", status, 1)], "V1.1")
        check(len(rows) == 1, f"{status!r} blocks")
    check(mod.blockers([card("V1.1", "Done", 1)], "V1.1") == [],
          "`Done` does not")


def test_a_card_with_no_status_still_blocks():
    print("\na card with no status is not finished")
    mod = load()
    it = {"release": "V1.1", "title": "unstatused", "kind": "Engine"}
    rows = mod.blockers([it], "V1.1")
    check(len(rows) == 1, "it is a blocker, not a silent omission", rows)
    check("(no status)" in rows[0], "and says so rather than inventing one",
          rows)


def test_a_draft_has_no_number():
    print("\na draft item is a blocker with no issue number to cite")
    mod = load()
    rows = mod.blockers([card("V1.1", "Todo", None, "Screen", "a draft")],
                        "V1.1")
    check("draft" in rows[0], "printed as `draft`", rows)


# --------------------------------------------------------------------------
# The refusals. These are the reason this is a script.
# --------------------------------------------------------------------------

def test_truncation_is_refused_not_reported_as_a_pass():
    print("\nTRUNCATION: the defect. A prefix of the board is not an answer")
    # The old query's shape: a limit smaller than the board, no complaint.
    # The fixture's V1.1 cards are last, so a cap of 6 makes V1.1 look CLEAR.
    rc, out, err = run("--release", "V1.1", cap=6)
    check(rc == 2, f"refuses with exit 2, not a clean 0 (got {rc})", err)
    check("10" in err and "6" in err, "names both counts", err)
    check(out.strip() == "", "prints no rows it cannot vouch for", out)

    # And the proof that the fixture can see the defect at all: without the
    # refusal this run is indistinguishable from a finished release.
    mod = load()
    truncated = mod.blockers(board()[:6], "V1.1")
    check(truncated == [],
          "a truncating fetch really does make V1.1 look clear -- which is "
          "what the shipped defect looked like", truncated)


def test_the_size_is_asked_for_so_there_is_no_limit_to_outgrow():
    print("\nno constant to outgrow: the fetch asks the board how big it is")
    rc, out, err = run("--release", "V1.1")
    check(rc == 1, "a board larger than any default is read whole", err)
    check(len(out.strip().splitlines()) == 3, "all three blockers", out)
    check("116" in out, "including the one that would fall off the end", out)

    # A board of 250 cards -- past any limit somebody would have written down.
    big = [card("V1.2", "Todo", i) for i in range(240)] + board()
    rc, out, err = run("--release", "V1.1", items=big)
    check(rc == 1, "250 cards: still answered", err)
    check(len(out.strip().splitlines()) == 3, "and still all three", out)


def test_an_unknown_release_is_an_error_not_an_empty_answer():
    print("\nUNKNOWN RELEASE: a typo must not read as a finished release")
    rc, out, err = run("--release", "v1.1")
    check(rc == 2, f"exit 2, not a clean 0 (got {rc})", err)
    check("V1, V1.1, V1.2, V2, Someday" in err,
          "lists what the board actually has", err)
    check(out.strip() == "", "prints nothing", out)


def test_a_release_with_no_cards_is_clear_not_unknown():
    print("\n...but a real release with no cards is CLEAR, not unknown")
    # V2 is a board option and the fixture has no V2 card. Validating against
    # the values items carry, rather than the field's options, would call this
    # a typo -- so this is the case that decides where the check reads from.
    rc, out, err = run("--release", "V2")
    check(rc == 0, f"exit 0 (got {rc})", err)
    check("clear" in err, "and says so", err)


def test_a_finished_release_exits_zero():
    print("\na finished release is exit 0 -- V1's real state today")
    rc, out, err = run("--release", "V1")
    check(rc == 0, f"exit 0 (got {rc})", err)
    check(out.strip() == "", "nothing on stdout", out)
    check("V1 is clear" in err, "the verdict is on stderr, not mixed in", err)


def test_an_unreachable_tracker_is_not_a_verdict():
    print("\nCANNOT ANSWER: three ways, none of them a pass")
    for broken, label in [("missing", "gh not installed"),
                          ("scope", "token without the project scope"),
                          ("garbage", "gh returning non-JSON")]:
        rc, out, err = run("--release", "V1.1", broken=broken)
        check(rc == 2, f"{label} -> exit 2 (got {rc})", err)
        check(out.strip() == "", f"{label} -> no rows", out)
    _, _, err = run("--release", "V1.1", broken="scope")
    check("gh auth refresh -s project" in err,
          "the scope failure names the interactive fix", err)


def test_a_missing_release_field_is_refused():
    print("\na board with no Release field cannot gate anything")
    rc, out, err = run("--release", "V1.1",
                       fields={"fields": [{"name": "Status", "id": "s"}]})
    check(rc == 2, f"exit 2 (got {rc})", err)
    check("Release" in err, "names the field it wanted", err)


def test_a_card_with_no_release_is_noted():
    print("\na card in no release appears in no gate, so it is named")
    items = board() + [{"status": "Todo", "title": "orphan", "kind": "Engine"}]
    rc, out, err = run("--release", "V1.1", items=items)
    check("orphan" in err, "named on stderr", err)
    check(rc == 1, "but it does not change the verdict for V1.1", err)
    check("orphan" not in out, "and is not claimed as a V1.1 blocker", out)


def test_the_three_exit_codes_are_distinct():
    print("\nclear / blocked / unanswerable are three different claims")
    codes = {
        "clear": run("--release", "V1")[0],
        "blocked": run("--release", "V1.1")[0],
        "unanswerable": run("--release", "V1.1", broken="missing")[0],
    }
    check(len(set(codes.values())) == 3, "all distinct", codes)
    check(codes == {"clear": 0, "blocked": 1, "unanswerable": 2},
          "and are 0 / 1 / 2 as documented", codes)


def main():
    test_selects_not_done_in_one_release()
    test_every_status_but_done_blocks()
    test_a_card_with_no_status_still_blocks()
    test_a_draft_has_no_number()
    test_truncation_is_refused_not_reported_as_a_pass()
    test_the_size_is_asked_for_so_there_is_no_limit_to_outgrow()
    test_an_unknown_release_is_an_error_not_an_empty_answer()
    test_a_release_with_no_cards_is_clear_not_unknown()
    test_a_finished_release_exits_zero()
    test_an_unreachable_tracker_is_not_a_verdict()
    test_a_missing_release_field_is_refused()
    test_a_card_with_no_release_is_noted()
    test_the_three_exit_codes_are_distinct()

    print()
    if FAILURES:
        print(f"{len(FAILURES)} FAILED:")
        for f in FAILURES:
            print(f"  - {f}")
        return 1
    print("all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
