#pragma once

#include "Owned.hpp"
#include "Password.hpp"

#include <optional>
#include <string>
#include <vector>

#include <sys/types.h>

namespace cerberus {

// Checks passwords with PAM in a process of its own, away from the drawing, which goes on
// meanwhile, and so that a check taking long can be given up. When the answer is in, fd()
// becomes readable.
class Authenticator {
public:
    explicit Authenticator(std::string user);
    ~Authenticator();

    Authenticator(const Authenticator&) = delete;
    Authenticator& operator=(const Authenticator&) = delete;

    // Readable once the answer is in; -1 while nothing is being checked, which poll() passes by.
    int fd() const { return m_answer.get(); }
    bool busy() const { return m_child > 0; }

    // Starts checking `password`, which only the checking process keeps; false when it cannot,
    // as while too many checks given up still run.
    bool check(const Password& password);
    // Gives up the check under way. Its answer is never heard, so it cannot unlock; it still runs
    // to its end, though, so that a wrong password counts against the user as any other, and
    // waits as long.
    void cancel();
    // The answer, once fd() was readable: whether the password was right.
    std::optional<bool> result();

private:
    std::string m_user;
    FileDescriptor m_answer;
    pid_t m_child = -1;
    std::vector<pid_t> m_abandoned; // checks given up, still running
};

} // namespace cerberus
