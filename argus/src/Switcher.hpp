#pragma once

#include "Picker.hpp"

#include <SessionConfig.hpp>

#include <QImage>
#include <QRect>

#include <map>
#include <vector>

namespace argus {

// The window switcher of Alt+Tab: a row of the windows in the middle of the screen, with
// their pictures or only their icons, and the name of the one picked below them.
class Switcher : public Picker {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.zskamljic.Argus.Switcher")

public:
    using Style = shell::SessionConfig::Switcher::Style;

    explicit Switcher(Toplevels& toplevels, QWidget* parent = nullptr);

    void setStyle(Style style);

public slots:
    Q_SCRIPTABLE void Show(bool backwards, bool sameApplication) { pick(backwards, sameApplication); }
    Q_SCRIPTABLE void Release() { release(); }

protected:
    void closing() override;
    void paintEvent(QPaintEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    struct Layout {
        QRect panel;
        std::vector<QRect> items;
        QRect title;
    };

    Layout layOut() const;

    Style m_style = Style::Previews;
    std::map<quint64, QImage> m_pictures; // the windows' pictures, scaled to their items
};

} // namespace argus
