#pragma once

#include <QIcon>
#include <QTimer>
#include <QWidget>

#include <memory>

namespace hermes {

// Where the bar is found on the session bus.
inline constexpr char BusService[] = "io.github.zskamljic.Hermes";
inline constexpr char BusPath[] = "/io/github/zskamljic/Hermes";

class Audio;
class Brightness;

// What the volume and brightness keys changed, shown for a moment near the bottom of the
// screen: an icon and how far the level goes.
class Osd : public QWidget {
    Q_OBJECT

public:
    explicit Osd(QWidget* parent = nullptr);

    // Shows `level`, from 0 to 1, next to the icon called `iconName`.
    void present(const QString& iconName, double level);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QIcon m_icon;
    double m_level = 0;
    QTimer m_hide;
};

// The volume and brightness keys, which the compositor hands to the bar over the session bus,
// so the change shows on the screen. Its scriptable slots are the D-Bus methods.
class MediaKeys : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.zskamljic.Hermes")

public:
    MediaKeys(Audio& audio, Brightness& brightness, QObject* parent = nullptr);
    ~MediaKeys() override;

    Osd& osd() const { return *m_osd; }

public slots:
    Q_SCRIPTABLE void RaiseVolume();
    Q_SCRIPTABLE void LowerVolume();
    Q_SCRIPTABLE void ToggleMute();
    Q_SCRIPTABLE void RaiseBrightness();
    Q_SCRIPTABLE void LowerBrightness();

private:
    void changeVolume(double by);
    void changeBrightness(double by);

    Audio& m_audio;
    Brightness& m_brightness;
    std::unique_ptr<Osd> m_osd; // a window of its own
};

} // namespace hermes
