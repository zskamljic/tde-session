#include "BackgroundPage.hpp"

#include <DesktopSettings.hpp>

#include <tde/DesktopConfig.hpp>
#include <tde/Theme.hpp>

#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGuiApplication>
#include <QImageReader>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QPushButton>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>

using namespace Qt::StringLiterals;

namespace daedalus {
namespace {

using Mode = shell::SessionConfig::Background::Mode;

constexpr int Columns = 3;
constexpr int TileWidth = 192;
constexpr int PreviewWidth = 400;
constexpr int MaxPictures = 60;

const std::pair<Mode, const char*> modeNames[] = {
    {Mode::Fill, "Fill the screen"},
    {Mode::Fit, "Fit in the screen"},
    {Mode::Stretch, "Stretch"},
    {Mode::Center, "Centre"},
    {Mode::Tile, "Tile"},
};

// The screen's shape, which tiles and the preview take.
QSize screenSize()
{
    const QScreen* screen = QGuiApplication::primaryScreen();
    return screen && !screen->size().isEmpty() ? screen->size() : QSize(16, 9);
}

QSize ofScreenShape(int width)
{
    const QSize screen = screenSize();
    return {width, width * screen.height() / screen.width()};
}

// Where pictures the user adds are kept, as GNOME keeps them.
QString addedPictures()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + u"/backgrounds"_s;
}

// The pictures there are to pick: the user's own first, then those installed for it.
QStringList installedPictures()
{
    const QStringList pictureFormats = shell::pictureFilePatterns();
    QStringList folders {addedPictures()};
    for (const QString& data : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation)) {
        folders << data + u"/backgrounds"_s << data + u"/wallpapers"_s;
    }
    folders.removeDuplicates();
    QStringList pictures;
    for (const QString& folder : std::as_const(folders)) {
        QDirIterator it(
            folder, pictureFormats, QDir::Files, QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
        while (it.hasNext() && pictures.size() < MaxPictures)
            pictures << it.next();
    }
    return pictures;
}

QImage thumbnail(const QString& path, QSize size)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    // Only as much of a large picture as the tile shows is read.
    if (const QSize full = reader.size(); full.isValid())
        reader.setScaledSize(full.scaled(size, Qt::KeepAspectRatioByExpanding));
    QImage image = reader.read();
    if (image.isNull())
        return image;
    QRect crop(QPoint(), size);
    crop.moveCenter(image.rect().center());
    return image.copy(crop);
}

} // namespace

// PictureTile -----------------------------------------------------------------------------

PictureTile::PictureTile(const QString& path, bool removable, QWidget* parent)
    : QAbstractButton(parent)
    , m_path(path)
    , m_removable(removable)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setToolTip(path.isEmpty() ? u"No picture, only the colour"_s : QFileInfo(path).fileName());
    setFixedSize(sizeHint());
}

QSize PictureTile::sizeHint() const
{
    return ofScreenShape(TileWidth) + QSize(8, 8);
}

void PictureTile::setThumbnail(QImage thumbnail)
{
    m_thumbnail = std::move(thumbnail);
    update();
}

void PictureTile::setColor(const QColor& color)
{
    m_color = color;
    update();
}

QRect PictureTile::removeButton() const
{
    return {width() - 34, 10, 24, 24};
}

void PictureTile::paintEvent(QPaintEvent*)
{
    const auto& colors = tde::theme::colors();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    const QRectF picture = QRectF(rect()).adjusted(4, 4, -4, -4);
    QPainterPath shape;
    shape.addRoundedRect(picture, 8, 8);
    if (isChecked()) {
        painter.setPen(QPen(colors.accent, 3));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5), 11, 11);
    }
    painter.setClipPath(shape);
    if (m_path.isEmpty()) {
        painter.fillPath(shape, m_color);
        painter.setPen(colors.text);
        painter.drawText(picture, Qt::AlignCenter, u"No Picture"_s);
    } else if (m_thumbnail.isNull()) {
        painter.fillPath(shape, colors.base);
    } else {
        painter.drawImage(picture, m_thumbnail);
    }
    painter.setClipping(false);
    if (m_hovered && !isChecked())
        painter.fillPath(shape, QColor(255, 255, 255, 18));

    if (m_removable && m_hovered) {
        const QRectF button = removeButton();
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 160));
        painter.drawEllipse(button);
        painter.setPen(QPen(Qt::white, 1.5, Qt::SolidLine, Qt::RoundCap));
        const QPointF c = button.center();
        painter.drawLine(c + QPointF(-4, -4), c + QPointF(4, 4));
        painter.drawLine(c + QPointF(-4, 4), c + QPointF(4, -4));
    }
}

