#pragma once

namespace cerberus {

// Tells logind whether the session is locked, for programs that ask it, such as
// `loginctl show-session -p LockedHint`. Nothing comes of it without logind.
void setLockedHint(bool locked);

} // namespace cerberus
