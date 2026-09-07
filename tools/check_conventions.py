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

  A PR TITLE is checked too, by `--subject`, and it is not a nicety: GitHub's
  squash-merge writes the PR title as the merge commit's SUBJECT, so the title
  is where a subject landing on `main` is actually authored. Checking commits
  and not the title is a gate whose input arrives by a path it does not cover --
  five subjects on `main` got there that way (issue #71). Its own mode rather
  than --message-file because a title is a string and not a file, and because
  --message-file SKIPS lines beginning with `#` as git comments: a PR titled
  `#71 ...` would read as an empty message and pass without being examined.
"""
import argparse
import pathlib
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
    # split() so a range can be several tokens: the pre-push hook passes
    # "<sha> --not --remotes=origin", which is "the commits this push actually
    # adds" and is immune to a stale origin/main -- where a plain
    # origin/main..HEAD would drag in already-merged history and fail on it.
    out = git("log", "--format=%H\x1f%s\x1f%P", *rev_range.split())
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


def check_subject(subject, label, allow_fixup=False):
    """[] if this one subject conforms, else a list of complaints.

    allow_fixup is the commit-msg hook's: `git commit --fixup` writes a
    `fixup!` subject and that is a LEGITIMATE local state -- rejecting it there
    would break the workflow whose whole point is to be squashed later. It stays
    rejected on push and in CI, which are the moments it must not survive.
    """
    if subject.startswith(("fixup!", "squash!", "amend!")):
        if allow_fixup:
            return []
        return ["%s %r is a fixup and must be squashed before merging"
                % (label, subject)]
    if SUBJECT_RE.match(subject):
        return []
    return ["%s %r\n"
            "    expected <type>(<optional scope>): <subject>\n"
            "    types: %s\n"
            "    the house style writes the subsystem as the type; it is a\n"
            "    SCOPE -- `peek:` becomes `feat(peek):` or `docs(peek):`"
            % (label, subject, ", ".join(TYPES))]


def check_commits(rows, allow_fixup=False):
    """[] if every non-merge subject conforms, else a list of complaints."""
    problems = []
    for sha, subject, parent_count in rows:
        if parent_count > 1:
            continue  # a merge commit's subject is git's, not the author's
        problems += check_subject(subject, sha[:8], allow_fixup)
    return problems


def subject_of_message_file(path):
    """The subject line of a commit message file, or None if there is none.

    Comment lines are git's (`#`), and `git commit -v` appends a whole diff
    below a scissors line -- so the subject is the first line that is neither
    blank nor a comment, not simply line one.
    """
    text = pathlib.Path(path).read_text(encoding="utf-8", errors="replace")
    for line in text.splitlines():
        if line.startswith("#"):
            continue
        if not line.strip():
            continue
        return line.rstrip()
    return None


def report_one_subject(subject, label, fix_hint, allow_fixup=False):
    """Check one subject and print the verdict. 0 if it conforms, else 1.

    Shared by --message-file and --subject so the two cannot drift into two
    spellings of the same verdict.
    """
    problems = check_subject(subject, label, allow_fixup=allow_fixup)
    if problems:
        print(f"\n{len(problems)} convention problem(s):\n", file=sys.stderr)
        for msg in problems:
            print(f"  [commit] {msg}\n", file=sys.stderr)
        print(f"  {fix_hint}", file=sys.stderr)
        return 1
    print("conventions ok")
    return 0


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
    ap.add_argument("--message-file",
                    help="validate the subject in this commit message file and "
                         "nothing else -- the commit-msg hook's argument.")
    ap.add_argument("--subject",
                    help="validate this ONE subject string and nothing else. "
                         "For the PR-title job: squash-merge writes the PR "
                         "title as the merge commit's subject, so the title is "
                         "the last place that subject can still be edited. An "
                         "empty or whitespace-only value is an ERROR, not a "
                         "pass -- it means the caller was wired to the wrong "
                         "field, and a check that examines nothing must not "
                         "report success.")
    ap.add_argument("--allow-fixup", action="store_true",
                    help="accept fixup!/squash! subjects. For the commit-msg "
                         "hook only: `git commit --fixup` is a legitimate local "
                         "state, and stays rejected on push and in CI.")
    ap.add_argument("--require-commits", action="store_true",
                    help="fail if the range selects NO commits. CI passes this: "
                         "an empty range there means the base or head ref was "
                         "wrong and the check silently examined nothing, which "
                         "is a pass that reports on less than it claims. Off by "
                         "default so `make conventions` on a fresh branch with "
                         "no commits yet is not an error.")
    args = ap.parse_args()

    problems = []

    if args.message_file and args.subject is not None:
        ap.error("--message-file and --subject are two spellings of the same "
                 "one-subject mode; pass one")

    # --subject is its own mode: one string, no branch, no range, no git.
    if args.subject is not None:
        if not args.subject.strip():
            print("\n1 convention problem(s):\n", file=sys.stderr)
            print("  [commit] --subject is empty. Nothing was checked, so this "
                  "would have passed\n    without examining anything -- "
                  "refusing instead. Check the field it was\n    read from.\n",
                  file=sys.stderr)
            return 1
        return report_one_subject(
            args.subject, "the pull request title",
            "a squash-merge writes this as the commit subject; edit the PR "
            "title.")

    # --message-file is its own mode: one pending subject, no branch, no range.
    if args.message_file:
        subject = subject_of_message_file(args.message_file)
        if subject is None:
            print("note: empty commit message; git will abort on its own")
            return 0
        return report_one_subject(
            subject, "the message you just wrote",
            "fix it with:  git commit --amend",
            allow_fixup=args.allow_fixup)

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
