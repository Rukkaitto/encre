#!/bin/sh
# Bring a fresh Orca worktree to the point where `make test` runs and an agent
# landing in it is not lied to. Orca runs this from `orca.yaml`'s scripts.setup,
# in the new worktree's own directory; it is equally runnable by hand:
#
#   tools/orca-setup.sh
#
# WHAT A FRESH LINKED WORKTREE IS ACTUALLY MISSING, all four measured on one
# (2026-09-18): `freeink-sdk/` is EMPTY, there is no `build/`, no `graft/`, and
# no `.claude/settings.local.json`. None of that announces itself. `make test`
# passes on a bare tree with no submodule, so a worktree looks perfectly healthy
# and still cannot build firmware; and the graft SessionStart hook tells every
# agent "this repo is indexed by graft" while the index does not exist, which is
# the reports-on-more-than-it-has shape CLAUDE.md keeps paying for.
#
# NOT `set -e`, which is the one deliberate departure from tools/cardsync.sh: a
# setup that stops at the first failure reports on less than it claims -- the
# card probe answered from cache, the `make compare` default that skipped four
# screens. Every step runs, every step says ok/skip/FAIL, and the summary names
# what is broken. Only a step that blocks the fast loop fails the script.
#
# Knobs, all defaulting to the cheap answer so a worktree create stays quick:
#   ENCRE_BUILD_TYPE=Release   configure build/ optimised (see the caveat below)
#   ENCRE_SETUP_BUILD=1        compile after configuring, not just configure
#   ENCRE_SETUP_GRAFT=0        skip the graft index
#   ENCRE_SETUP_SUBMODULE=0    skip the submodule (desktop-only work)
set -u

blocked=0        # a step that stops `make test` -- this decides the exit code
firmware=1       # whether `make firmware` can work when this finishes
say() { printf '  %-13s %s\n' "$1" "$2"; }
indent() { sed 's/^/                /' >&2; }

printf 'orca-setup: %s\n' "${ORCA_WORKSPACE_NAME:-$(git branch --show-current 2>/dev/null || echo '?')}"

# 1. freeink-sdk/ -- THE documented trap. Without it `make firmware` fails with
# `PackageException: not a directory`, which names neither the submodule nor the
# fix. The status prefix is git's own: ' ' is at the pinned commit, '-' is not
# initialised, '+' is checked out at some other commit.
if [ "${ENCRE_SETUP_SUBMODULE:-1}" = "0" ]; then
  say "freeink-sdk" "skip     ENCRE_SETUP_SUBMODULE=0; make firmware will not build"
  firmware=0
elif [ "$(git submodule status freeink-sdk 2>/dev/null | cut -c1)" = " " ]; then
  say "freeink-sdk" "ok       already at the pinned commit"
elif out=$(git submodule update --init freeink-sdk 2>&1); then
  say "freeink-sdk" "ok       initialised"
else
  say "freeink-sdk" "FAIL     make firmware will fail with PackageException: not a directory"
  printf '%s\n' "$out" | indent
  firmware=0
fi

# 2. Hooks. core.hooksPath lives in the COMMON .git/config, so one `make hooks`
# in any worktree covers them all -- this only fills it in when nothing has. It
# deliberately does NOT rewrite an existing value: that config is shared with the
# main checkout, and a setup script quietly repointing the user's hooks is a side
# effect on a file this worktree does not own.
hooks=$(git config --get core.hooksPath 2>/dev/null)
if [ -n "$hooks" ]; then
  say "git hooks" "ok       core.hooksPath=$hooks"
elif git config core.hooksPath .githooks 2>/dev/null; then
  say "git hooks" "ok       installed commit-msg + pre-push"
else
  say "git hooks" "FAIL     run make hooks by hand, or a bad subject reaches the push"
fi

