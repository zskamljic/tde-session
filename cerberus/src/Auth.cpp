#include "Auth.hpp"

#include <security/pam_appl.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <string_view>
#include <utility>

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

} // namespace

Authenticator::Authenticator(std::string user)
    : m_user(std::move(user))
    , m_event(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK))
{
}

Authenticator::~Authenticator()
{
    if (m_thread.joinable())
        m_thread.join();
}

void Authenticator::check(const Password& password)
{
    if (busy())
        return;
    m_password.clear();
    m_password.append(std::string_view(password.data()));
    m_thread = std::thread(&Authenticator::work, this);
}

void Authenticator::work()
{
    // The service's file in /etc/pam.d says how passwords are checked here.
    const pam_conv conversation {converse, const_cast<char*>(m_password.data())};
    pam_handle_t* handle = nullptr;
    bool accepted = false;
    if (pam_start("tde-cerberus", m_user.c_str(), &conversation, &handle) == PAM_SUCCESS) {
        accepted = pam_authenticate(handle, 0) == PAM_SUCCESS;
        pam_end(handle, PAM_SUCCESS);
    }
    m_password.clear();
    m_accepted = accepted;
    const uint64_t one = 1;
    [[maybe_unused]] const ssize_t written = write(m_event.get(), &one, sizeof(one));
}

std::optional<bool> Authenticator::result()
{
    uint64_t count = 0;
    if (read(m_event.get(), &count, sizeof(count)) != sizeof(count))
        return std::nullopt;
    if (m_thread.joinable())
        m_thread.join();
    return m_accepted.load();
}

} // namespace cerberus
