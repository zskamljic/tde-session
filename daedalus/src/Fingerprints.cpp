#include "Fingerprints.hpp"

#include <tde/Dialog.hpp>

#include <QComboBox>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusVariant>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace daedalus {
namespace {

const QString Service = u"net.reactivated.Fprint"_s;
const QString DeviceInterface = u"net.reactivated.Fprint.Device"_s;

const QStringList Fingers {
    u"right-index-finger"_s,
    u"left-index-finger"_s,
    u"right-thumb"_s,
    u"left-thumb"_s,
    u"right-middle-finger"_s,
    u"left-middle-finger"_s,
    u"right-ring-finger"_s,
    u"left-ring-finger"_s,
    u"right-little-finger"_s,
    u"left-little-finger"_s,
};

} // namespace

QString fingerName(const QString& finger)
{
    QString name = finger;
    name.replace(u'-', u' ');
    if (!name.isEmpty())
        name[0] = name[0].toUpper();
    return name;
}

// FingerprintReader -----------------------------------------------------------------------

FingerprintReader::FingerprintReader(QObject* parent)
    : QObject(parent)
{
    const QDBusReply<QDBusObjectPath> device = QDBusConnection::systemBus().call(QDBusMessage::createMethodCall(
        Service, u"/net/reactivated/Fprint/Manager"_s, u"net.reactivated.Fprint.Manager"_s, u"GetDefaultDevice"_s));
    if (!device.isValid())
        return;
    m_device = device.value().path();
    QDBusConnection::systemBus().connect(
        Service, m_device, DeviceInterface, u"EnrollStatus"_s, this, SLOT(onEnrollStatus(QString, bool)));
}

QString FingerprintReader::call(const QString& method, const QVariantList& arguments)
{
    QDBusMessage message = QDBusMessage::createMethodCall(Service, m_device, DeviceInterface, method);
    message.setArguments(arguments);
    // The system may ask for the user's password first; long enough to type it.
    message.setInteractiveAuthorizationAllowed(true);
    const QDBusMessage reply = QDBusConnection::systemBus().call(message, QDBus::Block, 120000);
    return reply.type() == QDBusMessage::ErrorMessage ? reply.errorMessage() : QString();
}

QStringList FingerprintReader::enrolled() const
{
    if (!isAvailable())
        return {};
    QDBusMessage message = QDBusMessage::createMethodCall(Service, m_device, DeviceInterface, u"ListEnrolledFingers"_s);
    message << QString();
    const QDBusReply<QStringList> fingers = QDBusConnection::systemBus().call(message);
    return fingers.isValid() ? fingers.value() : QStringList();
}

int FingerprintReader::stages() const
{
    QDBusMessage message
        = QDBusMessage::createMethodCall(Service, m_device, u"org.freedesktop.DBus.Properties"_s, u"Get"_s);
    message << DeviceInterface << u"num-enroll-stages"_s;
    const QDBusReply<QDBusVariant> stages = QDBusConnection::systemBus().call(message);
    return stages.isValid() ? std::max(0, stages.value().variant().toInt()) : 0;
}

QString FingerprintReader::startEnrolling(const QString& finger)
{
    if (QString error = call(u"Claim"_s, {QString()}); !error.isEmpty())
        return error;
    if (QString error = call(u"EnrollStart"_s, {finger}); !error.isEmpty()) {
        call(u"Release"_s);
        return error;
    }
    m_enrolling = true;
    return {};
}

void FingerprintReader::stopEnrolling()
{
    if (!m_enrolling)
        return;
    m_enrolling = false;
    call(u"EnrollStop"_s);
    call(u"Release"_s);
}

QString FingerprintReader::removeAll()
{
    if (QString error = call(u"Claim"_s, {QString()}); !error.isEmpty())
        return error;
    const QString error = call(u"DeleteEnrolledFingers2"_s);
    call(u"Release"_s);
    return error;
}

void FingerprintReader::onEnrollStatus(const QString& result, bool done)
{
    if (!m_enrolling)
        return;
    if (done)
        stopEnrolling();
    emit enrollStatus(result, done);
}

// FingerprintGroup ------------------------------------------------------------------------

