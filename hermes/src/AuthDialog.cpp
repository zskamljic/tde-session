#include "AuthDialog.hpp"

#include <tde/Theme.hpp>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace hermes {

AuthDialog::AuthDialog(const QString& message, const QString& iconName, const QString& actionId,
    const QStringList& users, int user, QWidget* parent)
    : tde::Dialog(u"Authentication Required"_s, parent)
{
    auto* top = new QHBoxLayout;
    top->setSpacing(16);
    auto* icon = new QLabel;
    const QIcon themed = QIcon::fromTheme(iconName, QIcon::fromTheme(u"dialog-password"_s));
    icon->setPixmap(themed.pixmap(48, 48));
    icon->setAlignment(Qt::AlignTop);
    top->addWidget(icon);

    auto* text = new QVBoxLayout;
    auto* heading = new QLabel(message);
    heading->setObjectName(u"ConfirmMessage"_s);
    heading->setTextFormat(Qt::PlainText);
    heading->setWordWrap(true);
    heading->setMinimumWidth(340);
    heading->setMaximumWidth(460);
    text->addWidget(heading);
    // The action, for those who want to know what exactly is asked for.
    auto* detail = new QLabel(actionId);
    detail->setObjectName(u"AboutDetails"_s);
    detail->setTextFormat(Qt::PlainText);
    text->addWidget(detail);
    top->addLayout(text, 1);
    contentLayout()->addLayout(top);
    contentLayout()->addSpacing(8);

    if (users.size() > 1) {
        m_users = new QComboBox;
        m_users->addItems(users);
        m_users->setCurrentIndex(user);
        connect(m_users, &QComboBox::currentIndexChanged, this, &AuthDialog::userChanged);
        contentLayout()->addWidget(m_users);
    } else if (!users.isEmpty()) {
        auto* who = new QLabel(u"Password of %1"_s.arg(users.first()));
        who->setTextFormat(Qt::PlainText);
        contentLayout()->addWidget(who);
    }

    m_prompt = new QLabel;
    m_prompt->setTextFormat(Qt::PlainText);
    m_prompt->hide();
    contentLayout()->addWidget(m_prompt);
    m_password = new QLineEdit;
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setPlaceholderText(u"Password"_s);
    contentLayout()->addWidget(m_password);
    m_status = new QLabel;
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    m_status->hide();
    contentLayout()->addWidget(m_status);

    auto* buttons = new QDialogButtonBox;
    buttons->addButton(QDialogButtonBox::Cancel);
    m_accept = buttons->addButton(u"Authenticate"_s, QDialogButtonBox::AcceptRole);
    connect(buttons, &QDialogButtonBox::rejected, this, &AuthDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (m_accept->isEnabled())
            emit authenticate(m_password->text());
    });
    contentLayout()->addSpacing(6);
    contentLayout()->addWidget(buttons);
    setDefaultButton(m_accept);
    m_password->setFocus();
    setBusy(true); // until polkit asks for the password
}

int AuthDialog::user() const
{
    return m_users ? m_users->currentIndex() : 0;
}

void AuthDialog::setPrompt(const QString& prompt, bool echo)
{
    // "Password:" says nothing the field does not; other questions are shown.
    const QString trimmed = prompt.trimmed();
    const bool plain = trimmed.compare(u"Password:"_s, Qt::CaseInsensitive) == 0 || trimmed.isEmpty();
    m_prompt->setText(trimmed);
    m_prompt->setVisible(!plain);
    m_password->setEchoMode(echo ? QLineEdit::Normal : QLineEdit::Password);
    m_password->clear();
    setBusy(false);
    m_password->setFocus();
}

void AuthDialog::showError(const QString& text)
{
    m_status->setStyleSheet(u"color: %1;"_s.arg(tde::theme::colors().error.name()));
    m_status->setText(text);
    m_status->setVisible(!text.isEmpty());
}

void AuthDialog::showInfo(const QString& text)
{
    m_status->setStyleSheet(QString());
    m_status->setText(text);
    m_status->setVisible(!text.isEmpty());
}

void AuthDialog::setBusy(bool busy)
{
    m_password->setEnabled(!busy);
    m_accept->setEnabled(!busy);
    if (m_users)
        m_users->setEnabled(!busy);
}

void AuthDialog::reject()
{
    emit cancelled();
    tde::Dialog::reject();
}

} // namespace hermes
