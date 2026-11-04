#include "Clock.hpp"

#include <langinfo.h>

#include <ctime>
#include <string_view>

namespace cerberus {

std::string formatted(const char* format)
{
    char text[128];
    const std::time_t now = std::time(nullptr);
    std::tm local {};
    localtime_r(&now, &local);
    return std::strftime(text, sizeof(text), format, &local) ? text : "";
}

std::string timeText(bool seconds)
{
    const std::string_view format = nl_langinfo(T_FMT);
    const bool twelveHours = format.find("%I") != std::string_view::npos || format.find("%r") != std::string_view::npos;
    if (twelveHours)
        return formatted(seconds ? "%-I:%M:%S %p" : "%-I:%M %p");
    return formatted(seconds ? "%H:%M:%S" : "%H:%M");
}

} // namespace cerberus
