#!/usr/bin/env python3
"""Release notes for a tag, from the commit log, which IS the changelog here.

    python3 tools/release_notes.py --tag v0.1.0

Printed to stdout; `.github/workflows/release.yml` feeds it to
`gh release create --notes-file`. Runnable here for the same reason
check_conventions.py is: a release is cut rarely enough that finding out what
the notes look like only after pushing the tag is finding out too late.

WHY THE LOG AND NOT `gh release --generate-notes`. GitHub's generator groups by
pull request author and appends a contributors list, which on a one-person
repository says nothing at all, and it ignores the one thing that does carry
information: subjects are Conventional Commits and the convention is ENFORCED on
every pull request, in CI and in two git hooks, so `%s` is already a written
changelog entry. docs/releasing.md has said `git log --no-merges --format='- %s'`
since before this file existed; this is that command with the two edges handled.

THE TWO EDGES, which are the whole reason this is a tracked script with a test
rather than four lines of bash in a workflow:

  THE FIRST TAG HAS NO PREDECESSOR. `git describe --tags --abbrev=0 <tag>^`
  aborts with `fatal: No names found` on a repository with no earlier tag, and
  that is not an error -- it is v0.1.0, the case this repo is actually in. The
  range is then the whole history, which is right: v0.1.0 contains all of it.

  MERGES ARE EXCLUDED, and that is not cosmetic here. This repo squash-merges,
  so a pull request arrives as ONE commit with the PR title as its subject --
  already the entry we want. What `--no-merges` drops is `git merge main` into a
  branch, whose subject git wrote and which check_conventions.py exempts for
  that same reason. Including them would list the same work twice, once in
  somebody else's words.

WHAT IT DELIBERATELY DOES NOT DO: group by type. docs/releasing.md offers
grouping as an option for a human pasting the list ("grouped by type if it
helps"); doing it here would mean a second subject parser beside
check_conventions.py's, free to disagree with it about what a type is.

THE X4 CAVEAT IS ASKED FOR, NOT HARDCODED. docs/releasing.md instructs that the
notes must say a version number does not imply the reader works on an X4 --
every device measurement in this repo was taken on an X3 and rotation is
unverified there (issue #23). An instruction like that, carried out by a
generator, becomes a claim with an expiry date and no owner, which is this
project's most expensive recurring defect: it would still be printed the day #23
closes and nobody would come back here. So the line is emitted only while #23 is
OPEN, read live from the tracker, and it retires itself. If the query cannot be
made at all -- no `gh`, no network, no token -- the line is INCLUDED: an
unverified limitation stated once too often is a smaller wrong than a real one
dropped silently, and the alternative fails toward the release that overclaims.
"""
import argparse
import json
import subprocess
import sys

# Issue #23: rotation is CCW measured on the X3 and unverified on the X4.
X4_ISSUE = 23
X4_CAVEAT = (
    "> **Tested on an X3 only.** Every device measurement in this project was "
    "taken on an Xteink X3 (UC8279). One binary drives both models, but "
    "rotation is unverified on the X4 -- see "
    "https://github.com/{repo}/issues/{issue}."
)


def git(*args):
    return subprocess.run(["git", *args], capture_output=True, text=True,
                          check=True).stdout.strip("\n")


def previous_tag(tag):
    """The nearest tag before `tag`, or None if `tag` is the first one.

    Asked of `<tag>^` rather than of `<tag>`, which would describe the tag as
    itself. None is an ordinary answer and not a failure: it is the first
    release, and the range is then the whole history.
    """
    p = subprocess.run(["git", "describe", "--tags", "--abbrev=0", f"{tag}^"],
                       capture_output=True, text=True)
    if p.returncode != 0:
        return None
    return p.stdout.strip() or None


