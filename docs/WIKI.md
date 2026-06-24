# Offline Wiki

The Wiki app reads an offline encyclopedia from a SQLite database with built-in
FTS5 full-text search (`wiki.db` — Simple English Wikipedia, ~267K articles,
541 MB). Access is via the `wiki::WikiSource` seam: the host uses `SqliteWiki`
(the vendored SQLite amalgamation, FTS5); the device build uses the same
amalgamation over the SD card.

## Where it lives
- **Device:** copy `wiki.db` to the **SD card root** (`/wiki.db`).
- **Emulator:** place it at `emulator/emu_sd/wiki.db` (the SD sandbox).

The DB is **not in git** (too large). It is published as a **GitHub Release
asset** and fetched with `gh`:

```sh
tools/fetch_wiki.sh                 # -> emulator/emu_sd/wiki.db
tools/fetch_wiki.sh /path/to/sd/wiki.db
```

To (re)publish the asset (maintainer, one-time, ~541 MB upload):

```sh
tools/publish_wiki.sh ~/Downloads/wiki.db
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
SQLite pages from the SD card, but FTS5 working memory on a 512 KB no-PSRAM part
is unverified — needs an on-device test. If it's too heavy, the fallback is a
pre-built lighter index; the `WikiSource` seam keeps that swap localized.
