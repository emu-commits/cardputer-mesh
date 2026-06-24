#!/usr/bin/env bash
# Fetch the public-domain SQLite amalgamation (with FTS5) into third_party/sqlite/.
# Same single-file source the ESP32 firmware build uses, so host + device share
# one SQLite. Gitignored; run once after cloning.
set -euo pipefail
DEST="$(cd "$(dirname "$0")/.." && pwd)/third_party/sqlite"
VER="${SQLITE_AMALGAMATION_VER:-3460100}"   # 3.46.1
YEAR="${SQLITE_AMALGAMATION_YEAR:-2024}"
URL="https://www.sqlite.org/${YEAR}/sqlite-amalgamation-${VER}.zip"
mkdir -p "$DEST"
if [ -f "$DEST/sqlite3.c" ]; then echo "sqlite3.c already present"; exit 0; fi
echo "Downloading $URL"
tmp="$(mktemp -d)"
curl -fsSL "$URL" -o "$tmp/s.zip"
unzip -j -o "$tmp/s.zip" "*/sqlite3.c" "*/sqlite3.h" -d "$DEST"
rm -rf "$tmp"
echo "SQLite amalgamation ready in $DEST"
