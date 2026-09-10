#!/usr/bin/env python3
"""Tests for release_notes.py, against real throwaway repositories.

    python3 tools/test_release_notes.py

DELIBERATELY NOT WIRED INTO `make test`, for tools/test_check_conventions.py's
and tools/test_compare_design.py's reason: the fast loop builds on a bare
checkout with no Python, and every generated asset is committed precisely so it
can. These are the tests for a tool, run when the tool changes.

WHY REAL REPOSITORIES AND NOT FIXTURES. Every defect this file guards against
lives in what `git describe` and `git log` ANSWER, not in the formatting -- the
first tag having no predecessor is a `git describe` failure exit, and the merge
exclusion is a `git log` flag. A fixture list of subjects would exercise
`render` and nothing that has ever been wrong. Same argument as
test_check_conventions.py driving the real pre-push hook: the defect was in the
range the hook computes, not in the regex.

WHY `gh` IS STUBBED. The X4 caveat is emitted only while issue #23 is open, and
that is the mechanism that keeps it from becoming a claim with an expiry date
and no owner. A test that asked the real tracker would change its verdict the
day somebody closes #23 -- which is the very failure the design avoids,
reintroduced in the thing checking it. So a fake `gh` goes on PATH and each of
the three answers is driven deliberately, the missing one included: `gh` absent
must INCLUDE the caveat, because failing toward the release that overclaims is
the direction that cannot be seen on a release page.
"""
import importlib.util
import os
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
SCRIPT = ROOT / "tools" / "release_notes.py"

FAILURES = []


def check(ok, label, detail=""):
    print(("  ok   " if ok else "  FAIL ") + label
          + (f"   [{detail}]" if detail and not ok else ""))
    if not ok:
        FAILURES.append(label)


def load():
    spec = importlib.util.spec_from_file_location("release_notes", SCRIPT)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def git(repo, *args):
    return subprocess.run(["git", "-C", str(repo), *args],
                          capture_output=True, text=True, check=True).stdout


def commit(repo, subject):
    git(repo, "commit", "-q", "--allow-empty", "-m", subject)


def new_repo(tmp):
    """An initialised repository with an identity and a `main` branch."""
    repo = tmp / "repo"
    repo.mkdir()
    git(repo, "init", "-q", "-b", "main")
    git(repo, "config", "user.email", "t@example.com")
    git(repo, "config", "user.name", "T")
    return repo


def tag(repo, name):
    """Annotated, which is what docs/releasing.md says a release is."""
    git(repo, "tag", "-a", name, "-m", name)


def gh_stub(tmp, state):
    """A directory holding a fake `gh` that answers with `state`.

    state=None means no `gh` at all -- the PATH is emptied of it, which is what
    a machine without the CLI, or a runner with no token, actually looks like.
    """
    bindir = tmp / "bin"
    bindir.mkdir(exist_ok=True)
    gh = bindir / "gh"
    if state is None:
        gh.write_text("#!/bin/sh\nexit 127\n")
    else:
        gh.write_text(f'#!/bin/sh\nprintf \'{{"state":"{state}"}}\'\n')
    gh.chmod(0o755)
    return bindir


def run(repo, *args, gh_state="OPEN", tmp=None):
    """(returncode, stdout, stderr) for a real invocation, cwd inside `repo`."""
    env = dict(os.environ)
    env["PATH"] = str(gh_stub(tmp, gh_state)) + os.pathsep + env["PATH"]
    p = subprocess.run([sys.executable, str(SCRIPT), *args],
                       cwd=str(repo), capture_output=True, text=True, env=env)
    return p.returncode, p.stdout, p.stderr


# --------------------------------------------------------------------------
# The range: which commits a release's notes are made of.
# --------------------------------------------------------------------------

