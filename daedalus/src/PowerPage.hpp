#pragma once

#include "Pages.hpp"

namespace daedalus {

// How the computer saves power: its power mode, when the screens turn off, and when it sleeps
// by itself, plugged in and on battery.
class PowerPage : public Page {
public:
    explicit PowerPage(Settings& settings, QWidget* parent = nullptr);
};

} // namespace daedalus
