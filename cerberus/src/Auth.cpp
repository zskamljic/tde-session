#include "Auth.hpp"

#include <fcntl.h>
#include <security/pam_appl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

namespace cerberus {
namespace {

// Replies to PAM, in memory of malloc() as PAM frees them once handed over; until then,
// freed here.
class Replies {
public:
    explicit Replies(int count)
        : m_items(static_cast<pam_response*>(std::calloc(size_t(count), sizeof(pam_response))))
        , m_count(count)
    {
    }
    ~Replies()
    {
        if (!m_items)
            return;
        for (int i = 0; i < m_count; ++i)
            std::free(m_items[i].resp);
        std::free(m_items);
    }
    Replies(const Replies&) = delete;
    Replies& operator=(const Replies&) = delete;

    explicit operator bool() const { return m_items != nullptr; }
    pam_response& operator[](int i) { return m_items[i]; }
    pam_response* release() { return std::exchange(m_items, nullptr); }

private:
    pam_response* m_items;
    int m_count;
};

// Answers PAM's questions: the password to those asked without echo, nothing to the rest.
int converse(int count, const pam_message** messages, pam_response** responses, void* data)
{
    Replies replies(count);
    if (!replies)
        return PAM_BUF_ERR;
    for (int i = 0; i < count; ++i) {
        if (messages[i]->msg_style == PAM_PROMPT_ECHO_OFF || messages[i]->msg_style == PAM_PROMPT_ECHO_ON) {
            replies[i].resp = strdup(static_cast<const char*>(data));
            if (!replies[i].resp)
                return PAM_BUF_ERR;
        }
    }
    *responses = replies.release();
    return PAM_SUCCESS;
}

// How many checks given up may still run; more would only pile up.
constexpr std::size_t MaxAbandoned = 4;

} // namespace

Authenticator::Authenticator(std::string user)
    : m_user(std::move(user))
{
}

Authenticator::~Authenticator()
{
    // Nothing waits for them any more.
    if (busy())
        m_abandoned.push_back(m_child);
    for (const pid_t child : m_abandoned) {
        kill(child, SIGKILL);
        waitpid(child, nullptr, 0);
    }
}

bool Authenticator::check(const Password& password)
{
    // Those given up and done are let go; while a few still run, no more are started.
    std::erase_if(m_abandoned, [](pid_t child) { return waitpid(child, nullptr, WNOHANG) != 0; });
    if (busy() || m_abandoned.size() >= MaxAbandoned)
        return false;
    int ends[2];
    if (pipe2(ends, O_CLOEXEC) != 0)
        return false;
    const pid_t child = fork();
    if (child < 0) {
        close(ends[0]);
        close(ends[1]);
        return false;
    }
    if (child == 0) {
        // The checking process: it says yes with a '1' and dies, taking its copy of the password
        // with it. Anything else, its death included, is a no.
        close(ends[0]);
        // The service's file in /etc/pam.d says how passwords are checked here.
        const pam_conv conversation {converse, const_cast<char*>(password.data())};
        pam_handle_t* handle = nullptr;
        bool accepted = false;
        if (pam_start("tde-cerberus", m_user.c_str(), &conversation, &handle) == PAM_SUCCESS) {
            accepted = pam_authenticate(handle, 0) == PAM_SUCCESS;
            pam_end(handle, PAM_SUCCESS);
        }
        const char answer = accepted ? '1' : '0';
        [[maybe_unused]] const ssize_t written = write(ends[1], &answer, 1);
        _exit(0);
    }
    close(ends[1]);
    m_answer = FileDescriptor(ends[0]);
    m_child = child;
    return true;
}

void Authenticator::cancel()
{
    if (!busy())
        return;
    m_answer.reset();
    m_abandoned.push_back(m_child);
    m_child = -1;
}

std::optional<bool> Authenticator::result()
{
    if (!busy())
        return std::nullopt;
    char answer = '0';
    const ssize_t length = read(m_answer.get(), &answer, 1);
    if (length < 0 && (errno == EAGAIN || errno == EINTR))
        return std::nullopt;
    m_answer.reset();
    waitpid(m_child, nullptr, 0);
    m_child = -1;
    return length == 1 && answer == '1';
}

} // namespace cerberus
