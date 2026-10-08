#pragma once

#include <QString>

namespace askpass {

// What a program asks through its askpass helper, read from the words it asks with.
struct Prompt {
    enum class Kind {
        KeyPassphrase, // ssh or ssh-add unlocking a key
        SudoPassword,
        Username, // seen as typed, as git asks for one with SSH_ASKPASS too
        Password, // anything else that wants a line typed
        Confirm, // a question answered yes or no, as ssh asks with SSH_ASKPASS_PROMPT=confirm
        Notice, // something to know, such as touching a security key, shown until done
    };

    Kind kind = Kind::Password;
    QString key; // the key's file, for a passphrase
    QString text; // what was asked, as asked

    bool operator==(const Prompt&) const = default;
};

// `text` as given on the command line, `sshPrompt` as SSH_ASKPASS_PROMPT says.
Prompt readPrompt(const QString& text, const QString& sshPrompt);

} // namespace askpass
