#include "Auth.hpp"

#include <security/pam_appl.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <string_view>

namespace cerberus {
namespace {

// Answers PAM's questions: the password to those asked without echo, nothing to the rest.
int converse(int count, const pam_message** messages, pam_response** responses, void* data)
{
    auto* replies = static_cast<pam_response*>(std::calloc(size_t(count), sizeof(pam_response)));
    if (!replies)
        return PAM_BUF_ERR;
    for (int i = 0; i < count; ++i) {
        if (messages[i]->msg_style == PAM_PROMPT_ECHO_OFF || messages[i]->msg_style == PAM_PROMPT_ECHO_ON) {
            replies[i].resp = strdup(static_cast<const char*>(data));
            if (!replies[i].resp) {
                for (int j = 0; j < i; ++j)
                    std::free(replies[j].resp);
                std::free(replies);
                return PAM_BUF_ERR;
            }
        }
    }
    *responses = replies;
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
    if (m_event >= 0)
        close(m_event);
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
    [[maybe_unused]] const ssize_t written = write(m_event, &one, sizeof(one));
}

std::optional<bool> Authenticator::result()
{
    uint64_t count = 0;
    if (read(m_event, &count, sizeof(count)) != sizeof(count))
        return std::nullopt;
    if (m_thread.joinable())
        m_thread.join();
    return m_accepted.load();
}

} // namespace cerberus
