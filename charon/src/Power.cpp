#include "Power.hpp"

#include <Owned.hpp>

#include <systemd/sd-bus.h>

#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace charon {
namespace {

using Bus = cerberus::Owned<sd_bus, sd_bus_flush_close_unref>;
using Message = cerberus::Owned<sd_bus_message, sd_bus_message_unref>;

constexpr const char* Login = "org.freedesktop.login1";
constexpr const char* LoginPath = "/org/freedesktop/login1";
constexpr const char* LoginManager = "org.freedesktop.login1.Manager";

Bus systemBus()
{
    sd_bus* opened = nullptr;
    if (sd_bus_open_system(&opened) < 0)
        return Bus(nullptr);
    return Bus(opened);
}

const char* methodOf(PowerAction action)
{
    switch (action) {
    case PowerAction::Suspend:
        return "Suspend";
    case PowerAction::Restart:
        return "Reboot";
    case PowerAction::PowerOff:
        return "PowerOff";
    }
    return "";
}

} // namespace

std::vector<PowerAction> availablePowerActions()
{
    const Bus bus = systemBus();
    std::vector<PowerAction> actions;
    if (!bus)
        return actions;
    for (const PowerAction action : {PowerAction::Suspend, PowerAction::Restart, PowerAction::PowerOff}) {
        sd_bus_message* replied = nullptr;
        const std::string method = std::string("Can") + methodOf(action);
        if (sd_bus_call_method(bus.get(), Login, LoginPath, LoginManager, method.c_str(), nullptr, &replied, "") < 0)
            continue;
        const Message reply(replied);
        const char* answer = nullptr;
        // "challenge" is asked for a password by polkit, which nobody here can give; GDM leaves
        // those out too.
        if (sd_bus_message_read(reply.get(), "s", &answer) > 0 && answer && std::string_view(answer) == "yes")
            actions.push_back(action);
    }
    return actions;
}

std::string powerActionName(PowerAction action)
{
    switch (action) {
    case PowerAction::Suspend:
        return "Suspend";
    case PowerAction::Restart:
        return "Restart";
    case PowerAction::PowerOff:
        return "Power Off";
    }
    return {};
}

bool runPowerAction(PowerAction action)
{
    const Bus bus = systemBus();
    if (!bus)
        return false;
    sd_bus_error error {};
    const bool done
        = sd_bus_call_method(bus.get(), Login, LoginPath, LoginManager, methodOf(action), &error, nullptr, "b", 0) >= 0;
    if (!done)
        std::fprintf(stderr, "tde-charon: %s: %s\n", methodOf(action), error.message ? error.message : "refused");
    sd_bus_error_free(&error);
    return done;
}

bool onBattery()
{
    const Bus bus = systemBus();
    int battery = 0;
    if (!bus
        || sd_bus_get_property_trivial(bus.get(), "org.freedesktop.UPower", "/org/freedesktop/UPower",
               "org.freedesktop.UPower", "OnBattery", nullptr, 'b', &battery)
            < 0)
        return false;
    return battery != 0;
}

} // namespace charon
