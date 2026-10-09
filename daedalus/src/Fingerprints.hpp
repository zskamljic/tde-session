#pragma once

#include "Widgets.hpp"

#include <QObject>
#include <QStringList>

class QLabel;
class QPushButton;

namespace daedalus {

// The fingerprint reader, through fprintd: the user's fingers enrolled on it, adding them and
// removing them. The reader is held only while a finger is added or they are removed.
class FingerprintReader : public QObject {
    Q_OBJECT

public:
    explicit FingerprintReader(QObject* parent = nullptr);

    bool isAvailable() const { return !m_device.isEmpty(); }
    // As fprintd names them: "right-index-finger" and the like.
    QStringList enrolled() const;
    // How many touches adding a finger takes; 0 when not known.
    int stages() const;

    // Starts adding `finger`; enrollStatus() follows. Returns an error, or nothing.
    QString startEnrolling(const QString& finger);
    void stopEnrolling();
    QString removeAll();

signals:
    // fprintd's word on adding a finger, "enroll-stage-passed" to "enroll-completed";
    // `done` when it is over either way.
    void enrollStatus(const QString& result, bool done);

private slots:
    void onEnrollStatus(const QString& result, bool done);

private:
    QString call(const QString& method, const QVariantList& arguments = {});

    QString m_device; // its object path
    bool m_enrolling = false;
};

// What a finger of fprintd's names is called: "Right index finger".
QString fingerName(const QString& finger);

// The fingers that unlock the screen, in a group of the lock screen's page; nothing without a
// reader.
class FingerprintGroup : public Group {
public:
    explicit FingerprintGroup(QWidget* parent = nullptr);

    bool isAvailable() const { return m_reader.isAvailable(); }

private:
    void sync();
    void add();
    void removeAll();

    FingerprintReader m_reader;
    QLabel* m_state = nullptr;
    QPushButton* m_remove = nullptr;
};

} // namespace daedalus
