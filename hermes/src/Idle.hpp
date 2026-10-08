#pragma once

// ext-idle-notify, which tells after how long without input the user is away: for locking the
// screen and for saving power.

#include "ext-idle-notify-v1-client-protocol.h"

#include <Proxy.hpp>

SHELL_PROXY(ext_idle_notifier_v1, ext_idle_notifier_v1_destroy);
SHELL_PROXY(ext_idle_notification_v1, ext_idle_notification_v1_destroy);
