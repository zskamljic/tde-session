#include "PolkitAgent.hpp"

#include "AuthDialog.hpp"

#include <PolkitQt1/Subject>

#include <QCoreApplication>

#include <algorithm>

#include <pwd.h>
#include <unistd.h>

using namespace Qt::StringLiterals;

namespace hermes {
namespace {

// What the user is called, for the users polkit lets authenticate.
QString displayName(const PolkitQt1::Identity& identity)
{
    const QString name = identity.toString().section(u':', 1);
    const passwd* user = getpwnam(name.toLocal8Bit().constData());
    if (!user || !user->pw_gecos || !*user->pw_gecos)
        return name;
    const QString full = QString::fromLocal8Bit(user->pw_gecos).section(u',', 0, 0);
    return full.isEmpty() || full == name ? name : u"%1 (%2)"_s.arg(full, name);
}

} // namespace

PolkitAgent::PolkitAgent(QObject* parent)
    : PolkitQt1::Agent::Listener(parent)
{
}

PolkitAgent::~PolkitAgent()
{
    if (m_result)
        cancelAuthentication();
}

bool PolkitAgent::start()
{
    const PolkitQt1::UnixSessionSubject session(QCoreApplication::applicationPid());
    return registerListener(session, u"/io/github/zskamljic/Hermes/PolicyKit1/AuthenticationAgent"_s);
}

void PolkitAgent::initiateAuthentication(const QString& actionId, const QString& message, const QString& iconName,
    const PolkitQt1::Details&, const QString& cookie, const PolkitQt1::Identity::List& identities,
    PolkitQt1::Agent::AsyncResult* result)
{
    // polkit asks one at a time; another before this one is done is not answered.
    if (m_result) {
        result->setError(u"Another authentication is in progress"_s);
        result->setCompleted();
        return;
    }
    m_cookie = cookie;
    m_identities = identities;
    m_result = result;

    // The user's own account first, when it may answer.
    int own = 0;
    QStringList users;
    for (int i = 0; i < identities.size(); ++i) {
        users << displayName(identities[i]);
        const passwd* entry = getpwnam(identities[i].toString().section(u':', 1).toLocal8Bit().constData());
        if (entry && entry->pw_uid == getuid())
            own = i;
    }

    m_dialog = new AuthDialog(message, iconName, actionId, users, own);
    m_dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(m_dialog, &AuthDialog::authenticate, this, [this](const QString& response) {
        if (!m_session)
            return;
        m_dialog->setBusy(true);
        m_dialog->showInfo(QString());
        m_session->setResponse(response);
    });
    connect(m_dialog, &AuthDialog::userChanged, this, [this] { startSession(); });
    connect(m_dialog, &AuthDialog::cancelled, this, [this] {
        if (m_session)
            m_session->cancel();
        if (m_result) {
            m_result->setError(u"Cancelled"_s);
            finish();
        }
    });
    m_dialog->show();
    startSession();
}

void PolkitAgent::startSession()
{
    if (!m_result || !m_dialog)
        return;
    if (m_session)
        m_session->cancel();
    const int user = std::clamp(m_dialog->user(), 0, int(m_identities.size()) - 1);
    if (m_identities.isEmpty())
        return;
    // The session answers polkit itself once it is done, so the result is not handed to it.
    m_session = std::make_unique<PolkitQt1::Agent::Session>(m_identities[user], m_cookie);
    connect(m_session.get(), &PolkitQt1::Agent::Session::request, this, [this](const QString& request, bool echo) {
        if (m_dialog) {
            m_dialog->setPrompt(request, echo);
            if (std::exchange(m_retry, false))
                m_dialog->showError(u"That did not work. Please try again."_s);
        }
    });
    connect(m_session.get(), &PolkitQt1::Agent::Session::showError, this, [this](const QString& text) {
        if (m_dialog)
            m_dialog->showError(text);
    });
    connect(m_session.get(), &PolkitQt1::Agent::Session::showInfo, this, [this](const QString& text) {
        if (m_dialog)
            m_dialog->showInfo(text);
    });
    connect(m_session.get(), &PolkitQt1::Agent::Session::completed, this, [this](bool gained) {
        if (gained || !m_dialog) {
            finish();
            return;
        }
        // Wrong: asked again, with a word on why.
        m_retry = true;
        QMetaObject::invokeMethod(this, &PolkitAgent::startSession, Qt::QueuedConnection);
    });
    m_session->initiate();
}

bool PolkitAgent::initiateAuthenticationFinish()
{
    return true;
}

void PolkitAgent::cancelAuthentication()
{
    // polkit no longer needs the answer, as when the program asking went away.
    if (m_session)
        m_session->cancel();
    finish();
}

void PolkitAgent::finish()
{
    if (m_result) {
        m_result->setCompleted();
        m_result = nullptr;
    }
    // Deleted later: this may run from one of its signals.
    if (m_session)
        m_session.release()->deleteLater();
    if (m_dialog) {
        m_dialog->disconnect(this);
        m_dialog->close();
    }
    m_identities.clear();
    m_cookie.clear();
    m_retry = false;
}

} // namespace hermes