def test_first_tag_is_the_whole_history():
    print("\nthe FIRST tag has no predecessor, and that is v0.1.0's case")
    with tempfile.TemporaryDirectory() as d:
        tmp = pathlib.Path(d)
        repo = new_repo(tmp)
        for s in ["feat(a): one", "fix(b): two", "docs(c): three"]:
            commit(repo, s)
        tag(repo, "v0.1.0")

        rc, out, err = run(repo, "--tag", "v0.1.0", tmp=tmp)
        check(rc == 0, "exits 0 with no earlier tag", f"rc={rc} {err}")
        for s in ["feat(a): one", "fix(b): two", "docs(c): three"]:
            check(f"- {s}" in out, f"lists {s!r}")
        check("first tagged release" in out,
              "says so rather than naming a predecessor it does not have")
        check("3 commit(s)." in out, "counts them", out)


def test_second_tag_is_the_range_since_the_first():
    print("\na later tag lists only what it ADDS")
    with tempfile.TemporaryDirectory() as d:
        tmp = pathlib.Path(d)
        repo = new_repo(tmp)
        commit(repo, "feat(a): before the first release")
        tag(repo, "v0.1.0")
        commit(repo, "fix(b): after it")
        tag(repo, "v0.2.0")

        rc, out, _ = run(repo, "--tag", "v0.2.0", tmp=tmp)
        check(rc == 0, "exits 0")
        check("- fix(b): after it" in out, "lists the new commit")
        check("feat(a): before the first release" not in out,
              "does NOT re-list the previous release's commits")
        check("v0.1.0" in out, "names the predecessor it measured from")


def test_merges_are_excluded():
    print("\n`git merge main` is git's subject, not a release note")
    with tempfile.TemporaryDirectory() as d:
        tmp = pathlib.Path(d)
        repo = new_repo(tmp)
        commit(repo, "feat(base): root")
        git(repo, "checkout", "-q", "-b", "side")
        commit(repo, "feat(side): the work")
        git(repo, "checkout", "-q", "main")
        commit(repo, "fix(main): moved on")
        # --no-ff so there really is a merge commit to exclude.
        git(repo, "merge", "-q", "--no-ff", "-m", "Merge branch 'side'", "side")
        tag(repo, "v0.1.0")

        rc, out, _ = run(repo, "--tag", "v0.1.0", tmp=tmp)
        check(rc == 0, "exits 0")
        check("- feat(side): the work" in out, "keeps the branch's own commit")
        check("Merge branch" not in out, "drops the merge commit's subject")


def test_unknown_tag_refuses_without_a_traceback():
    print("\nan unfetched or mistyped tag is the ordinary way this is called "
          "wrongly")
    with tempfile.TemporaryDirectory() as d:
        tmp = pathlib.Path(d)
        repo = new_repo(tmp)
        commit(repo, "feat(a): one")
        tag(repo, "v0.1.0")

        rc, out, err = run(repo, "--tag", "v9.9.9", tmp=tmp)
        check(rc == 1, "exits 1", f"rc={rc}")
        check("no such tag or commit" in err, "names the problem", err)
        check("Traceback" not in err,
              "and does NOT hand back a Python stack trace", err)
        check(out.strip() == "", "writes no body to stdout", out)


