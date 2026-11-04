#pragma once

#include <string>
#include <vector>

namespace charon {

// What the computer can be told to do from the login screen, through logind.
enum class PowerAction { Suspend, Restart, PowerOff };

// Those logind allows here, as for the active session of the seat; in the order of the menu.
std::vector<PowerAction> availablePowerActions();
std::string powerActionName(PowerAction action);
// Asks logind; false when it refuses.
bool runPowerAction(PowerAction action);

// Whether the computer runs on its battery, as UPower says; false without UPower.
bool onBattery();

} // namespace charon
