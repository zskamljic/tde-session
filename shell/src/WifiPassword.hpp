#pragma once

#include <tde/Dialog.hpp>

#include <QBoxLayout>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

#include <optional>

namespace shell {

// Asks for the password of the Wi-Fi network `ssid`; nullopt when given up. Header-only, for
// the programs with widgets: the shell library has none.
inline std::optional<QString> askWifiPassword(QWidget* parent, const QString& ssid)
{
    using namespace Qt::StringLiterals;
    tde::Dialog dialog(u"Wi-Fi Password"_s, parent);
    auto* label = new QLabel(u"Type the password of “%1”:"_s.arg(ssid), &dialog);
    label->setTextFormat(Qt::PlainText);
    auto* field = new QLineEdit(&dialog);
    field->setEchoMode(QLineEdit::Password);
    auto* show = new QCheckBox(u"Show the password"_s, &dialog);
    QObject::connect(show, &QCheckBox::toggled, field,
        [field](bool on) { field->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password); });
    auto* buttons = new QHBoxLayout;
    auto* cancel = new QPushButton(u"Cancel"_s, &dialog);
    auto* connect = new QPushButton(u"Connect"_s, &dialog);
    // WPA passwords are 8 characters at least.
    connect->setEnabled(false);
    QObject::connect(field, &QLineEdit::textChanged, connect,
        [connect](const QString& text) { connect->setEnabled(text.size() >= 8); });
    buttons->addStretch(1);
    buttons->addWidget(cancel);
    buttons->addWidget(connect);
    dialog.contentLayout()->addWidget(label);
    dialog.contentLayout()->addWidget(field);
    dialog.contentLayout()->addWidget(show);
    dialog.contentLayout()->addLayout(buttons);
    dialog.setDefaultButton(connect);
    QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(connect, &QPushButton::clicked, &dialog, &QDialog::accept);
    field->setFocus();
    if (dialog.run() != QDialog::Accepted)
        return std::nullopt;
    return field->text();
}

} // namespace shell
