#!/usr/bin/env python3
"""Branch name and commit message conventions, checked in CI and runnable here.

Runnable HERE is the point. A convention enforced only by CI is one you learn
about after pushing, which is the worst moment to be told to rewrite a commit
message; `make conventions` answers the same question before the push.

WHAT IT ENFORCES, and what it deliberately does not:

  Branch names take the git-flow vocabulary (feature/bugfix/hotfix/release/
  support) plus the housekeeping prefixes this repo actually uses, plus
  `claude/`. Claude Code NAMES ITS OWN BRANCHES `claude/<slug>`, so a pattern
  without it would reject every agent branch -- including the one that added
  this file -- and the cost would be a rename before every PR rather than any
  improvement in clarity. There is no `develop` branch and this does not invent
  one: full git flow is a change to how the project is developed, not a check.

  Commit subjects take Conventional Commits with the ELEVEN STANDARD TYPES and
  a free-form scope. The house style writes the subsystem as the type --
  `peek:`, `design:`, `reader:` -- which is a scope wearing a type's clothes;
  `feat(peek):` says the same thing, validates against a stock config, and adds
  the one bit of information the bare area name never carried. Measured over
  main at the time this landed: 358 of 513 subjects already passed, and 155
  failed, almost all of them that one way.

  The scope vocabulary is NOT restricted. A list of allowed scopes needs a line
  per subsystem and is a merge conflict every time a screen is added, which is
  the same maintenance shape this repo avoids elsewhere.

  Subject LENGTH is not enforced. Conventional Commits says nothing about it and
  this project writes long, explanatory subjects on purpose.

  A merge commit is exempt. `git merge main` writes its own subject and the
  person merging did not choose it.
"""
import argparse
import re
import subprocess
import sys

# The eleven standard Conventional Commits types.
TYPES = ["feat", "fix", "docs", "style", "refactor",
         "perf", "test", "build", "ci", "chore", "revert"]

# type(scope)!: subject -- scope and `!` optional, subject required non-empty.
SUBJECT_RE = re.compile(r"^(?:%s)(?:\([^()\n]+\))?!?: \S.*$" % "|".join(TYPES))

# git-flow's own vocabulary, the housekeeping prefixes in use here, and the
# agent branches Claude Code generates.
BRANCH_PREFIXES = ["feature", "bugfix", "hotfix", "release", "support",
                   "chore", "docs", "ci", "refactor", "test", "perf", "claude"]
BRANCH_RE = re.compile(r"^(?:%s)/[a-z0-9][a-z0-9._\-/]*$" % "|".join(BRANCH_PREFIXES))

# Long-lived branches answer to nobody.
EXEMPT_BRANCHES = {"main", "master", "develop"}


def git(*args):
    # .strip("\n") rather than .strip(): PYTHON COUNTS \x1f AS WHITESPACE
    # (`"\x1f".isspace()` is True), and \x1f is the field separator below. A bare
    # .strip() therefore ate the trailing separator of the LAST line -- which is
    # the ROOT commit, the one commit with no parents and so an empty final
    # field -- and the parse crashed on it. Found by running this over the real
    # 513-commit history rather than over the fixtures, which all had parents.
    return subprocess.run(["git", *args], capture_output=True, text=True,
                          check=True).stdout.strip("\n")


def check_branch(name):
    """[] if the branch name is fine, else a list of complaints."""
    if name in EXEMPT_BRANCHES:
        return []
    if BRANCH_RE.match(name):
        return []
    return ["branch %r does not match <prefix>/<slug>\n"
            "    prefixes: %s\n"
            "    the slug is lowercase letters, digits, dot, underscore, hyphen\n"
            "    e.g. feature/peek-overlay, bugfix/anchor-high-water, docs/ci"
            % (name, ", ".join(BRANCH_PREFIXES))]


def commits_in(rev_range):
    """(sha, subject, parent_count) for each commit in the range, newest first."""
    if not rev_range:
        return []
    # %P is the parent list; a merge has two, and is exempt.
    out = git("log", "--format=%H\x1f%s\x1f%P", rev_range)
    rows = []
    for line in out.split("\n"):
        if not line.strip():
            continue
        # Padded rather than unpacked directly: an empty trailing field is a
        # real case (the root commit has no parents) and must not be a crash.
        fields = (line.split("\x1f") + ["", "", ""])[:3]
        sha, subject, parents = fields
        rows.append((sha, subject, len(parents.split())))
    return rows


def check_commits(rows):
    """[] if every non-merge subject conforms, else a list of complaints."""
    problems = []
    for sha, subject, parent_count in rows:
        if parent_count > 1:
            continue  # a merge commit's subject is git's, not the author's
        if subject.startswith(("fixup!", "squash!", "amend!")):
            problems.append(
                "%s %r is a fixup and must be squashed before merging"
                % (sha[:8], subject))
            continue
        if not SUBJECT_RE.match(subject):
            problems.append(
                "%s %r\n"
                "    expected <type>(<optional scope>): <subject>\n"
                "    types: %s\n"
                "    the house style writes the subsystem as the type; it is a\n"
                "    SCOPE -- `peek:` becomes `feat(peek):` or `docs(peek):`"
                % (sha[:8], subject, ", ".join(TYPES)))
    return problems


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--branch", help="branch name to check "
                                     "(default: the current branch)")
    ap.add_argument("--range", dest="rev_range",
                    help="commit range to check, e.g. origin/main..HEAD "
                         "(default: origin/main..HEAD)")
    ap.add_argument("--skip-branch", action="store_true",
                    help="check commits only")
    ap.add_argument("--skip-commits", action="store_true",
                    help="check the branch name only")
    ap.add_argument("--require-commits", action="store_true",
                    help="fail if the range selects NO commits. CI passes this: "
                         "an empty range there means the base or head ref was "
                         "wrong and the check silently examined nothing, which "
                         "is a pass that reports on less than it claims. Off by "
                         "default so `make conventions` on a fresh branch with "
                         "no commits yet is not an error.")
    args = ap.parse_args()

    problems = []

    if not args.skip_branch:
        branch = args.branch or git("rev-parse", "--abbrev-ref", "HEAD")
        problems += [("branch", p) for p in check_branch(branch)]

    if not args.skip_commits:
        rev_range = args.rev_range or "origin/main..HEAD"
        rows = commits_in(rev_range)
        # A range that selects NOTHING is a pass reporting on less than it
        # claims -- this repo's own recorded defect, and one wrong base ref
        # away here. In CI that is an error; locally it is a fresh branch.
        if not rows:
            if args.require_commits:
                problems.append(
                    ("commit",
                     "%s selects NO commits. Nothing was checked, so this would "
                     "have passed without examining anything -- refusing instead. "
                     "Check the base and head refs." % rev_range))
            else:
                print(f"note: {rev_range} selects no commits; nothing to check")
        problems += [("commit", p) for p in check_commits(rows)]
        print(f"checked {len(rows)} commit(s) in {rev_range}")

    if problems:
        print(f"\n{len(problems)} convention problem(s):\n", file=sys.stderr)
        for kind, p in problems:
            print(f"  [{kind}] {p}\n", file=sys.stderr)
        return 1
    print("conventions ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
