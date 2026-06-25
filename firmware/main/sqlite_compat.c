// POSIX shims for the SQLite unix VFS on ESP-IDF.
//
// SQLite's os_unix.c references a handful of POSIX calls that ESP-IDF's newlib
// doesn't provide. They live in SQLite's syscall table / locking + create paths,
// none of which run for our read-only, single-process use over a FATFS card — so
// these are minimal stubs just to satisfy the linker. nanosleep is the one that
// can actually be called (sqlite3_sleep), so it's implemented for real via usleep.
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>

int nanosleep(const struct timespec* req, struct timespec* rem) {
    if (req) usleep((useconds_t)(req->tv_sec * 1000000ULL + req->tv_nsec / 1000));
    (void)rem;
    return 0;
}
int utimes(const char* path, const struct timeval times[2]) {
    (void)path; (void)times; return 0;
}
int fchmod(int fd, mode_t mode) { (void)fd; (void)mode; return 0; }
int fchown(int fd, uid_t owner, gid_t group) { (void)fd; (void)owner; (void)group; return 0; }
uid_t geteuid(void) { return 0; }
ssize_t readlink(const char* path, char* buf, size_t bufsiz) {
    (void)path; (void)buf; (void)bufsiz; errno = EINVAL; return -1;  // FAT has no symlinks
}
