#include <Prompt.hpp>

#include <QTest>

using namespace Qt::StringLiterals;
using askpass::Prompt;

class TestPrompt : public QObject {
    Q_OBJECT

private slots:
    void readsKeyPassphrases()
    {
        QCOMPARE(askpass::readPrompt(u"Enter passphrase for key '/home/me/.ssh/id_ed25519': "_s, {}),
            (Prompt {Prompt::Kind::KeyPassphrase, u"/home/me/.ssh/id_ed25519"_s,
                u"Enter passphrase for key '/home/me/.ssh/id_ed25519':"_s}));
        QCOMPARE(
            askpass::readPrompt(u"Enter passphrase for /home/me/.ssh/id_rsa: "_s, {}).key, u"/home/me/.ssh/id_rsa"_s);
        QCOMPARE(askpass::readPrompt(u"Enter passphrase for /home/me/my key (will confirm each use): "_s, {}).key,
            u"/home/me/my key"_s);
    }

    void readsSudo()
    {
        QCOMPARE(askpass::readPrompt(u"[sudo] password for me: "_s, {}).kind, Prompt::Kind::SudoPassword);
    }

    void readsTheRest()
    {
        QCOMPARE(askpass::readPrompt(u"Password: "_s, {}).kind, Prompt::Kind::Password);
        QCOMPARE(askpass::readPrompt(u"Username for 'https://github.com': "_s, {}).kind, Prompt::Kind::Username);
        QCOMPARE(askpass::readPrompt(u"Allow use of key?"_s, u"confirm"_s).kind, Prompt::Kind::Confirm);
        QCOMPARE(askpass::readPrompt(u"Confirm user presence for key"_s, u"none"_s).kind, Prompt::Kind::Notice);
    }
};

QTEST_GUILESS_MAIN(TestPrompt)
#include "tst_prompt.moc"