# 3. Per-machine config the repo ignores, and which a worktree therefore never
# gets. This is what Orca exposes $ORCA_ROOT_PATH for. Copied only when absent,
# never over the top of one: these are the user's files, not ours. The permission
# allowlist is the one that earns this step -- an agent spawned in a worktree
# without it re-asks for everything the user has already allowed.
root=${ORCA_ROOT_PATH:-}
if [ -z "$root" ]; then
  common=$(git rev-parse --path-format=absolute --git-common-dir 2>/dev/null)
  [ -n "$common" ] && root=$(dirname "$common")
fi
carried=""
if [ -n "$root" ] && [ "$root" != "$(pwd)" ]; then
  for f in .claude/settings.local.json platformio.local.ini; do
    if [ -f "$root/$f" ] && [ ! -e "$f" ]; then
      mkdir -p "$(dirname "$f")" 2>/dev/null
      cp "$root/$f" "$f" 2>/dev/null && carried="$carried $f"
    fi
  done
fi
if [ -n "$carried" ]; then
  say "local config" "ok       carried$carried"
else
  say "local config" "skip     nothing to carry from ${root:-<no root>}"
fi

# 4. build/. CMake globs its sources, so this is also the thing to re-run after
# adding or removing a file. A default configure sets no CMAKE_BUILD_TYPE and so
# builds core/ at -O0: right for `make test`, which is what CI runs, and NOT a
# tree to quote a `--bench` figure off -- every microsecond in CLAUDE.md is
# -O3/-Os and a default tree is ~5x slower. Hence the caveat on the line itself.
if ! command -v cmake >/dev/null 2>&1; then
  say "build/" "FAIL     cmake is not on PATH; make test cannot run"
  blocked=1
else
  if [ -n "${ENCRE_BUILD_TYPE:-}" ]; then
    out=$(cmake -S . -B build -DCMAKE_BUILD_TYPE="$ENCRE_BUILD_TYPE" 2>&1)
    rc=$?
    note="configured CMAKE_BUILD_TYPE=$ENCRE_BUILD_TYPE"
  else
    out=$(cmake -S . -B build 2>&1)
    rc=$?
    note="configured (no CMAKE_BUILD_TYPE: -O0, not a tree to bench on)"
  fi
  if [ $rc -ne 0 ]; then
    say "build/" "FAIL     cmake configure failed"
    printf '%s\n' "$out" | indent
    blocked=1
  elif [ "${ENCRE_SETUP_BUILD:-0}" = "1" ]; then
    if out=$(cmake --build build -j 2>&1); then
      say "build/" "ok       $note, compiled"
    else
      say "build/" "FAIL     $note, but the build failed"
      printf '%s\n' "$out" | indent
      blocked=1
    fi
  else
    say "build/" "ok       $note; the first make test compiles it"
  fi
fi

# 5. graft/. Gitignored and regenerable, so a worktree starts with none -- while
# the SessionStart hook and CLAUDE.md both tell an agent to reach for graft
# first. No `--deep`: that is the LLM pass and wants an API key, where the plain
# build is free and keyless.
if [ "${ENCRE_SETUP_GRAFT:-1}" = "0" ]; then
  say "graft/" "skip     ENCRE_SETUP_GRAFT=0; graft answers off a stale or absent index"
elif ! command -v graft >/dev/null 2>&1; then
  say "graft/" "skip     graft is not on PATH"
elif out=$(graft build 2>&1); then
  say "graft/" "ok       indexed"
else
  say "graft/" "FAIL     graft build failed; grep until it is rebuilt"
  printf '%s\n' "$out" | indent
fi

if [ "$blocked" -ne 0 ]; then
  printf 'orca-setup: NOT ready -- make test cannot run here yet (see FAIL above)\n' >&2
  exit 1
fi
if [ "$firmware" -eq 0 ]; then
  printf 'orca-setup: ready for desktop work; make firmware will NOT build (freeink-sdk)\n'
  exit 0
fi
printf 'orca-setup: ready\n'
