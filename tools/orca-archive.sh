#!/bin/sh
# Run by Orca from `orca.yaml`'s scripts.archive, just before a worktree is
# archived or removed. Runnable by hand to see what a removal would cost:
#
#   tools/orca-archive.sh
#
# WHAT A REMOVAL ACTUALLY DESTROYS, which is narrower than it feels and is the
# whole reason this is worth a script: removing a worktree does NOT delete its
# branch, so every COMMITTED change survives in the common repository whether or
# not it was ever pushed -- it is reachable by branch name for as long as the
# branch exists. What goes with the directory is the WORKING TREE: modifications
# that were never committed, and new files that were never added. Those have no
# other copy anywhere.
#
# So this does not warn and hope. It writes what cannot be recovered to
# `<common .git>/orca-archive/`, which is inside .git -- never shown by `git
# status`, never a stray untracked file in somebody's checkout, and it travels
# with the repository rather than with the worktree being deleted.
#
# IT ALWAYS EXITS 0, DELIBERATELY. Whether Orca refuses an archive on a non-zero
# hook is not documented anywhere this repo can check, and a guess that turns out
# to block is a worktree the user cannot delete. Saving the work and getting out
# of the way is the answer that is right under either semantics.
set -u

branch=$(git branch --show-current 2>/dev/null || echo '(detached HEAD)')
name=${ORCA_WORKSPACE_NAME:-$branch}
# Run by hand the workspace name IS the branch, and printing it twice reads like
# a bug in the script rather than a fact about the worktree.
if [ "$name" = "$branch" ]; then
  printf 'orca-archive: %s\n' "$branch"
else
  printf 'orca-archive: %s on %s\n' "$name" "$branch"
fi

common=$(git rev-parse --path-format=absolute --git-common-dir 2>/dev/null)
if [ -z "$common" ]; then
  printf '  not a git worktree; nothing to save\n'
  exit 0
fi
out="$common/orca-archive"
stamp=$(date +%Y%m%d-%H%M%S)
slug=$(printf '%s' "$name" | tr -c 'A-Za-z0-9._-' '-')
saved=0

# Tracked modifications. --binary so a changed PNG -- a re-blessed golden is the
# likeliest binary in this repo -- survives the round trip rather than coming
# back as "Binary files differ".
if ! git diff --quiet HEAD 2>/dev/null; then
  mkdir -p "$out" 2>/dev/null
  if git diff --binary HEAD > "$out/$slug-$stamp.patch" 2>/dev/null; then
    printf '  saved      %s.patch  (git apply it in a fresh checkout)\n' "$slug-$stamp"
    saved=1
  else
    printf '  COULD NOT SAVE uncommitted changes to %s\n' "$out" >&2
  fi
fi

# New files, which no patch of tracked content can hold. --exclude-standard is
# what keeps build/, graft/ and .pio/ out of it, so this is the work and not the
# artefacts.
if [ -n "$(git ls-files --others --exclude-standard 2>/dev/null | head -1)" ]; then
  mkdir -p "$out" 2>/dev/null
  if git ls-files --others --exclude-standard -z 2>/dev/null \
     | tar -czf "$out/$slug-$stamp-untracked.tgz" --null -T - 2>/dev/null; then
    printf '  saved      %s-untracked.tgz\n' "$slug-$stamp"
    saved=1
  else
    printf '  COULD NOT SAVE untracked files to %s\n' "$out" >&2
  fi
fi

# Commits. These are NOT lost by a removal, so this names where they live rather
# than rescuing them. Against the upstream when there is one; a branch that was
# never pushed has none, and then every commit on it is simply unpublished.
ahead=$(git rev-list --count '@{upstream}..HEAD' 2>/dev/null)
if [ -n "$ahead" ] && [ "$ahead" != "0" ]; then
  printf '  unpushed   %s commit(s); they stay on branch %s\n' "$ahead" "$branch"
elif [ -z "$ahead" ] && [ "$branch" != "(detached HEAD)" ]; then
  printf '  unpushed   branch %s has no upstream; its commits stay on it\n' "$branch"
fi

if [ "$saved" -eq 1 ]; then
  printf 'orca-archive: working tree saved under %s\n' "$out"
else
  printf 'orca-archive: clean working tree; nothing would be lost\n'
fi
exit 0
