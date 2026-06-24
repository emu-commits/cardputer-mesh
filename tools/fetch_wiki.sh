#!/usr/bin/env bash
# Download the offline wiki database (GitHub Release asset) to the SD / emulator
# sandbox. The DB is too large for git (541 MB); it ships as a Release asset.
set -euo pipefail
REPO="${WIKI_REPO:-emu-commits/cardputer-mesh}"
TAG="${WIKI_RELEASE_TAG:-data-wiki-v1}"
DEST="${1:-emulator/emu_sd/wiki.db}"
mkdir -p "$(dirname "$DEST")"
if [ -f "$DEST" ]; then echo "wiki.db already at $DEST"; exit 0; fi
echo "Downloading wiki.db from $REPO release $TAG -> $DEST"
gh release download "$TAG" -R "$REPO" -p 'wiki.db' -O "$DEST"
echo "Done. ($(du -h "$DEST" | cut -f1))"
