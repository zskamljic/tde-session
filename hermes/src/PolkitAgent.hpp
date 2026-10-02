#pragma once

#include <PolkitQt1/Agent/Listener>
#include <PolkitQt1/Agent/Session>
#include <PolkitQt1/Identity>

#include <QPointer>

#include <memory>

namespace hermes {

class AuthDialog;

// The desktop's polkit agent: when a program asks to do something that needs a password,
// such as mounting a disk or installing updates, it asks the user for it.
class PolkitAgent : public PolkitQt1::Agent::Listener {
    Q_OBJECT

public:
    explicit PolkitAgent(QObject* parent = nullptr);
    ~PolkitAgent() override;

    // Becomes the agent of this login session; false when another program is it already.
    bool start();

public slots:
    void initiateAuthentication(const QString& actionId, const QString& message, const QString& iconName,
        const PolkitQt1::Details& details, const QString& cookie, const PolkitQt1::Identity::List& identities,
        PolkitQt1::Agent::AsyncResult* result) override;
    bool initiateAuthenticationFinish() override;
    void cancelAuthentication() override;

private:
    void startSession();
    void finish();

    // The request being answered: one at a time, as polkit asks them.
    QString m_cookie;
    PolkitQt1::Identity::List m_identities;
    PolkitQt1::Agent::AsyncResult* m_result = nullptr; // polkit's, until completed
    std::unique_ptr<PolkitQt1::Agent::Session> m_session;
    QPointer<AuthDialog> m_dialog;
    bool m_retry = false;
};

} // namespace hermes
