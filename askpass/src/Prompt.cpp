#include "Prompt.hpp"

#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace askpass {

Prompt readPrompt(const QString& text, const QString& sshPrompt)
{
    const QString trimmed = text.trimmed();
    if (sshPrompt == u"confirm")
        return {Prompt::Kind::Confirm, {}, trimmed};
    if (sshPrompt == u"none")
        return {Prompt::Kind::Notice, {}, trimmed};
    // ssh: "Enter passphrase for key '/home/me/.ssh/id_ed25519':"; ssh-add the same without the
    // word and the quotes, or with "(will confirm each use)" after the file.
    static const QRegularExpression passphrase(uR"(^Enter passphrase for (?:key )?'?(.+?)'?(?: \(.*\))?:$)"_s);
    if (const auto match = passphrase.match(trimmed); match.hasMatch())
        return {Prompt::Kind::KeyPassphrase, match.captured(1), trimmed};
    static const QRegularExpression sudo(uR"(^\[sudo\] password for .+:$)"_s);
    if (sudo.match(trimmed).hasMatch())
        return {Prompt::Kind::SudoPassword, {}, trimmed};
    if (trimmed.startsWith(u"Username"))
        return {Prompt::Kind::Username, {}, trimmed};
    return {Prompt::Kind::Password, {}, trimmed};
}

} // namespace askpass
