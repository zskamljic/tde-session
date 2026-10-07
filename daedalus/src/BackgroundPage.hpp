#pragma once

#include "Pages.hpp"

#include <QAbstractButton>
#include <QHash>
#include <QImage>

class QComboBox;
class QGridLayout;
class QLabel;
class QPushButton;

namespace daedalus {

// A picture to pick for the background, as a small rounded copy of it; those the user added
// can be removed again.
class PictureTile : public QAbstractButton {
    Q_OBJECT

public:
    // An empty path stands for the colour alone.
    PictureTile(const QString& path, bool removable, QWidget* parent = nullptr);

    QString path() const { return m_path; }
    void setThumbnail(QImage thumbnail);
    void setColor(const QColor& color);
    QSize sizeHint() const override;

signals:
    void removeRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QRect removeButton() const;

    QString m_path;
    bool m_removable;
    bool m_hovered = false;
    QImage m_thumbnail;
    QColor m_color;
};

// The desktop's background: a picture from those installed or added, or none, how it covers
// the screen, and the colour around it; with a preview of the outcome.
class BackgroundPage : public Page {
    Q_OBJECT

public:
    explicit BackgroundPage(Settings& settings, QWidget* parent = nullptr);

private:
    void fillPictures();
    void loadNextThumbnail();
    void addPicture();
    void removePicture(const QString& path);
    void pick(const QString& path);
    double previewScale() const;
    // Shows the settings as they are now; changed() also saves them.
    void showSettings();
    void changed();

    Settings& m_settings;
    QImage m_picture; // the one picked, as large as on the preview
    QHash<QString, QImage> m_thumbnails; // by path
    QLabel* m_preview = nullptr;
    QGridLayout* m_grid = nullptr;
    std::vector<PictureTile*> m_tiles;
    size_t m_nextThumbnail = 0;
    QComboBox* m_mode = nullptr;
    QPushButton* m_color = nullptr;
};

} // namespace daedalus
