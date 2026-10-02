#pragma once

#include <QObject>
#include <QString>

#include <memory>

struct pa_context;
struct pa_threaded_mainloop;

namespace hermes {

// The volume of the default output, through PulseAudio, as PipeWire offers it too. libpulse
// runs on its own thread; what it reports arrives here on the thread of the bar.
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
    void connected();
    void refresh();
    void query();
    void update(bool available, double volume, bool muted, const QString& output, const QString& sink, int channels);

    std::unique_ptr<Loop> m_loop;
    bool m_available = false;
    double m_volume = 0;
    bool m_muted = false;
    QString m_output;
    QString m_sink;
    int m_channels = 2;
};

} // namespace hermes
