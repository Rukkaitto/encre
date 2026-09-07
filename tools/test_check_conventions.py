#!/usr/bin/env python3
"""Tests for check_conventions.py's TWO BLIND SPOTS, which is what issue #71 was.

    python3 tools/test_check_conventions.py

DELIBERATELY NOT WIRED INTO `make test`, for tools/test_compare_design.py's
reason: that target runs on a bare checkout with no Python and no submodule --
every generated asset is committed precisely so it can -- and putting an
interpreter on the fast loop would be a worse trade than these tests are worth.
They are the tests for a tool, run when the tool changes. Same for the shell
cases below, which run the real `.githooks/pre-push` against real throwaway
repositories.

WHAT THEY COVER, AND WHY IT IS THIS AND NOT THE REGEX. The subject regex is the
part of this tool that has never been wrong. What has been wrong is WHICH
subjects reach it:

  - the PR TITLE never did, and a squash-merge writes it as the merge commit's
    subject, so five non-conforming subjects are on `main`;
  - pre-push's range was "since the remote tip of THIS branch", which after
    `git merge origin/main` includes commits the branch did not write -- so one
    of those five rejected the push of every branch that merged main;
  - and the two arms of that range DISAGREED, so a never-pushed branch passed
    and the same branch after one push failed on the same tree.

So the assertions are about the SET of subjects examined and the verdict, and
there are two properties every case here exists to hold apart: a check must not
pass by examining nothing (an empty --subject, an empty range), and a fix that
lets a merged commit through must still reject a bad commit the branch wrote.
"""
import importlib.util
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
CHECKER = ROOT / "tools" / "check_conventions.py"
PRE_PUSH = ROOT / ".githooks" / "pre-push"

FAILURES = []


def check(ok, label, detail=""):
    print(("  ok   " if ok else "  FAIL ") + label + (f"   [{detail}]" if detail and not ok else ""))
    if not ok:
        FAILURES.append(label)


