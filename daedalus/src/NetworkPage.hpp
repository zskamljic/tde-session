#pragma once

#include "Widgets.hpp"

#include <Wifi.hpp>

#include <QTimer>

#include <vector>

namespace tde {
class Toast;
}

namespace daedalus {

// Cable connections, Wi-Fi on or off, the networks in range to connect to, and the networks
// kept, to forget. Networks are looked for again while the page shows.
class NetworkPage : public Page {
public:
    explicit NetworkPage(QWidget* parent = nullptr);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void sync();
    QWidget* networkControls(const shell::WifiNetwork& network);
    // In a dialog, to read or copy.
    void showPassword(const QString& ssid);

    shell::Wifi m_wifi;
    QTimer m_scanning;
    QWidget* m_content = nullptr;
    tde::Toast* m_toast = nullptr;
    // What the groups show, to make them again only when it changes.
    std::vector<shell::WifiNetwork> m_shownNetworks;
    std::vector<shell::WiredDevice> m_shownWired;
    bool m_shownEnabled = false;
    bool m_built = false;
};

} // namespace daedalus