def test_a_merge_only_range_refuses():
    print("\na range whose every commit is a merge would print an empty list")
    with tempfile.TemporaryDirectory() as d:
        tmp = pathlib.Path(d)
        repo = new_repo(tmp)
        commit(repo, "feat(a): one")
        first = git(repo, "rev-parse", "HEAD").strip()
        commit(repo, "feat(b): two")
        tag(repo, "v0.1.0")

        # CONSTRUCTED, and it has to be: an ordinary release adds at least its
        # own commit, and this repository squash-merges, so no normal graph
        # reaches the guard. Without this the refusal would be a branch no test
        # enters, which is the shape CLAUDE.md records as untested slack under a
        # max() whose other arm cannot win.
        #
        # BOTH PARENTS ARE ALREADY REACHABLE FROM v0.1.0, which is what makes
        # the range hold the merge and nothing else. They must also be
        # DISTINCT: `commit-tree -p X -p X` collapses to a single parent, so
        # that spelling produces an ordinary commit, `--no-merges` keeps it, and
        # the case silently tests nothing -- which is how this was written the
        # first time.
        head = git(repo, "rev-parse", "HEAD").strip()
        tree = git(repo, "rev-parse", "HEAD^{tree}").strip()
        merge = subprocess.run(
            ["git", "-C", str(repo), "commit-tree", tree,
             "-p", head, "-p", first, "-m", "Merge branch 'side'"],
            capture_output=True, text=True, check=True).stdout.strip()
        git(repo, "tag", "-a", "v0.2.0", "-m", "v0.2.0", merge)
        # The fixture is only a fixture if it really is a merge.
        parents = git(repo, "log", "-1", "--format=%P", merge).split()
        check(len(parents) == 2, "the fixture really has two parents",
              str(parents))

        rc, out, err = run(repo, "--tag", "v0.2.0", tmp=tmp)
        check(rc == 1, "exits 1", f"rc={rc} {err}")
        check("selects no non-merge commits" in err, "says why", err)
        check(out.strip() == "", "writes no body to stdout", out)


# --------------------------------------------------------------------------
# The X4 caveat, which exists to retire itself.
# --------------------------------------------------------------------------

def test_caveat_follows_the_issue():
    print("\nthe X4 caveat is asked for, and fails toward INCLUDING it")
    with tempfile.TemporaryDirectory() as d:
        tmp = pathlib.Path(d)
        repo = new_repo(tmp)
        commit(repo, "feat(a): one")
        tag(repo, "v0.1.0")

        _, out, _ = run(repo, "--tag", "v0.1.0", gh_state="OPEN", tmp=tmp)
        check("Tested on an X3 only" in out, "issue OPEN -> the caveat is there")

        _, out, _ = run(repo, "--tag", "v0.1.0", gh_state="CLOSED", tmp=tmp)
        check("Tested on an X3 only" not in out,
              "issue CLOSED -> it retires itself, with no line to remember")

        # The direction that matters. A dropped caveat is invisible on a
        # release page; a redundant one is merely redundant.
        _, out, _ = run(repo, "--tag", "v0.1.0", gh_state=None, tmp=tmp)
        check("Tested on an X3 only" in out,
              "gh MISSING -> included, not dropped")

        _, out, _ = run(repo, "--tag", "v0.1.0", "--no-caveat-query",
                        gh_state="CLOSED", tmp=tmp)
        check("Tested on an X3 only" in out,
              "--no-caveat-query includes it without asking")


def test_caveat_names_the_issue_it_expires_with():
    print("\nthe caveat carries its own expiry pointer")
    mod = load()
    body = mod.render("v0.1.0", None, ["feat(a): one"],
                      caveat=mod.X4_CAVEAT.format(repo="o/r",
                                                  issue=mod.X4_ISSUE))
    check("o/r/issues/23" in body, "links the issue a reader can check", body)


def test_render_is_pure():
    print("\nrender() needs no repository, so the shape is testable directly")
    mod = load()
    body = mod.render("v0.2.0", "v0.1.0", ["fix(a): one", "feat(b): two"])
    check(body.startswith("Changes since "), "names the span first", body)
    check("https://github.com/Rukkaitto/encre/releases/tag/v0.1.0" in body,
          "links the predecessor ABSOLUTELY -- a relative href in a release "
          "body resolves against a path that is not the one it looks like",
          body)
    check("- fix(a): one\n- feat(b): two" in body,
          "one bullet per subject, in git log's order", body)
    check("2 commit(s)." in body, "counts them", body)


def main():
    test_first_tag_is_the_whole_history()
    test_second_tag_is_the_range_since_the_first()
    test_merges_are_excluded()
    test_unknown_tag_refuses_without_a_traceback()
    test_a_merge_only_range_refuses()
    test_caveat_follows_the_issue()
    test_caveat_names_the_issue_it_expires_with()
    test_render_is_pure()

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