def load():
    """Import check_conventions.py for the unit-level cases."""
    spec = importlib.util.spec_from_file_location(
        "check_conventions", CHECKER)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def run_checker(*args):
    """(returncode, stdout+stderr) for a real invocation of the checker."""
    p = subprocess.run([sys.executable, str(CHECKER), *args],
                       capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


# --------------------------------------------------------------------------
# --subject: the PR title, which is the subject squash-merge actually writes.
# --------------------------------------------------------------------------

def test_subject_mode():
    print("\n--subject checks the PR title, which is what squash-merge commits")

    # The five real subjects on main that got there as PR titles. Every one of
    # them must be rejected by the mode that now sees them, or this fix is
    # decorative.
    real = [
        "Hold power to wake, and a gate that makes the badge true (#62)",
        "Home reports the real battery charge, and a charging mark (#51)",
        "The sleep card's author wraps to two lines instead of escaping the card (#86)",
        "Six TODO tickets: the card log, the Names row, sleep titles, Contents headers, and --only (#84)",
        "Move both corrupt-book copy shapes off the wrap boundary (#79)",
    ]
    for title in real:
        code, out = run_checker("--subject", title)
        check(code == 1 and "the pull request title" in out,
              f"rejects a real main subject: {title[:44]!r}", f"exit={code}")

    # And it must accept the form the rule asks for, including the shapes the
    # regex allows: no scope, a scope, and a breaking `!`.
    for title in ("ci(conventions): gate the PR title as a commit subject",
                  "fix: one non-conforming subject no longer rejects a push",
                  "feat(reader)!: the anchor is a high-water mark"):
        code, out = run_checker("--subject", title)
        check(code == 0 and "conventions ok" in out,
              f"accepts {title[:40]!r}", f"exit={code} out={out!r}")

    # A fixup is legitimate at commit time and never in a PR title: there is no
    # later squash to absorb it, the squash IS the merge.
    code, out = run_checker("--subject", "fixup! feat(x): y")
    check(code == 1 and "fixup" in out, "rejects a fixup! PR title", f"exit={code}")

    # An empty value is the reports-on-nothing shape --require-commits exists
    # for. A workflow wired to the wrong payload field yields exactly this.
    for empty, label in ((["--subject", ""], "empty"),
                         (["--subject", "   "], "whitespace-only")):
        code, out = run_checker(*empty)
        check(code == 1 and "without examining anything" in out,
              f"refuses an {label} --subject rather than passing", f"exit={code}")

    # `#` is a git comment in a message FILE and an ordinary character in a PR
    # title. This is the reason --subject is its own mode: routing a title
    # through --message-file would read this as an empty message and exit 0.
    code, out = run_checker("--subject", "#71 gate the PR title")
    check(code == 1, "a title starting with `#` is checked, not skipped as a comment",
          f"exit={code} out={out!r}")

    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
        f.write("#71 gate the PR title\n")
        path = f.name
    try:
        code, out = run_checker("--message-file", path)
        check(code == 0 and "empty commit message" in out,
              "...and --message-file really would have skipped it (why the mode exists)",
              f"exit={code} out={out!r}")
    finally:
        os.unlink(path)

    # Two spellings of one mode is an error, not a silent precedence rule.
    code, out = run_checker("--subject", "feat: x", "--message-file", "/dev/null")
    check(code != 0 and "one-subject mode" in out,
          "--subject and --message-file together is an error", f"exit={code}")

    # --message-file's fixup exemption must not have leaked into --subject via
    # the shared reporter.
    code, out = run_checker("--subject", "fixup! feat(x): y", "--allow-fixup")
    check(code == 1, "--allow-fixup does not excuse a fixup PR title", f"exit={code}")


def test_shared_reporter_did_not_change_commit_msg_mode():
    print("\nthe commit-msg hook's behaviour is unchanged by the shared reporter")
    cases = [("feat(peek): a real subject", 0, ""),
             ("peek: the bare-subsystem style", 1, "SCOPE"),
             ("fixup! feat(peek): squash me", 0, "")]
    for text, want, needle in cases:
        with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
            f.write(text + "\n\nbody\n")
            path = f.name
        try:
            code, out = run_checker("--message-file", path, "--allow-fixup")
            check(code == want and needle in out,
                  f"--message-file --allow-fixup on {text[:34]!r} -> {want}",
                  f"exit={code} out={out!r}")
        finally:
            os.unlink(path)

    # Without --allow-fixup the same message is rejected: push and CI are the
    # moments a fixup must not survive.
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
        f.write("fixup! feat(peek): squash me\n")
        path = f.name
    try:
        code, _ = run_checker("--message-file", path)
        check(code == 1, "a fixup is still rejected without --allow-fixup", f"exit={code}")
    finally:
        os.unlink(path)


def test_range_mode_still_reports_on_what_it_claims():
    print("\n--require-commits still refuses an empty range")
    mod = load()
    check(mod.commits_in("") == [], "an empty range string selects nothing")
    code, out = run_checker("--skip-branch", "--range", "HEAD..HEAD",
                            "--require-commits")
    check(code == 1 and "selects NO commits" in out,
          "an empty range with --require-commits is an error", f"exit={code}")
    code, out = run_checker("--skip-branch", "--range", "HEAD..HEAD")
    check(code == 0 and "nothing to check" in out,
          "...and only a note without it", f"exit={code} out={out!r}")


# --------------------------------------------------------------------------
# pre-push's range. These drive the REAL hook against real repositories,
# because the defect was in the range it computes and not in the checker.
# --------------------------------------------------------------------------

BAD_SUBJECT = "Hold power to wake, and a gate that makes the badge true (#62)"


def g(repo, *args, **kw):
    return subprocess.run(["git", "-C", str(repo), *args], capture_output=True,
                          text=True, check=kw.get("check", True))


def build_world(tmp):
    """A remote with a `main` carrying one non-conforming subject, and a clone.

    The subject is the real one from `main` -- the point is a commit nobody on
    the branch under test wrote, sitting on the upstream branch they merge.
    """
    remote = tmp / "remote.git"
    origin = tmp / "origin"
    work = tmp / "work"
    g(tmp, "init", "--quiet", "--bare", "--initial-branch=main", str(remote))
    g(tmp, "init", "--quiet", "--initial-branch=main", str(origin))
    g(origin, "config", "user.email", "t@t")
    g(origin, "config", "user.name", "t")
    (origin / "a.txt").write_text("base\n")
    g(origin, "add", "a.txt")
    g(origin, "commit", "--quiet", "-m", "feat(base): the shared root")
    g(origin, "remote", "add", "origin", str(remote))
    g(origin, "push", "--quiet", "-u", "origin", "main")
    base = g(origin, "rev-parse", "HEAD").stdout.strip()

    # main moves on, with the kind of subject a squash-merge writes.
    (origin / "b.txt").write_text("main moved\n")
    g(origin, "add", "b.txt")
    g(origin, "commit", "--quiet", "-m", BAD_SUBJECT)
    g(origin, "push", "--quiet", "origin", "main")

    g(tmp, "clone", "--quiet", str(remote), str(work))
    g(work, "config", "user.email", "t@t")
    g(work, "config", "user.name", "t")
    g(work, "config", "core.hooksPath", str(tmp / "hooks"))
    return remote, work, base


def install_hook(tmp, source):
    hooks = tmp / "hooks"
    hooks.mkdir(exist_ok=True)
    dst = hooks / "pre-push"
    shutil.copy(source, dst)
    dst.chmod(0o755)
    # The hook resolves the checker from the repo it runs in, which is a
    # throwaway. Point it at this checkout's copy instead.
    text = dst.read_text()
    text = text.replace('python3 "$root/tools/check_conventions.py"',
                        'python3 "%s"' % CHECKER)
    dst.write_text(text)


FIXED_RANGE = '        range="$local_sha --not --remotes=$remote $remote_sha"'


def mutate_pre_push(tmp, replacement, name):
    """The shipped hook with its negotiated-arm range replaced.

    The assert is not decoration: this project has had a mutation land on a line
    the code no longer contained and read the resulting green as evidence. If
    the range line is reworded, every BEFORE case here must fail loudly rather
    than quietly measure the shipped hook twice.
    """
    text = PRE_PUSH.read_text()
    assert text.count(FIXED_RANGE) == 1, \
        "the fixed range line moved; every mutation below is stale"
    path = tmp / f"pre-push.{name}"
    path.write_text(text.replace(FIXED_RANGE, replacement))
    return path


def old_pre_push(tmp):
    """The pre-#71 range: since the negotiated tip of this branch, and no more."""
    return mutate_pre_push(tmp, '        range="$remote_sha..$local_sha"', "old")


def blind_pre_push(tmp):
    """A range that selects NOTHING -- the shape a careless fix would have.

    "Let the merged commits through" and "let everything through" both make the
    reported symptom go away, and only one of them is a fix.
    """
    return mutate_pre_push(tmp, '        range="$local_sha --not $local_sha"',
                           "blind")


def push(work, extra=None):
    p = subprocess.run(["git", "-C", str(work), "push", *(extra or []),
                        "origin", "claude/under-test"],
                       capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


def scenario(tmp, hook, own_subjects, merge_main):
    """Push once, then optionally merge main, add commits, and push again."""
    remote, work, base = build_world(tmp)
    install_hook(tmp, hook)
    g(work, "checkout", "--quiet", "-b", "claude/under-test", base)
    (work / "own1.txt").write_text("x\n")
    g(work, "add", "own1.txt")
    g(work, "commit", "--quiet", "--no-verify", "-m", "feat(own): the first commit")
    first = push(work, ["-u"])
    if merge_main:
        g(work, "merge", "--quiet", "--no-edit", "origin/main")
    for i, subject in enumerate(own_subjects):
        (work / f"own{i + 2}.txt").write_text("y\n")
        g(work, "add", f"own{i + 2}.txt")
        g(work, "commit", "--quiet", "--no-verify", "-m", subject)
    second = push(work)
    return first, second


def test_pre_push_range():
    print("\npre-push: a merged commit nobody on the branch wrote")

    with tempfile.TemporaryDirectory() as d:
        tmp = pathlib.Path(d)
        first, second = scenario(tmp, old_pre_push(tmp), [], merge_main=True)
        check(first[0] == 0, "BEFORE: the first push passes", f"exit={first[0]}")
        check(second[0] != 0 and BAD_SUBJECT in second[1],
              "BEFORE: merging main then pushing is REJECTED (issue #71)",
              f"exit={second[0]}")
        globals()["_before_output"] = second[1]

    with tempfile.TemporaryDirectory() as d:
        tmp = pathlib.Path(d)
        first, second = scenario(tmp, PRE_PUSH, [], merge_main=True)
        check(first[0] == 0, "AFTER: the first push passes", f"exit={first[0]}")
        check(second[0] == 0, "AFTER: merging main then pushing is ACCEPTED",
              f"exit={second[0]} out={second[1]!r}")

    print("\npre-push: and a bad subject the branch itself wrote is still rejected")

    # The whole hazard of the fix: it must not let everything through.
    with tempfile.TemporaryDirectory() as d:
        tmp = pathlib.Path(d)
        _, second = scenario(tmp, PRE_PUSH, ["A subject in the old bare style"],
                             merge_main=True)
        check(second[0] != 0 and "A subject in the old bare style" in second[1],
              "AFTER: bad own commit + merged main -> rejected, naming only the own one",
              f"exit={second[0]} out={second[1]!r}")
        check(BAD_SUBJECT not in second[1],
              "...and the merged commit is not among the complaints")

    with tempfile.TemporaryDirectory() as d:
        tmp = pathlib.Path(d)
        _, second = scenario(tmp, PRE_PUSH, ["Another bare subject"],
                             merge_main=False)
        check(second[0] != 0 and "Another bare subject" in second[1],
              "AFTER: bad own commit with no merge -> still rejected",
              f"exit={second[0]}")

    # And the two cases above are the ONLY thing standing between this fix and
    # a hook that passes everything, so mutate to that and watch them fail.
    # A fix that lets everything through is worse than the bug it closes.
    with tempfile.TemporaryDirectory() as d:
        tmp = pathlib.Path(d)
        _, second = scenario(tmp, blind_pre_push(tmp),
                             ["A subject in the old bare style"], merge_main=True)
        check(second[0] == 0,
              "MUTANT: a range selecting nothing really does accept a bad "
              "commit (so the two cases above are what catch it)",
              f"exit={second[0]} out={second[1]!r}")

    print("\npre-push: the two arms of the range now agree on ONE graph")

    # THE SAME final commit graph, reached two ways, so the only difference is
    # which arm of the range runs:
    #
    #   A  branch, own commit, merge main, push        -> remote_sha is zeros,
    #                                                     the --remotes arm
    #   B  branch, own commit, push, merge main, push  -> remote_sha is the
    #                                                     branch's tip, the
    #                                                     negotiated arm
    #
    # BEFORE, A passed and B failed. The graph being pushed is identical, so
    # that disagreement is what said the negotiated arm was measuring the wrong
    # set rather than that the branch had a problem.
    def only_arm_a(tmp, hook):
        remote, work, base = build_world(tmp)
        install_hook(tmp, hook)
        g(work, "checkout", "--quiet", "-b", "claude/under-test", base)
        (work / "own1.txt").write_text("x\n")
        g(work, "add", "own1.txt")
        g(work, "commit", "--quiet", "--no-verify", "-m", "feat(own): the first commit")
        g(work, "merge", "--quiet", "--no-edit", "origin/main")
        return push(work, ["-u"])

    for name, hook_of in (("BEFORE", old_pre_push), ("AFTER", lambda _t: PRE_PUSH)):
        with tempfile.TemporaryDirectory() as d:
            tmp = pathlib.Path(d)
            a = only_arm_a(tmp, hook_of(tmp))
        with tempfile.TemporaryDirectory() as d:
            tmp = pathlib.Path(d)
            _, b = scenario(tmp, hook_of(tmp), [], merge_main=True)
        # Same graph pushed; the trees are byte-identical by construction.
        agree = (a[0] == 0) == (b[0] == 0)
        if name == "BEFORE":
            check(a[0] == 0 and b[0] != 0 and not agree,
                  "BEFORE: arm A accepts and arm B rejects the SAME graph",
                  f"A={a[0]} B={b[0]}")
        else:
            check(a[0] == 0 and b[0] == 0 and agree,
                  "AFTER: both arms accept the same graph", f"A={a[0]} B={b[0]}")


def main():
    test_subject_mode()
    test_shared_reporter_did_not_change_commit_msg_mode()
    test_range_mode_still_reports_on_what_it_claims()
    test_pre_push_range()

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
