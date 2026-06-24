#!/usr/bin/env bash
# One-time: publish the local wiki.db as a GitHub Release asset (run by a human;
# uploads ~541 MB). Usage: tools/publish_wiki.sh [path-to-wiki.db]
set -euo pipefail
REPO="${WIKI_REPO:-emu-commits/cardputer-mesh}"
TAG="${WIKI_RELEASE_TAG:-data-wiki-v1}"
SRC="${1:-$HOME/Downloads/wiki.db}"
[ -f "$SRC" ] || { echo "not found: $SRC"; exit 1; }
gh release view "$TAG" -R "$REPO" >/dev/null 2>&1 || \
  gh release create "$TAG" -R "$REPO" --title "Offline wiki data" \
    --notes "Simple English Wikipedia (267K articles, FTS5). Copy wiki.db to the SD card root."
gh release upload "$TAG" "$SRC" -R "$REPO" --clobber
echo "Uploaded $SRC to $REPO release $TAG"