void PictureTile::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_removable && event->button() == Qt::LeftButton && removeButton().contains(event->position().toPoint())) {
        setDown(false);
        emit removeRequested();
        return;
    }
    QAbstractButton::mouseReleaseEvent(event);
}

void PictureTile::enterEvent(QEnterEvent* event)
{
    m_hovered = true;
    update();
    QAbstractButton::enterEvent(event);
}

void PictureTile::leaveEvent(QEvent* event)
{
    m_hovered = false;
    update();
    QAbstractButton::leaveEvent(event);
}

// BackgroundPage --------------------------------------------------------------------------

BackgroundPage::BackgroundPage(Settings& settings, QWidget* parent)
    : Page(parent)
    , m_settings(settings)
{
    const auto& background = m_settings.config.background;
    if (!background.image.isEmpty())
        m_picture = shell::loadBackgroundPicture(background.image, previewScale());

    // The look of the TDE applications and the shell; GTK programs follow between light and dark.
    auto* theme = new QComboBox(this);
    theme->addItem(u"Dark"_s, u"arc-dark"_s);
    theme->addItem(u"Light"_s, u"arc"_s);
    theme->addItem(u"Follow the System"_s, u"system"_s);
    theme->setCurrentIndex(std::max(0, theme->findData(tde::loadDesktopConfig().appearance.theme)));
    connect(theme, &QComboBox::activated, this, [this, theme] {
        const QString name = theme->currentData().toString();
        if (!shell::setDesktopString(tde::desktopConfigPath(), u"appearance"_s, u"theme"_s, name))
            qWarning("tde-daedalus: cannot write %s", qPrintable(tde::desktopConfigPath()));
        if (name != u"system")
            QProcess::startDetached(u"gsettings"_s,
                {u"set"_s, u"org.gnome.desktop.interface"_s, u"color-scheme"_s,
                    name == u"arc" ? u"default"_s : u"prefer-dark"_s});
    });
    Group* style = addGroup(u"Style"_s);
    style->addRow(u"Theme"_s, u"The colours of windows and of the desktop"_s, theme);

    m_preview = new QLabel(this);
    m_preview->setAlignment(Qt::AlignCenter);
    addWidget(m_preview, Qt::AlignHCenter);

    Group* pictures = addGroup(u"Pictures"_s);
    auto* add = new QPushButton(u"Add Picture…"_s, this);
    connect(add, &QPushButton::clicked, this, &BackgroundPage::addPicture);
    pictures->setHeaderWidget(add);
    auto* gridHolder = new QWidget(pictures);
    m_grid = new QGridLayout(gridHolder);
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setSpacing(6);
    pictures->layout()->addWidget(gridHolder, 0, Qt::AlignHCenter);

    m_mode = new QComboBox(this);
    for (const auto& [mode, name] : modeNames)
        m_mode->addItem(QString::fromUtf8(name), int(mode));
    m_mode->setCurrentIndex(m_mode->findData(int(background.mode)));
    connect(m_mode, &QComboBox::activated, this, [this] {
        m_settings.config.background.mode = Mode(m_mode->currentData().toInt());
        changed();
    });
    m_color = new QPushButton(this);
    m_color->setFixedSize(52, 30);
    m_color->setCursor(Qt::PointingHandCursor);
    connect(m_color, &QPushButton::clicked, this, [this] {
        const QColor color = QColorDialog::getColor(m_settings.config.background.color, this, u"Background Colour"_s);
        if (color.isValid()) {
            m_settings.config.background.color = color;
            changed();
        }
    });
    Group* look = addGroup(u"Background"_s);
    look->addRow(u"Placement"_s, u"How the picture covers the screen"_s, m_mode);
    look->addRow(u"Colour"_s, u"Around a picture that leaves room, or alone without one"_s, m_color);

    fillPictures();
    showSettings();
}

