#!/usr/bin/env python3
"""What a release is waiting on, read live from the project board.

    python3 tools/release_blockers.py --release V1.1

Prints one TSV line per card in that release that is not `Done`, sorted:
status, issue number (or `draft`), kind, title. docs/releasing.md's gate opens
with this; everything it prints is work that has to land before the tag.

WHY A SCRIPT AND NOT THE `gh ... | jq` ONE-LINER THIS REPLACES. The one-liner
could not fail. It hardcoded `--limit 100` against a board that has since
reached 114 items, and `gh project item-list` truncates silently -- so a real
blocker fell off the end and the gate read as a pass. That is the
reports-on-less-than-it-claims shape this project refuses for the card probe
answered from cache, for the `make compare` default that skipped four screens,
and for `logToCard`. Three of the four refusals below cannot be written as a
pasteable pipeline at all, and the fourth is worse than useless in one:

  TRUNCATION IS NOW IMPOSSIBLE RATHER THAN MERELY LARGER. Raising the limit to
  200 reintroduces the same defect one board-year later, so there is no limit
  here to raise. `totalCount` is the board's own count and does NOT shrink with
  `--limit` (measured: 114 at limits 5, 50, 100, 114 and 200), so the size is
  ASKED FOR -- one call with `--limit 1` to learn it, a second that asks for
  exactly that many. The equality check survives as the belt to that braces: a
  card filed between the two calls makes them disagree, and a gate that cannot
  tell a complete answer from a truncated one is the defect, not the number.

  AN UNKNOWN RELEASE IS AN ERROR, NOT AN EMPTY ANSWER. `--release v1.1` against
  a board spelling it `V1.1` selects nothing and exits 0, which reads exactly
  like a release with no blockers left -- a false pass produced by a typo. The
  name is checked against the Release field's own options, read live, for the
  reason compare-design.py's `--only` errors on an id it does not recognise
  rather than reporting `0/0`. Read live and not listed here because the option
  list is the fragile part: CLAUDE.md records that table going stale TWICE, the
  same way both times, because a release was added and nothing noticed.

  A CARD WITH NO RELEASE IS IN NO GATE. It appears in no release's query, so it
  can only be found by looking. That is a note rather than a refusal -- it hides
  nothing from the release being asked about -- but it must not be silent.

  AND THE PIPELINE SWALLOWS THE FAILURE. `gh ... | jq ... | sort` reports
  `sort`'s exit status, so a jq that refused mid-stream leaves a pipeline that
  printed nothing and exited 0. The fix for a gate that passes silently must not
  itself pass silently.

EXIT CODES ARE THREE-WAY, because "clear" and "could not answer" are different
claims and this file exists to keep them apart:

    0  the release is clear -- nothing is not-Done
    1  blockers remain, and are on stdout
    2  the question could not be answered; the reason is on stderr

WHAT IT DELIBERATELY DOES NOT DO: hide the release card. docs/releasing.md used
to carve out "everything that prints is a blocker, except the release card
itself", and a script that guessed which card that was -- by title, there being
nothing else to go on -- would hide a real blocker the day a title happened to
match. The carve-out is prose in that file, where a human applies it to a line
they can see. An absent claim beats a false one, and a hidden row is neither.
"""
import argparse
import json
import shutil
import subprocess
import sys

BOARD = 1
OWNER = "Rukkaitto"
RELEASE_FIELD = "Release"

CLEAR = 0
BLOCKERS_REMAIN = 1
# Kept distinct from BLOCKERS_REMAIN so that "could not reach the board" cannot
# be read as a verdict about the release, in either direction.
CANNOT_ANSWER = 2


class Unanswerable(Exception):
    """The board could not be read, or was read incompletely.

    Raised rather than returned so that no caller can carry on with a partial
    answer by forgetting to check one. Every message is written to be actionable
    on its own -- this is read by somebody about to cut a tag.
    """


def gh_json(args):
    """Run `gh` and parse its stdout as JSON, or refuse with a reason.

    stderr is kept OUT of the parsed stream deliberately. The pipeline this
    replaces merged it in, and gh writing anything at all -- a deprecation
    notice, a rate-limit warning -- then arrives as a JSON parse error about a
    control character, which names neither the tool nor the cause.
    """
    if shutil.which("gh") is None:
        raise Unanswerable(
            "gh is not on PATH. The board is the only index of this project's "
            "deferred work, so there is no offline answer to this question.")
    p = subprocess.run(["gh", *args], capture_output=True, text=True)
    if p.returncode != 0:
        detail = (p.stderr or p.stdout).strip().splitlines()
        hint = detail[0] if detail else f"gh exited {p.returncode}"
        if "scope" in hint.lower():
            hint += ("  -- reading the board needs the `project` scope: "
                     "`gh auth refresh -s project` (interactive).")
        raise Unanswerable(f"`gh {' '.join(args)}` failed: {hint}")
    try:
        return json.loads(p.stdout)
    except json.JSONDecodeError as e:
        raise Unanswerable(
            f"`gh {' '.join(args)}` did not return JSON ({e}). "
            f"First 200 bytes: {p.stdout[:200]!r}")


