#pragma once

namespace charon {

// Shows the login screen until someone logs in, then has greetd start their session once it
// ends. Returns the exit status: 0 once a session is to start.
int greet();

} // namespace charon
