#include "core/wallclock.h"

namespace wallclock {

static long g_off = 0;

void init(long o) { g_off = o; }
long offset() { return g_off; }
void set_epoch(std::time_t epoch) { g_off = (long)(epoch - std::time(nullptr)); }
std::time_t now() { return std::time(nullptr) + g_off; }

} // namespace wallclock
