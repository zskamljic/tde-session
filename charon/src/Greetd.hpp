#pragma once

#include <Owned.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace charon {

// What greetd answered a request with.
struct Reply {
    enum class Kind {
        Success, // logged in, or the session is to start
        Error, // with `type` auth_error for wrong answers, error for the rest
        Question, // a question of PAM's, `type` visible or secret, to be answered
        Message, // something PAM says, `type` info or error, to be acknowledged
    };
    Kind kind = Kind::Error;
    std::string type;
    std::string text;
};

// The connection to greetd, which logs users in with PAM and starts their sessions; the login
// screen asks and answers over it, one request at a time. Requests are sent at once, and the
// reply is read once fd() is readable.
class Greetd {
public:
    // Connects to the socket GREETD_SOCK names; false when there is none.
    bool connect();
    int fd() const { return m_socket.get(); }
    // A request is out, and its reply not read yet.
    bool waiting() const { return m_waiting; }

    // Begins logging `user` in; PAM's questions come as replies.
    bool createSession(std::string_view user);
    // Answers PAM's question; nothing for a message. The answer is wiped once sent.
    bool answer(std::string* response);
    // Once logged in, the session to run when the greeter ends: `command` with `environment`
    // added to what PAM sets.
    bool startSession(const std::vector<std::string>& command, const std::vector<std::string>& environment);
    // Gives up logging in, as for another user.
    bool cancelSession();

    // The reply, once fd() was readable; nothing when the connection broke.
    std::optional<Reply> read();

private:
    bool send(std::string& message);

    cerberus::FileDescriptor m_socket;
    bool m_waiting = false;
};

// The reply in `payload`, as greetd sends it; nothing when it makes no sense.
std::optional<Reply> parseReply(std::string_view payload);

// What the login screen says for an error reply. greetd gives PAM's account checks no
// conversation, so a password that ran out cannot be changed through it: the person is told
// where it can be.
std::string failureText(const Reply& reply);

} // namespace charon
