#pragma once

#include <QAbstractButton>

namespace hermes {

// The button at the start of the bar, in the hot corner, as Windows 7 had one at the end of
// its taskbar: a narrow strip, lit under the pointer, that does what pushing the pointer into
// the corner does.
class CornerButton : public QAbstractButton {
    Q_OBJECT

public:
    explicit CornerButton(QWidget* parent = nullptr);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
};

} // namespace hermes
