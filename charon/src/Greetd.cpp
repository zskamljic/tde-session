#include "Greetd.hpp"

#include "Json.hpp"
#include "Sessions.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace charon {
namespace {

// All of `size` bytes, however many reads it takes.
bool readAll(int fd, void* data, std::size_t size)
{
    auto* at = static_cast<char*>(data);
    while (size > 0) {
        const ssize_t got = ::read(fd, at, size);
        if (got < 0 && errno == EINTR)
            continue;
        if (got <= 0)
            return false;
        at += got;
        size -= std::size_t(got);
    }
    return true;
}

bool writeAll(int fd, const void* data, std::size_t size)
{
    const auto* at = static_cast<const char*>(data);
    while (size > 0) {
        const ssize_t sent = ::write(fd, at, size);
        if (sent < 0 && errno == EINTR)
            continue;
        if (sent <= 0)
            return false;
        at += sent;
        size -= std::size_t(sent);
    }
    return true;
}

} // namespace

std::string failureText(const Reply& reply)
{
    const auto says = [&](std::string_view code) { return reply.text.find(code) != std::string::npos; };
    if (says("NEW_AUTHTOK_REQD"))
        return "Your password has expired. Change it on another terminal (Ctrl+Alt+F2), then log in here.";
    if (says("ACCT_EXPIRED"))
        return "This account has expired.";
    // A wrong answer is said as GDM says it; other failures in greetd's words.
    if (reply.type == "auth_error")
        return "Sorry, that didn’t work. Please try again.";
    return reply.text.empty() ? "Logging in failed" : reply.text;
}

std::optional<Reply> parseReply(std::string_view payload)
{
    const auto object = json::parseObject(payload);
    if (!object)
        return std::nullopt;
    const auto field = [&](const char* key) {
        const auto it = object->find(key);
        return it == object->end() ? std::string() : it->second;
    };
    const std::string type = field("type");
    Reply reply;
    if (type == "success") {
        reply.kind = Reply::Kind::Success;
    } else if (type == "error") {
        reply.kind = Reply::Kind::Error;
        reply.type = field("error_type");
        reply.text = field("description");
    } else if (type == "auth_message") {
        reply.type = field("auth_message_type");
        reply.text = field("auth_message");
        reply.kind = reply.type == "visible" || reply.type == "secret" ? Reply::Kind::Question : Reply::Kind::Message;
    } else {
        return std::nullopt;
    }
    return reply;
}

bool Greetd::connect()
{
    const char* path = std::getenv("GREETD_SOCK");
    if (!path || !*path)
        return false;
    sockaddr_un address {};
    address.sun_family = AF_UNIX;
    if (std::strlen(path) >= sizeof(address.sun_path))
        return false;
    std::strcpy(address.sun_path, path);
    cerberus::FileDescriptor socket(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    if (!socket || ::connect(socket.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        std::fprintf(stderr, "tde-charon: cannot reach greetd at %s: %s\n", path, std::strerror(errno));
        return false;
    }
    m_socket = std::move(socket);
    return true;
}

bool Greetd::send(std::string& message)
{
    // The length first, in the byte order of the machine, then the message.
    const auto length = uint32_t(message.size());
    const bool sent
        = m_socket && writeAll(fd(), &length, sizeof(length)) && writeAll(fd(), message.data(), message.size());
    // It may hold an answer to a secret question.
    explicit_bzero(message.data(), message.size());
    if (sent)
        m_waiting = true;
    return sent;
}

bool Greetd::createSession(std::string_view user)
{
    std::string message = R"({"type":"create_session","username":)" + json::quoted(user) + '}';
    return send(message);
}

bool Greetd::answer(std::string* response)
{
    std::string message = R"({"type":"post_auth_message_response")";
    if (response) {
        std::string value = json::quoted(*response);
        message += R"(,"response":)" + value;
        explicit_bzero(value.data(), value.size());
        explicit_bzero(response->data(), response->size());
        response->clear();
    }
    message += '}';
    return send(message);
}

bool Greetd::startSession(const std::vector<std::string>& command, const std::vector<std::string>& environment)
{
    // greetd hands its words to /bin/sh joined by spaces; one line quoted for it keeps them whole.
    std::string message = R"({"type":"start_session","cmd":)" + json::list({commandLine(command)})
        + R"(,"env":)" + json::list(environment) + '}';
    return send(message);
}

bool Greetd::cancelSession()
{
    std::string message = R"({"type":"cancel_session"})";
    return send(message);
}

std::optional<Reply> Greetd::read()
{
    m_waiting = false;
    uint32_t length = 0;
    if (!readAll(fd(), &length, sizeof(length)) || length > 1 << 20)
        return std::nullopt;
    std::string payload(length, '\0');
    if (!readAll(fd(), payload.data(), length))
        return std::nullopt;
    return parseReply(payload);
}

} // namespace charon
