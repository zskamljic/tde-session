#pragma once

namespace cerberus {

// Locks the session and keeps it locked until the user's password is given. Returns the
// exit status: 0 once unlocked, other values when the session could not be locked. With
// `seconds`, the clock shows them.
int lockSession(bool seconds);

} // namespace cerberus
