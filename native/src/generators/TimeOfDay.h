#pragma once
// Shared "current local time-of-day in milliseconds since midnight" helper - the C++
// equivalent of C#'s `DateTime.Now.TimeOfDay.TotalMilliseconds`, used by every
// generator that plays back timestamped content against wall-clock time (Fade, SRT,
// LRC, ASS).
#include <chrono>
#include <ctime>

inline double NowTimeOfDayMs() {
    using namespace std::chrono;
    auto now = system_clock::now();
    std::time_t nowTimeT = system_clock::to_time_t(now);

    std::tm localTm{};
#if defined(_WIN32)
    localtime_s(&localTm, &nowTimeT);
#else
    localtime_r(&nowTimeT, &localTm);
#endif

    long long totalMs = duration_cast<milliseconds>(now.time_since_epoch()).count();
    long long subSecondMs = totalMs % 1000;

    double hoursMs = static_cast<double>(localTm.tm_hour) * 3600000.0;
    double minutesMs = static_cast<double>(localTm.tm_min) * 60000.0;
    double secondsMs = static_cast<double>(localTm.tm_sec) * 1000.0;
    return hoursMs + minutesMs + secondsMs + static_cast<double>(subSecondMs);
}
