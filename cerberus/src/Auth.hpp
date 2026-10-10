#pragma once

#include "Owned.hpp"
#include "Password.hpp"

#include <atomic>
#include <optional>
#include <string>
#include <thread>

namespace cerberus {

// Checks passwords with PAM, away from the drawing, which goes on meanwhile. When the answer
// is in, fd() becomes readable.
class Authenticator {
public:
    explicit Authenticator(std::string user);
    ~Authenticator();

    Authenticator(const Authenticator&) = delete;
    Authenticator& operator=(const Authenticator&) = delete;

    int fd() const { return m_event.get(); }
    bool busy() const { return m_thread.joinable(); }

    // Starts checking `password`, a copy of which is kept only until then.
    void check(const Password& password);
    // The answer, once fd() was readable: whether the password was right.
    std::optional<bool> result();

private:
    void work();

    std::string m_user;
    FileDescriptor m_event;
    Password m_password;
    std::atomic<bool> m_accepted = false;
    std::thread m_thread;
};

} // namespace cerberus