void BackgroundPage::fillPictures()
{
    for (PictureTile* tile : m_tiles)
        tile->deleteLater();
    m_tiles.clear();

    QStringList paths = installedPictures();
    const QString current = m_settings.config.background.image;
    if (!current.isEmpty() && !paths.contains(current))
        paths.prepend(current);
    paths.prepend(QString()); // the colour alone

    const QString added = addedPictures() + u'/';
    for (const QString& path : std::as_const(paths)) {
        auto* tile = new PictureTile(path, path.startsWith(added), m_grid->parentWidget());
        connect(tile, &PictureTile::clicked, this, [this, path] { pick(path); });
        connect(tile, &PictureTile::removeRequested, this, [this, path] { removePicture(path); });
        const int index = int(m_tiles.size());
        m_grid->addWidget(tile, index / Columns, index % Columns);
        m_tiles.push_back(tile);
    }
    // The thumbnails come one at a time, so the page shows at once.
    m_nextThumbnail = 0;
    QTimer::singleShot(0, this, &BackgroundPage::loadNextThumbnail);
}

void BackgroundPage::loadNextThumbnail()
{
    while (m_nextThumbnail < m_tiles.size() && m_tiles[m_nextThumbnail]->path().isEmpty())
        ++m_nextThumbnail;
    if (m_nextThumbnail >= m_tiles.size())
        return;
    PictureTile* tile = m_tiles[m_nextThumbnail++];
    // Read once, and kept for when the tiles are made again.
    QImage& small = m_thumbnails[tile->path()];
    if (small.isNull())
        small = thumbnail(tile->path(), ofScreenShape(TileWidth) * devicePixelRatioF());
    tile->setThumbnail(small);
    QTimer::singleShot(0, this, &BackgroundPage::loadNextThumbnail);
}

void BackgroundPage::addPicture()
{
    const QString chosen = QFileDialog::getOpenFileName(this, u"Add a Picture"_s,
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
        u"Pictures (%1)"_s.arg(shell::pictureFilePatterns().join(u' ')));
    if (chosen.isEmpty() || !QImageReader(chosen).canRead())
        return;
    // A copy is kept, so the background stays when the original moves.
    const QString folder = addedPictures();
    QDir().mkpath(folder);
    const QFileInfo info(chosen);
    QString copy = folder + u'/' + info.fileName();
    for (int n = 2; QFileInfo::exists(copy); ++n)
        copy = folder + u"/%1-%2.%3"_s.arg(info.completeBaseName()).arg(n).arg(info.suffix());
    if (!QFile::copy(chosen, copy)) {
        qWarning("tde-daedalus: cannot copy %s to %s", qPrintable(chosen), qPrintable(copy));
        return;
    }
    pick(copy);
    fillPictures();
}

void BackgroundPage::removePicture(const QString& path)
{
    QFile::remove(path);
    m_thumbnails.remove(path);
    if (m_settings.config.background.image == path)
        pick({});
    fillPictures();
}

void BackgroundPage::pick(const QString& path)
{
    QImage picture = path.isEmpty() ? QImage() : shell::loadBackgroundPicture(path, previewScale());
    if (!path.isEmpty() && picture.isNull())
        return;
    m_picture = std::move(picture);
    m_settings.config.background.image = path;
    changed();
}

// The preview is the screen made smaller: the picture is read as much smaller, so it is as
// large on it as it will be on the screen.
double BackgroundPage::previewScale() const
{
    return double(PreviewWidth) / screenSize().width() * devicePixelRatioF();
}

void BackgroundPage::changed()
{
    showSettings();
    m_settings.save();
}

void BackgroundPage::showSettings()
{
    const auto& background = m_settings.config.background;
    const QSize size = ofScreenShape(PreviewWidth);
    const qreal ratio = devicePixelRatioF();
    const QImage& picture = m_picture;
    QPixmap preview(size * ratio);
    preview.fill(Qt::transparent);
    {
        QPainter painter(&preview);
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath shape;
        shape.addRoundedRect(QRectF(QPointF(), size * ratio), 12 * ratio, 12 * ratio);
        painter.setClipPath(shape);
        painter.drawImage(0, 0, shell::drawBackground(background, picture, size * ratio));
    }
    preview.setDevicePixelRatio(ratio);
    m_preview->setPixmap(preview);

    for (PictureTile* tile : m_tiles) {
        tile->setChecked(tile->path() == background.image);
        if (tile->path().isEmpty())
            tile->setColor(background.color);
    }
    m_mode->setEnabled(!background.image.isEmpty());
    m_color->setStyleSheet(u"QPushButton { background: %1; border: 1px solid %2; border-radius: 8px; }"_s.arg(
        background.color.name(), tde::theme::colors().border.name()));
}

} // namespace daedalus
