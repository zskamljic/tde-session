#pragma once

#include "Widgets.hpp"

#include <QObject>
#include <QString>

#include <memory>
#include <vector>

class QComboBox;
class QSlider;
class QToolButton;

namespace daedalus {

// An output or an input of the sound server.
struct SoundDevice {
    QString name; // the sound server's
    QString description; // for people
    double volume = 0; // from 0 to 1, as the server's 100%
    bool muted = false;
    int channels = 2;

    bool operator==(const SoundDevice&) const = default;
};

// The outputs and inputs, and which of each is used, through PulseAudio, as PipeWire offers it
// too. libpulse runs on its own thread; what it reports arrives on the window's.
class SoundDevices : public QObject {
    Q_OBJECT

public:
    explicit SoundDevices(QObject* parent = nullptr);
    ~SoundDevices() override;

    bool isAvailable() const { return m_available; }
    const std::vector<SoundDevice>& outputs() const { return m_outputs; }
    const std::vector<SoundDevice>& inputs() const { return m_inputs; }
    QString defaultOutput() const { return m_defaultOutput; }
    QString defaultInput() const { return m_defaultInput; }

    void setDefault(bool output, const QString& name);
    void setVolume(bool output, const SoundDevice& device, double volume);
    void setMuted(bool output, const QString& name, bool muted);

signals:
    void changed();

private:
    struct Loop;
    void connected();
    void refresh();
    void query();
    void received(bool output, std::vector<SoundDevice> devices);
    void receivedDefaults(const QString& output, const QString& input);

    std::unique_ptr<Loop> m_loop;
    bool m_available = false;
    std::vector<SoundDevice> m_outputs;
    std::vector<SoundDevice> m_inputs;
    QString m_defaultOutput;
    QString m_defaultInput;
};

// Which output and input are used, and how loud they are.
class SoundPage : public Page {
public:
    explicit SoundPage(QWidget* parent = nullptr);

private:
    struct Controls {
        QComboBox* device = nullptr;
        QSlider* volume = nullptr;
        QToolButton* mute = nullptr;
    };

    Controls addDevice(Group* group, bool output);
    void sync();
    void syncDevice(const Controls& controls, bool output);

    SoundDevices m_devices;
    Group* m_none = nullptr;
    Group* m_output = nullptr;
    Group* m_input = nullptr;
    Controls m_outputControls;
    Controls m_inputControls;
};

} // namespace daedalus
