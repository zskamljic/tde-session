#pragma once

#include <QString>

#include <optional>

namespace shell {

// The user's session as logind knows it, which locking and polkit go by.
struct LoginSession {
    QString id; // "2"
    QString path; // its object path on the system bus
};

// The session this program belongs to; for one that systemd runs as a service of the user,
// and so in no session of its own, the user's graphical session. Nullopt without logind.
std::optional<LoginSession> loginSession();

} // namespace shell
