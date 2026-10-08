#include "SettingsWindow.hpp"

#include "BackgroundPage.hpp"
#include "BluetoothPage.hpp"
#include "DisplaysPage.hpp"
#include "LockPage.hpp"
#include "NetworkPage.hpp"
#include "SoundPage.hpp"

#include <tde/DesktopConfig.hpp>
#include <tde/FramelessHelper.hpp>
#include <tde/HeaderBar.hpp>
#include <tde/Theme.hpp>
#include <tde/WindowButtons.hpp>

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QStackedWidget>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace daedalus {
namespace {

QLabel* headerTitle(const QString& text, QWidget* parent)
{
    auto* title = new QLabel(text, parent);
    title->setObjectName(u"DialogTitle"_s); // bold, as dialogs have it
    return title;
}

} // namespace

SettingsWindow::SettingsWindow(QWidget* parent)
    : QWidget(parent)
{
    m_settings.config = shell::loadSessionConfig();
    // Written once the changes settle, as a value being typed or stepped through is one change.
    m_saving.setSingleShot(true);
    m_saving.setInterval(400);
    connect(&m_saving, &QTimer::timeout, this, &SettingsWindow::save);
    m_settings.save = [this] { m_saving.start(); };

    setObjectName(u"MainWindow"_s);
    setWindowTitle(u"Settings"_s);
    setAttribute(Qt::WA_StyledBackground);
    resize(940, 640);
    setMinimumSize(720, 480);
    new tde::FramelessHelper(this);

    // The pages on the left and the one shown on the right, each under a header bar with its
    // title; the window buttons go to the end the desktop config wants them.
    const auto& buttons = tde::desktop().windowButtons;
    auto* sidebar = new QWidget(this);
    sidebar->setObjectName(u"SidebarColumn"_s);
    sidebar->setAttribute(Qt::WA_StyledBackground);
    sidebar->setFixedWidth(240);
    auto* sidebarHeader = new tde::HeaderBar(sidebar);
    if (buttons.side == tde::ButtonSide::Left)
        sidebarHeader->contentLayout()->addWidget(new tde::WindowButtons(buttons.order, sidebarHeader));
    sidebarHeader->contentLayout()->addStretch(1);
    sidebarHeader->contentLayout()->addWidget(headerTitle(u"Settings"_s, sidebarHeader));
    sidebarHeader->contentLayout()->addStretch(1);
    m_pages = new QListWidget(sidebar);
    m_pages->setObjectName(u"Pages"_s);
    m_pages->setFrameShape(QFrame::NoFrame);
    m_pages->setIconSize(QSize(16, 16));
    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(0, 0, 0, 0);
    sidebarLayout->setSpacing(0);
    sidebarLayout->addWidget(sidebarHeader);
    sidebarLayout->addWidget(m_pages);

    auto* content = new QWidget(this);
    content->setObjectName(u"ContentColumn"_s);
    content->setAttribute(Qt::WA_StyledBackground);
    auto* contentHeader = new tde::HeaderBar(content);
    QLabel* title = headerTitle({}, contentHeader);
    contentHeader->contentLayout()->addStretch(1);
    contentHeader->contentLayout()->addWidget(title);
    contentHeader->contentLayout()->addStretch(1);
    if (buttons.side == tde::ButtonSide::Right)
        contentHeader->contentLayout()->addWidget(new tde::WindowButtons(buttons.order, contentHeader));
    m_stack = new QStackedWidget(content);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    contentLayout->addWidget(contentHeader);
    contentLayout->addWidget(m_stack);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(sidebar);
    layout->addWidget(content, 1);

    const auto addPage = [&](const QString& name, const QString& icon, const QString& title, QWidget* page) {
        new QListWidgetItem(tde::theme::symbolicIcon(icon), title, m_pages);
        m_stack->addWidget(page);
        m_names << name;
    };
    addPage(u"network"_s, u"network-wireless"_s, u"Network"_s, new NetworkPage(m_stack));
    addPage(u"bluetooth"_s, u"bluetooth-active"_s, u"Bluetooth"_s, new BluetoothPage(m_stack));
    addPage(
        u"background"_s, u"preferences-desktop-wallpaper"_s, u"Background"_s, new BackgroundPage(m_settings, m_stack));
    addPage(u"displays"_s, u"preferences-desktop-display"_s, u"Displays"_s, new DisplaysPage(m_settings, m_stack));
    addPage(u"sound"_s, u"audio-speakers"_s, u"Sound"_s, new SoundPage(m_stack));
    addPage(u"windows"_s, u"preferences-system-windows"_s, u"Windows"_s, new WindowsPage(m_settings, m_stack));
    addPage(u"lock"_s, u"system-lock-screen"_s, u"Lock Screen"_s, new LockPage(m_stack));
    addPage(u"datetime"_s, u"preferences-system-time"_s, u"Date & Time"_s, new DateTimePage(m_settings, m_stack));

    connect(m_pages, &QListWidget::currentRowChanged, this, [this, title](int row) {
        m_stack->setCurrentIndex(row);
        title->setText(m_pages->item(row)->text());
    });
    m_pages->setCurrentRow(0);
}

bool SettingsWindow::showPage(const QString& name)
{
    const qsizetype index = m_names.indexOf(name.toLower());
    if (index < 0)
        return false;
    m_pages->setCurrentRow(int(index));
    return true;
}

void SettingsWindow::ShowPage(const QString& name)
{
    showPage(name);
    show();
    raise();
    activateWindow();
}

SettingsWindow::~SettingsWindow()
{
    if (m_saving.isActive())
        save();
}

void SettingsWindow::save()
{
    m_saving.stop();
    if (!shell::saveSessionConfig(m_settings.config))
        qWarning("tde-daedalus: cannot write %s", qPrintable(shell::sessionConfigPath()));
}

} // namespace daedalus
