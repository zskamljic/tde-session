#pragma once

#include <string>

namespace cerberus {

// The time now as `format` of strftime() has it, in the language of the locale.
std::string formatted(const char* format);
// The clock as the locale writes it: with AM and PM where that is usual, and the seconds when
// asked for.
std::string timeText(bool seconds);

} // namespace cerberus
