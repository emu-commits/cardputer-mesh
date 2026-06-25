// ESP-IDF compile shim for the vendored SQLite amalgamation.
//
// Two defines MUST be in effect when sqlite3.c is preprocessed, and attaching
// them via CMake set_source_files_properties is unreliable for a source file
// that lives outside the component directory — so we set them here and #include
// the amalgamation directly, guaranteeing they apply:
//   NDEBUG     ESP-IDF keeps assert() live by default; without NDEBUG, SQLite's
//              asserts reference SQLITE_DEBUG-only helpers that aren't compiled,
//              causing a flood of "implicit declaration" errors.
//   lstat=stat ESP-IDF's newlib has no lstat(); FATFS has no symlinks and we
//              only ever open the DB read-only, so stat() is equivalent.
// The FTS5 / threadsafe / temp-store tuning is applied component-wide via
// target_compile_definitions in CMakeLists.txt.
#define NDEBUG 1
#define lstat stat
#include "sqlite3.c"
