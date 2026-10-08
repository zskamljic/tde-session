#pragma once

#include <optional>
#include <string>

namespace askpass {

// Passphrases of SSH keys kept in the Secret Service (gnome-keyring, KeePassXC and the like),
// by the key's file. Kept apart from the Qt code, as glib's headers and Qt's keywords clash.
std::optional<std::string> keptPassphrase(const std::string& key);
bool keepPassphrase(const std::string& key, const std::string& passphrase);
void forgetPassphrase(const std::string& key);

} // namespace askpass
