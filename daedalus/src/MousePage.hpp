#pragma once

#include "Pages.hpp"

namespace daedalus {

// Mice and touchpads: how fast the pointer goes and how far they scroll, which way, and
// tapping on the touchpad to click.
class MousePage : public Page {
public:
    explicit MousePage(Settings& settings, QWidget* parent = nullptr);
};

// Whether the computer has a touchpad, as the kernel names its input devices.
bool hasTouchpad();

} // namespace daedalus