FingerprintGroup::FingerprintGroup(QWidget* parent)
    : Group(u"Fingerprint"_s, parent)
{
    if (!m_reader.isAvailable())
        return;
    auto* controls = new QWidget(this);
    auto* row = new QHBoxLayout(controls);
    row->setContentsMargins(0, 0, 0, 0);
    auto* add = new QPushButton(u"Add…"_s, controls);
    m_remove = new QPushButton(u"Remove All"_s, controls);
    connect(add, &QPushButton::clicked, this, &FingerprintGroup::add);
    connect(m_remove, &QPushButton::clicked, this, &FingerprintGroup::removeAll);
    row->addWidget(add);
    row->addWidget(m_remove);
    addRow(u"Fingerprints"_s, u"Unlock the screen with a finger, as well as with the password"_s, controls);
    m_state = new QLabel(this);
    m_state->setWordWrap(true);
    m_state->setForegroundRole(QPalette::PlaceholderText);
    layout()->addWidget(m_state);
    sync();
}

void FingerprintGroup::sync()
{
    QStringList names;
    for (const QString& finger : m_reader.enrolled())
        names << fingerName(finger);
    m_state->setText(names.isEmpty() ? u"No fingers added yet."_s : u"Added: %1."_s.arg(names.join(u", "_s)));
    m_remove->setEnabled(!names.isEmpty());
}

void FingerprintGroup::add()
{
    const QStringList enrolled = m_reader.enrolled();
    tde::Dialog dialog(u"Add a Fingerprint"_s, window());
    auto* finger = new QComboBox(&dialog);
    for (const QString& name : Fingers) {
        if (!enrolled.contains(name))
            finger->addItem(fingerName(name), name);
    }
    auto* hint = new QLabel(u"Pick the finger, then touch the reader with it again and again, lifting it in "
                            "between, until it has been read all over."_s,
        &dialog);
    hint->setWordWrap(true);
    auto* progress = new QProgressBar(&dialog);
    const int stages = m_reader.stages();
    progress->setRange(0, std::max(stages, 1));
    progress->setValue(0);
    progress->setTextVisible(false);
    progress->hide();
    auto* status = new QLabel(&dialog);
    status->setWordWrap(true);
    auto* buttons = new QHBoxLayout;
    auto* cancel = new QPushButton(u"Cancel"_s, &dialog);
    auto* start = new QPushButton(u"Start"_s, &dialog);
    buttons->addStretch(1);
    buttons->addWidget(cancel);
    buttons->addWidget(start);
    dialog.contentLayout()->addWidget(finger);
    dialog.contentLayout()->addWidget(hint);
    dialog.contentLayout()->addWidget(progress);
    dialog.contentLayout()->addWidget(status);
    dialog.contentLayout()->addLayout(buttons);
    dialog.setDefaultButton(start);

    bool completed = false;
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(start, &QPushButton::clicked, &dialog, [&] {
        if (completed) {
            dialog.accept();
            return;
        }
        if (const QString error = m_reader.startEnrolling(finger->currentData().toString()); !error.isEmpty()) {
            status->setText(u"The reader could not be used: %1"_s.arg(error));
            return;
        }
        finger->setEnabled(false);
        start->setEnabled(false);
        progress->show();
        status->setText(u"Touch the reader."_s);
    });
    connect(&m_reader, &FingerprintReader::enrollStatus, &dialog, [&](const QString& result, bool done) {
        if (result == u"enroll-stage-passed") {
            progress->setValue(std::min(progress->value() + 1, progress->maximum()));
            status->setText(u"Lift the finger, and touch the reader again."_s);
        } else if (result == u"enroll-completed") {
            progress->setValue(progress->maximum());
            status->setText(u"The fingerprint was added."_s);
            completed = true;
        } else if (result == u"enroll-retry-scan" || result == u"enroll-swipe-too-short"
            || result == u"enroll-finger-not-centered" || result == u"enroll-remove-and-retry") {
            status->setText(u"That did not read well: touch the reader again, with the finger flat on it."_s);
        } else if (result == u"enroll-duplicate") {
            status->setText(u"That finger was added already."_s);
        } else if (result == u"enroll-data-full") {
            status->setText(u"The reader has no room for more fingerprints."_s);
        } else if (done) {
            status->setText(u"Adding the fingerprint failed."_s);
        }
        if (done) {
            start->setText(completed ? u"Done"_s : u"Start"_s);
            start->setEnabled(true);
            finger->setEnabled(!completed);
            cancel->setVisible(!completed);
            if (!completed)
                progress->setValue(0);
        }
    });
    dialog.run();
    m_reader.stopEnrolling();
    sync();
}

void FingerprintGroup::removeAll()
{
    if (!tde::Dialog::confirm(window(), u"Remove Fingerprints"_s, u"Remove all your fingerprints?"_s,
            u"The screen then unlocks with the password alone, until fingers are added again."_s, u"Remove"_s))
        return;
    if (const QString error = m_reader.removeAll(); !error.isEmpty())
        qWarning("tde-daedalus: the fingerprints were not removed: %s", qPrintable(error));
    sync();
}

} // namespace daedalus