def resolve(tag):
    """The commit `tag` names, or None if there is no such ref.

    Checked before anything else because the alternative is a traceback: a tag
    that was never fetched is the ordinary way this is called wrongly -- a
    shallow checkout, or a typo in the workflow -- and `git log` on an unknown
    ref raises CalledProcessError out of git(). A tool whose failure mode for
    the commonest mistake is a Python stack trace is one nobody can act on.
    """
    p = subprocess.run(["git", "rev-parse", "--verify", "--quiet",
                        f"{tag}^{{commit}}"], capture_output=True, text=True)
    if p.returncode != 0:
        return None
    return p.stdout.strip()


def subjects(tag, since=None):
    """The subject of every non-merge commit this release adds.

    Newest first, which is `git log`'s own order and the order a reader wants:
    what changed most recently is at the top.
    """
    rev = f"{since}..{tag}" if since else tag
    out = git("log", "--no-merges", "--format=%s", rev)
    return [line for line in out.split("\n") if line.strip()]


def x4_issue_open(repo):
    """Is the X4 rotation issue still open? True if it cannot be determined.

    Fails toward INCLUDING the caveat. Getting this wrong in the other
    direction means a release that quietly claims more hardware than anybody
    has tested, which is the false-claim shape this project refuses for an
    unread battery gauge (-1, never 0%) and for a badge promising a wake that
    charging cannot deliver.
    """
    p = subprocess.run(
        ["gh", "issue", "view", str(X4_ISSUE), "--repo", repo,
         "--json", "state"],
        capture_output=True, text=True)
    if p.returncode != 0:
        return True
    try:
        return json.loads(p.stdout).get("state", "").upper() != "CLOSED"
    except (json.JSONDecodeError, AttributeError):
        return True


def render(tag, since, lines, caveat=None, repo="Rukkaitto/encre"):
    """The note body. Pure, so the test can assert on it without a tracker.

    The `since` link is absolute rather than relative: a release body is
    rendered at /releases/tag/<tag>, and a relative href there resolves against
    a path that is not the one it looks like.
    """
    if since:
        span = (f"Changes since "
                f"[{since}](https://github.com/{repo}/releases/tag/{since}).")
    else:
        span = "The first tagged release; this is the whole history."

    body = [span, ""]
    if caveat:
        body += [caveat, ""]
    body += [f"- {s}" for s in lines]
    # No claim about what is attached: the release page lists its own assets,
    # and a body that names them is a second copy free to be wrong when this
    # script is run somewhere that attaches nothing.
    body += ["", f"{len(lines)} commit(s)."]
    return "\n".join(body)


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tag", required=True,
                    help="the tag being released, e.g. v0.1.0")
    ap.add_argument("--repo", default="Rukkaitto/encre",
                    help="owner/name, for issue links and the X4 caveat query")
    ap.add_argument("--no-caveat-query", action="store_true",
                    help="skip the tracker lookup and always emit the X4 "
                         "caveat. For running this offline, where the query "
                         "would answer 'include' anyway.")
    args = ap.parse_args()

    if resolve(args.tag) is None:
        print(f"error: no such tag or commit: {args.tag}. Nothing to write "
              f"notes from -- if this is CI, the tag was not fetched.",
              file=sys.stderr)
        return 1

    since = previous_tag(args.tag)
    lines = subjects(args.tag, since)

    # An empty range means every commit it selected was a merge. Normal use
    # does not reach this -- a release adds at least its own commit, and this
    # repository squash-merges -- so it is a guard rather than a case, and it
    # is here for the reason `--require-commits` is in check_conventions.py: a
    # generator that examined nothing must not report success by printing an
    # empty list, which on a release page reads as a build that changed nothing.
    if not lines:
        print(f"error: {since or 'the repository root'}..{args.tag} selects no "
              f"non-merge commits. Refusing rather than publishing notes that "
              f"list nothing.", file=sys.stderr)
        return 1

    caveat = None
    if args.no_caveat_query or x4_issue_open(args.repo):
        caveat = X4_CAVEAT.format(repo=args.repo, issue=X4_ISSUE)

    print(render(args.tag, since, lines, caveat, args.repo))
    return 0


if __name__ == "__main__":
    sys.exit(main())
