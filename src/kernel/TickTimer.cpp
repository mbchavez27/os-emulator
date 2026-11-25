#include "kernel/TickTimer.h"

#include <thread>

#ifdef _WIN32
// Kept to this file so <windows.h> macros never reach the rest of the project.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <timeapi.h>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

TickTimer::TickTimer(std::chrono::microseconds interval) : interval_(interval)
{
    // Windows 10 1803+; older versions fail and fall back to sleeping.
    handle_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                     TIMER_ALL_ACCESS);
    if (handle_ == nullptr)
    {
        timeBeginPeriod(1);
    }
}

TickTimer::~TickTimer()
{
    if (handle_ != nullptr)
    {
        CloseHandle(handle_);
    }
    else
    {
        timeEndPeriod(1);
    }
}

void TickTimer::wait()
{
    if (handle_ != nullptr)
    {
        LARGE_INTEGER due;
        due.QuadPart = -static_cast<LONGLONG>(interval_.count()) * 10; // Relative, in 100 ns units.
        if (SetWaitableTimer(handle_, &due, 0, nullptr, nullptr, FALSE) &&
            WaitForSingleObject(handle_, INFINITE) == WAIT_OBJECT_0)
        {
            return;
        }
    }
    std::this_thread::sleep_for(interval_);
}

#else

TickTimer::TickTimer(std::chrono::microseconds interval) : interval_(interval) {}

TickTimer::~TickTimer() = default;

void TickTimer::wait()
{
    std::this_thread::sleep_for(interval_);
}

#endif
