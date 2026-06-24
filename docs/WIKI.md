# Offline Wiki

The Wiki app reads an offline encyclopedia from a SQLite database with built-in
FTS5 full-text search (`wiki.db` — Simple English Wikipedia, ~267K articles,
~320 MB slimmed). Access is via the `wiki::WikiSource` seam: the host uses
`SqliteWiki` (the vendored SQLite amalgamation, FTS5); the device build uses the
same amalgamation over the SD card.

The published `wiki.db` is **slimmed** by `tools/slim_wiki.sh`, which drops the
tables the firmware never queries (`section_search` FTS5, `vocabulary`,
`vectors*` ANN) and VACUUMs: ~517 MB → ~320 MB, no format change. Slimming is a
disk-footprint win only — it does **not** reduce runtime RAM (heap is driven by
the FTS5 page cache, not file size; see the device note below).

## Where it lives
- **Device:** copy `wiki.db` to the **SD card root** (`/wiki.db`).
- **Emulator:** place it at `emulator/emu_sd/wiki.db` (the SD sandbox).

The DB is **not in git** (too large). It is published as a **GitHub Release
asset** and fetched with `gh`:

```sh
tools/fetch_wiki.sh                 # -> emulator/emu_sd/wiki.db
tools/fetch_wiki.sh /path/to/sd/wiki.db
```

To (re)publish the asset (maintainer; ~320 MB upload). Slim first, then publish
the result **named `wiki.db`** so it clobbers the existing release asset:

```sh
tools/slim_wiki.sh ~/Downloads/wiki.db ~/Downloads/wiki-slim.db
cp ~/Downloads/wiki-slim.db /tmp/wiki.db
tools/publish_wiki.sh /tmp/wiki.db    # asset filename must be wiki.db
```

## Build dependency
SQLite is vendored as the public-domain amalgamation (FTS5), fetched on first
build:

```sh
tools/fetch_sqlite.sh              # -> third_party/sqlite/{sqlite3.c,sqlite3.h}
```

The emulator Makefile auto-runs this. On device it's an ESP-IDF component built
with `-DSQLITE_ENABLE_FTS5`.

## Device note (to validate on hardware)
SQLite pages from the SD card. With the firmware's device-representative pragmas
(`cache_size=-32` → 32 KiB, `mmap_size=0`, `temp_store=MEMORY`), the host
measures the SQLite heap at **~147 KB to open**, a **~230 KB peak** during a
search, dropping to **~70 KB** between queries after `sqlite3_db_release_memory()`
and fully reclaimed on close. That fits the 512 KB no-PSRAM part under the arena
model (DB open only while the Wiki app is foreground).

The remaining unknown is **heap fragmentation** on the real device (SQLite's many
small allocations plus the firmware's `std::string` use) — needs an on-device
test. If it's too heavy, the fallback is a pre-built lighter index; the
`WikiSource` seam keeps that swap localized.
