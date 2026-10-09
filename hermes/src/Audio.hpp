#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

#include <memory>

struct pa_context;
struct pa_threaded_mainloop;

namespace hermes {

// The volume of the default output, through PulseAudio, as PipeWire offers it too. libpulse
// runs on its own thread; what it reports arrives here on the thread of the bar. Bluetooth
// headphones left in their headset mode, as for a call, go back to playing music in full
// quality once nothing records from their microphone.
class Audio : public QObject {
    Q_OBJECT

public:
    explicit Audio(QObject* parent = nullptr);
    ~Audio() override;

    bool isAvailable() const { return m_available; }
    // From 0 to 1, as the sound server's 100%.
    double volume() const { return m_volume; }
    bool isMuted() const { return m_muted; }
    QString output() const { return m_output; } // what the default output is called

    void setVolume(double volume);
    void setMuted(bool muted);

signals:
    void changed();

private:
    struct Loop;
    struct Survey;
    void connected();
    void refresh();
    void query();
    // Looks at the Bluetooth cards and who records from them, then keeps them in full quality.
    void surveyBluetooth();
    void keepQuality(const Survey& survey);
    void update(bool available, double volume, bool muted, const QString& output, const QString& sink, int channels);

    std::unique_ptr<Loop> m_loop;
    QTimer m_bluetoothCheck; // a moment after cards or recordings changed
    bool m_available = false;
    double m_volume = 0;
    bool m_muted = false;
    QString m_output;
    QString m_sink;
    int m_channels = 2;
};

} // namespace hermes
