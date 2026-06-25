# The portable UI lives in ../../emulator/src (core/ + apps/); we compile it
# straight from there so the emulator and device share one source of truth.
file(GLOB CORE_SRCS ${CMAKE_CURRENT_LIST_DIR}/../../emulator/src/core/*.cpp)
file(GLOB APP_SRCS  ${CMAKE_CURRENT_LIST_DIR}/../../emulator/src/apps/*.cpp)

# Offline wiki: the host's SqliteWiki backing is portable (sqlite3 C API + the
# vendored FTS5 amalgamation), so the device reuses the EXACT same .cpp — only
# the DB path differs (/sdcard/wiki.db). Pulled in explicitly (it lives under
# host/, outside the core/apps GLOBs) alongside the amalgamation.
# sqlite3_esp32.c is a thin shim that #includes the amalgamation with the ESP-IDF
# build defines it needs (see that file); we compile the shim, not sqlite3.c.
set(WIKI_SRCS
    ${CMAKE_CURRENT_LIST_DIR}/../../emulator/src/host/sqlite_wiki.cpp
    "sqlite3_esp32.c"
    "sqlite_compat.c")

idf_component_register(
    SRCS "main.cpp" ${CORE_SRCS} ${APP_SRCS} ${WIKI_SRCS}
         "device/radio/sx1262.cpp"
    INCLUDE_DIRS "." "device/radio"
                 "${CMAKE_CURRENT_LIST_DIR}/../../emulator/src"
                 "${CMAKE_CURRENT_LIST_DIR}/../../third_party/sqlite"
    REQUIRES driver esp_timer nvs_flash fatfs sdmmc esp_driver_sdspi vfs esp_lcd
)

# The shared core/apps printf-format node IDs with %x; uint32_t is `unsigned` on
# the host but `long unsigned` on xtensa, so IDF's -Werror=format trips. Harmless
# (both 32-bit) — relax these to warnings here rather than diverge the source.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=format -Wno-error=format-truncation -Wno-error=format-nonliteral)

# SQLite build tuning for the no-PSRAM ESP32-S3 over a FATFS SD card: FTS5 on,
# single-threaded, temp tables in RAM (no temp files on the card), no WAL/mmap
# (FATFS supports neither). We only ever open the DB read-only.
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    SQLITE_ENABLE_FTS5
    SQLITE_THREADSAFE=0
    SQLITE_DEFAULT_MEMSTATUS=0
    SQLITE_TEMP_STORE=3
    SQLITE_OMIT_WAL
    SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_DISABLE_DIRSYNC
    SQLITE_MAX_MMAP_SIZE=0)