def item_list(board, owner, limit):
    return gh_json(["project", "item-list", str(board), "--owner", owner,
                    "--format", "json", "--limit", str(limit)])


def fetch_items(board=BOARD, owner=OWNER):
    """Every item on the board, or refuse. Never a prefix of them.

    Two calls on purpose. The first asks for one item and reads `totalCount`,
    which is the board's own figure and independent of `--limit`; the second
    asks for exactly that many. So there is no constant here to outgrow -- the
    board can reach any size and this keeps working.
    """
    head = item_list(board, owner, 1)
    try:
        total = int(head["totalCount"])
    except (KeyError, TypeError, ValueError):
        raise Unanswerable(
            "gh did not report a totalCount, so a complete answer cannot be "
            "told from a truncated one. Refusing rather than gating on a "
            "prefix of the board.")

    if total == 0:
        return []

    got = item_list(board, owner, total).get("items", [])
    if len(got) != total:
        # The race: a card filed between the two calls. Self-correcting, so say
        # so -- the alternative is the silent truncation this script exists for.
        raise Unanswerable(
            f"the board reports {total} items and returned {len(got)}. "
            f"If a card was filed while this ran, run it again; if it keeps "
            f"disagreeing, `gh project item-list` has stopped reporting a "
            f"count that means what this assumes.")
    return got


def release_options(board=BOARD, owner=OWNER):
    """The Release field's option names, in board order.

    The authority for what a release is CALLED. Not the values items happen to
    carry: a release whose cards have all landed, or that has none yet, is a
    legitimate thing to gate on and carries no items at all.
    """
    fields = gh_json(["project", "field-list", str(board), "--owner", owner,
                      "--format", "json"]).get("fields", [])
    for f in fields:
        if f.get("name") == RELEASE_FIELD:
            options = [o["name"] for o in f.get("options") or []]
            if not options:
                raise Unanswerable(
                    f"the {RELEASE_FIELD} field carries no options, so no "
                    f"release name can be checked against it.")
            return options
    raise Unanswerable(
        f"the board has no {RELEASE_FIELD} field. It has: "
        f"{', '.join(f.get('name', '?') for f in fields) or '(nothing)'}.")


def blockers(items, release):
    """The not-Done cards of `release`, as sorted TSV rows.

    Pure, so the selection can be tested without a tracker -- which is the half
    that decides a release. A card with no `status` counts as not-Done: an
    unstatused card is certainly not finished, and reading a missing field as
    `Done` would drop it from the gate.
    """
    rows = []
    for it in items:
        if it.get("release") != release:
            continue
        if it.get("status") == "Done":
            continue
        number = (it.get("content") or {}).get("number") or "draft"
        rows.append("\t".join([
            it.get("status") or "(no status)",
            str(number),
            it.get("kind") or "",
            it.get("title") or "",
        ]))
    return sorted(rows)


def unreleased(items):
    """Cards carrying no Release, which therefore appear in no gate."""
    return [it for it in items if not it.get("release")]


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--release", required=True,
                    help="the release to gate, e.g. V1.1. Checked against the "
                         "board's own Release options.")
    ap.add_argument("--board", type=int, default=BOARD)
    ap.add_argument("--owner", default=OWNER)
    ap.add_argument("--limit", type=int, default=None,
                    help=argparse.SUPPRESS)  # tests only; see below.
    args = ap.parse_args()

    try:
        options = release_options(args.board, args.owner)
        if args.release not in options:
            raise Unanswerable(
                f"{args.release!r} is not a release on this board. It has: "
                f"{', '.join(options)}. Selecting a release that does not "
                f"exist finds nothing wrong, which is a pass nobody earned.")

        if args.limit is not None:
            # A deliberately truncating fetch, so the refusal above it can be
            # demonstrated. Not a supported way to run the gate.
            got = item_list(args.board, args.owner, args.limit)
            total = int(got.get("totalCount", 0))
            n = len(got.get("items", []))
            if n != total:
                raise Unanswerable(
                    f"the board reports {total} items and returned {n}. "
                    f"--limit {args.limit} truncated the answer; a gate that "
                    f"cannot see the whole board cannot clear a release.")
            items = got.get("items", [])
        else:
            items = fetch_items(args.board, args.owner)
    except Unanswerable as e:
        print(f"error: {e}", file=sys.stderr)
        return CANNOT_ANSWER

    orphans = unreleased(items)
    if orphans:
        print(f"note: {len(orphans)} card(s) carry no Release and so appear in "
              f"no release's gate: "
              f"{', '.join(sorted(o.get('title', '?') for o in orphans))}",
              file=sys.stderr)

    rows = blockers(items, args.release)
    for r in rows:
        print(r)

    if rows:
        print(f"\n{len(rows)} blocker(s) for {args.release}, "
              f"of {len(items)} cards on the board.", file=sys.stderr)
        return BLOCKERS_REMAIN

    print(f"{args.release} is clear: nothing not-Done, "
          f"of {len(items)} cards on the board.", file=sys.stderr)
    return CLEAR


if __name__ == "__main__":
    sys.exit(main())
