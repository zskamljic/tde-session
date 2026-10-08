#include "Keyring.hpp"
#include "Prompt.hpp"

#include <tde/DesktopConfig.hpp>
#include <tde/Dialog.hpp>
#include <tde/Theme.hpp>

#include <QApplication>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QVBoxLayout>

#include <cstdio>
#include <iostream>
#include <optional>
#include <string>

#include <unistd.h>

using namespace Qt::StringLiterals;

namespace {

constexpr int Answered = 0;
constexpr int Declined = 1;

void answer(const QString& text)
{
    const QByteArray bytes = text.toUtf8() + '\n';
    std::fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout);
    std::fflush(stdout);
}

// A note of which program a kept passphrase was handed to: asked again by the same one for the
// same key, soon after, it did not work. Another program asking is no sign of that.
QString notePath(const QString& key)
{
    const QString folder = qEnvironmentVariable("XDG_RUNTIME_DIR", QDir::tempPath()) + u"/tde-askpass"_s;
    QDir().mkpath(folder);
    return folder + u'/'
        + QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex());
}

bool answeredLately(const QString& key)
{
    QFile note(notePath(key));
    const QFileInfo info(note);
    return note.open(QIODevice::ReadOnly) && note.readAll() == QByteArray::number(getppid())
        && info.lastModified().secsTo(QDateTime::currentDateTime()) < 60;
}

void noteAnswered(const QString& key)
{
    QFile note(notePath(key));
    if (note.open(QIODevice::WriteOnly | QIODevice::Truncate))
        note.write(QByteArray::number(getppid()));
}

bool keyringRuns()
{
    const auto bus = QDBusConnection::sessionBus();
    const QString service = u"org.freedesktop.secrets"_s;
    return bus.isConnected()
        && (bus.interface()->isServiceRegistered(service)
            || bus.interface()->activatableServiceNames().value().contains(service));
}

// Whether the program asking is ssh-add, which is adding the key already.
bool askedBySshAdd()
{
    QFile name(u"/proc/%1/comm"_s.arg(getppid()));
    return name.open(QIODevice::ReadOnly) && name.readAll().trimmed() == "ssh-add";
}

// Adds the key to the session's agent, so it is unlocked until the session ends. ssh-add asks
// this program again, which hands it the passphrase through stdin.
void addToAgent(const QString& key, const QString& passphrase)
{
    if (qEnvironmentVariableIsEmpty("SSH_AUTH_SOCK") || askedBySshAdd())
        return;
    QProcess add;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(u"SSH_ASKPASS"_s, QFileInfo(u"/proc/self/exe"_s).symLinkTarget());
    environment.insert(u"SSH_ASKPASS_REQUIRE"_s, u"force"_s);
    environment.insert(u"TDE_ASKPASS_RELAY"_s, u"1"_s);
    add.setProcessEnvironment(environment);
    add.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    add.start(u"ssh-add"_s, {u"-q"_s, key});
    add.write(passphrase.toUtf8() + '\n');
    add.closeWriteChannel();
    add.waitForFinished(10000);
}

