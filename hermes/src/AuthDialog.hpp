#pragma once

#include <tde/Dialog.hpp>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace hermes {

// Asks for a password on behalf of polkit: what the program wants to do, whose password is
// needed, and the password.
class AuthDialog : public tde::Dialog {
    Q_OBJECT

public:
    AuthDialog(const QString& message, const QString& iconName, const QString& actionId, const QStringList& users,
        int user, QWidget* parent = nullptr);

    int user() const;
    // What PAM asks for, "Password:" mostly; `echo` shows what is typed.
    void setPrompt(const QString& prompt, bool echo);
    void showError(const QString& text);
    void showInfo(const QString& text);
    // While the password is checked, it cannot be changed.
    void setBusy(bool busy);

signals:
    void authenticate(const QString& response);
    void userChanged(int user);
    void cancelled();

protected:
    void reject() override;

private:
    QComboBox* m_users = nullptr;
    QLabel* m_prompt;
    QLineEdit* m_password;
    QLabel* m_status;
    QPushButton* m_accept;
};

} // namespace hermes
