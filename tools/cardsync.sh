#!/bin/sh
# Copy generated .epub fixtures onto the device's SD card, and take them off again.
#
# The card is found by the firmware's OWN marker -- a `.reader/settings.json`
# that shell/src/main.cpp creates at boot -- rather than by volume name or size.
# A name is whatever the card was formatted as, and picking "the removable one"
# would happily write to somebody's backup drive. This fingerprint is specific to
# a card this firmware has booted from, and if it matches zero or several the
# script refuses instead of choosing.
#
# Usage:
#   tools/cardsync.sh add build/epubs-bulk     # copy every .epub in DIR to /books
#   tools/cardsync.sh remove build/epubs-bulk  # remove exactly those names again
#
# `remove` deletes ONLY files whose names appear in DIR, so it cannot take a real
# book with it -- the 200 fixtures come off and anything else on the card stays.
set -eu

action=${1:-}
dir=${2:-}
if [ -z "$action" ] || [ -z "$dir" ]; then
  echo "usage: $0 {add|remove} DIR" >&2
  exit 2
fi
[ -d "$dir" ] || { echo "no such directory: $dir" >&2; exit 2; }

card=""
found=0
for v in /Volumes/*/; do
  if [ -f "$v.reader/settings.json" ]; then
    card="$v"
    found=$((found + 1))
  fi
done

if [ "$found" -eq 0 ]; then
  echo "No Encre card found." >&2
  echo "Looked for a volume containing .reader/settings.json in:" >&2
  ls -1d /Volumes/*/ 2>/dev/null | sed 's/^/  /' >&2
  echo "Put the card in a reader (it lives in the device, so it has to come out)." >&2
  echo "If it is mounted but has never booted this firmware, there is no marker" >&2
  echo "to find -- boot it once, or copy by hand." >&2
  exit 1
fi
if [ "$found" -gt 1 ]; then
  echo "More than one volume looks like an Encre card; refusing to guess." >&2
  exit 1
fi

books="$card"books
count=$(ls -1 "$dir" | grep -c '\.epub$' || true)
[ "$count" -gt 0 ] || { echo "no .epub files in $dir" >&2; exit 2; }

case "$action" in
  add)
    mkdir -p "$books"
    echo "card:  $card"
    echo "adding $count .epub from $dir -> $books/"
    for f in "$dir"/*.epub; do cp "$f" "$books/"; done
    # AND STRIP THE APPLEDOUBLE SIDECARS, which is not tidiness -- it is half the
    # firmware's library read.
    #
    # Copying to a FAT volume from macOS writes a `._name` file beside every file,
    # to carry metadata FAT cannot hold. The firmware filters them out of the
    # library (they are hidden by convention), but it still PAYS for them: a
    # directory listing costs ~2.7 ms per entry on this card, and 203 books
    # measured 406 entries. So half of every library read -- at boot, and again
    # each time the Library screen opens -- is spent walking files that can never
    # be shown.
    #
    # dot_clean merges them back and deletes them. They return on the next copy
    # from a Mac, which is exactly why this runs here rather than being a thing to
    # remember.
    if command -v dot_clean >/dev/null 2>&1; then
      before=$(find "$books" -name '._*' | wc -l | tr -d ' ')
      dot_clean -m "$books" 2>/dev/null || dot_clean "$books" 2>/dev/null || true
      after=$(find "$books" -name '._*' | wc -l | tr -d ' ')
      echo "stripped $((before - after)) AppleDouble sidecar(s); $after left"
      echo "  (each one costs the firmware ~2.7 ms on every library read)"
    else
      echo "NOTE: dot_clean not found. The ._ sidecars macOS wrote will each cost"
      echo "      the firmware ~2.7 ms on every library read; remove them with"
      echo "      find \"$books\" -name '._*' -delete"
    fi
    ;;
  remove)
    echo "card:  $card"
    echo "removing the $count name(s) listed in $dir from $books/"
    gone=0
    for f in "$dir"/*.epub; do
      target="$books/$(basename "$f")"
      if [ -f "$target" ]; then rm "$target"; gone=$((gone + 1)); fi
      # ...and its sidecar, if the card has been near a Mac since.
      sidecar="$books/._$(basename "$f")"
      [ -f "$sidecar" ] && rm "$sidecar"
    done
    echo "removed $gone"
    ;;
  *)
    echo "unknown action: $action (add|remove)" >&2
    exit 2
    ;;
esac

# Flushed before the card can be pulled: macOS buffers writes, and a card yanked
# with dirty pages is how a FAT gets a truncated directory entry.
sync
echo "now on card: $(ls -1 "$books" | grep -c '\.epub$' || true) .epub in $books/"
echo "eject before pulling it:  diskutil eject \"${card%/}\""
