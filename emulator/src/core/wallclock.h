// Wall-clock seam — the single source of "what time is it" for the whole UI.
//
// The device has no battery-backed RTC, so std::time(nullptr) starts at the
// epoch + uptime on every boot. Rather than sprinkle per-app offsets around
// (the Clock app used to), every screen that shows time (Clock, the built-in
// status bar, etc.) reads wallclock::now(). Setting the clock stores an offset
// from std::time which is persisted (clock.offset_s) and restored at boot.
#pragma once
#include <ctime>

namespace wallclock {

void init(long offset_s);            // restore the persisted offset at boot
long offset();                       // current offset (seconds) for persisting
void set_epoch(std::time_t epoch);   // set the wall clock to `epoch` (adjusts offset)
std::time_t now();                   // std::time(nullptr) + offset

} // namespace wallclock