// The dialog that asks for a passphrase or a password; nullopt when given up.
std::optional<QString> ask(const askpass::Prompt& prompt, bool keyring, bool keptFailed, bool* remember)
{
    using Kind = askpass::Prompt::Kind;
    const bool key = prompt.kind == Kind::KeyPassphrase;
    tde::Dialog dialog(key                      ? u"Unlock SSH Key"_s
            : prompt.kind == Kind::SudoPassword ? u"Administrator Password"_s
            : prompt.kind == Kind::Username     ? u"User Name"_s
                                                : u"Password"_s);
    QString message;
    if (key) {
        const QFileInfo file(prompt.key);
        message = u"Type the passphrase of the key “%1” in %2 to use it."_s.arg(
            file.fileName(), QDir::home().relativeFilePath(file.absolutePath()).prepend(u"~/"_s));
    } else if (prompt.kind == Kind::SudoPassword) {
        message = u"A program wants to run something as the administrator. Type your password to allow it."_s;
    } else {
        message = prompt.text;
    }
    auto* label = new QLabel(message, &dialog);
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    label->setMinimumWidth(360);
    dialog.contentLayout()->addWidget(label);
    if (keptFailed) {
        auto* error = new QLabel(u"The passphrase kept in the keyring did not work; it is forgotten."_s, &dialog);
        error->setWordWrap(true);
        error->setStyleSheet(u"color: %1;"_s.arg(tde::theme::colors().error.name()));
        dialog.contentLayout()->addWidget(error);
    }
    auto* field = new QLineEdit(&dialog);
    field->setEchoMode(prompt.kind == Kind::Username ? QLineEdit::Normal : QLineEdit::Password);
    dialog.contentLayout()->addWidget(field);
    QCheckBox* keep = nullptr;
    if (key && keyring) {
        keep = new QCheckBox(u"Remember it in the keyring, to unlock the key when needed"_s, &dialog);
        dialog.contentLayout()->addWidget(keep);
    }
    auto* buttons = new QHBoxLayout;
    auto* cancel = new QPushButton(u"Cancel"_s, &dialog);
    auto* accept = new QPushButton(key ? u"Unlock"_s : u"OK"_s, &dialog);
    buttons->addStretch(1);
    buttons->addWidget(cancel);
    buttons->addWidget(accept);
    dialog.contentLayout()->addLayout(buttons);
    dialog.setDefaultButton(accept);
    QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(accept, &QPushButton::clicked, &dialog, &QDialog::accept);
    field->setFocus();
    if (dialog.run() != QDialog::Accepted)
        return std::nullopt;
    *remember = keep && keep->isChecked();
    return field->text();
}

} // namespace

int main(int argc, char* argv[])
{
    // ssh-add started by this program: the passphrase comes through stdin.
    if (qEnvironmentVariable("TDE_ASKPASS_RELAY") == u"1") {
        std::string line;
        std::getline(std::cin, line);
        answer(QString::fromStdString(line));
        return Answered;
    }
    const QString text = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    const askpass::Prompt prompt = askpass::readPrompt(text, qEnvironmentVariable("SSH_ASKPASS_PROMPT"));

    // A passphrase kept before answers without asking, unless it did not work just now.
    bool keptFailed = false;
    if (prompt.kind == askpass::Prompt::Kind::KeyPassphrase) {
        if (const auto kept = askpass::keptPassphrase(prompt.key.toStdString())) {
            if (!answeredLately(prompt.key)) {
                const QCoreApplication core(argc, argv);
                noteAnswered(prompt.key);
                answer(QString::fromStdString(*kept));
                addToAgent(prompt.key, QString::fromStdString(*kept));
                return Answered;
            }
            askpass::forgetPassphrase(prompt.key.toStdString());
            keptFailed = true;
        }
    }

    // Without a display there is nobody to ask.
    if (qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") && qEnvironmentVariableIsEmpty("DISPLAY"))
        return Declined;
    QApplication app(argc, argv);
    QApplication::setApplicationName(u"tde-askpass"_s);
    QApplication::setApplicationDisplayName(u"Password"_s);
    QApplication::setApplicationVersion(QStringLiteral(TDE_SESSION_VERSION));
    tde::setDesktop(tde::loadDesktopConfig());
    tde::theme::apply(app, tde::desktop().appearance);

    using Kind = askpass::Prompt::Kind;
    if (prompt.kind == Kind::Confirm) {
        return tde::Dialog::confirm(nullptr, u"SSH"_s, prompt.text, {}, u"Allow"_s, false) ? Answered : Declined;
    }
    if (prompt.kind == Kind::Notice) {
        // Shown until the program is done and ends this one.
        tde::Dialog dialog(u"SSH"_s);
        auto* label = new QLabel(prompt.text, &dialog);
        label->setWordWrap(true);
        label->setMinimumWidth(320);
        dialog.contentLayout()->addWidget(label);
        dialog.run();
        return Answered;
    }

    bool remember = false;
    const auto typed = ask(prompt, keyringRuns(), keptFailed, &remember);
    if (!typed)
        return Declined;
    answer(*typed);
    if (prompt.kind == Kind::KeyPassphrase) {
        if (remember)
            askpass::keepPassphrase(prompt.key.toStdString(), typed->toStdString());
        addToAgent(prompt.key, *typed);
    }
    return Answered;
}
