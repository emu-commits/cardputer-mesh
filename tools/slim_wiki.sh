#!/usr/bin/env bash
# Produce a slimmed wiki DB: drop tables the firmware never queries
# (section_search FTS5, vocabulary, vectors/ANN) and VACUUM. ~541MB -> ~350MB.
# No format change — still SQLite/FTS5 over articles + sections.
# Usage: tools/slim_wiki.sh [src wiki.db] [out wiki-slim.db]
set -euo pipefail
SRC="${1:-$HOME/Downloads/wiki.db}"
OUT="${2:-$HOME/Downloads/wiki-slim.db}"
[ -f "$SRC" ] || { echo "not found: $SRC"; exit 1; }
echo "copying $SRC -> $OUT"; cp "$SRC" "$OUT"
echo "dropping unused tables + vacuuming (this takes a few minutes)..."
sqlite3 "$OUT" <<SQL
DROP TABLE IF EXISTS section_search;
DROP TABLE IF EXISTS vocabulary;
DROP TABLE IF EXISTS vectors_ann_index;
DROP TABLE IF EXISTS vectors_ann_chunks;
DROP TABLE IF EXISTS vectors;
VACUUM;
SQL
echo "done.  sizes:"; du -h "$SRC" "$OUT"
