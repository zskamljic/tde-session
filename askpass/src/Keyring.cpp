#include "Keyring.hpp"

#include <libsecret/secret.h>

namespace askpass {
namespace {

const SecretSchema* schema()
{
    static const SecretSchema schema {
        "io.github.zskamljic.tde.SshKey",
        SECRET_SCHEMA_NONE,
        {
            {"key", SECRET_SCHEMA_ATTRIBUTE_STRING},
            {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING},
        },
        0,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
    };
    return &schema;
}

} // namespace

std::optional<std::string> keptPassphrase(const std::string& key)
{
    GError* error = nullptr;
    gchar* secret = secret_password_lookup_sync(schema(), nullptr, &error, "key", key.c_str(), nullptr);
    if (error)
        g_error_free(error);
    if (!secret)
        return std::nullopt;
    std::string passphrase = secret;
    secret_password_free(secret);
    return passphrase;
}

bool keepPassphrase(const std::string& key, const std::string& passphrase)
{
    GError* error = nullptr;
    const std::string label = "SSH key " + key;
    const bool kept = secret_password_store_sync(schema(), SECRET_COLLECTION_DEFAULT, label.c_str(), passphrase.c_str(),
        nullptr, &error, "key", key.c_str(), nullptr);
    if (error)
        g_error_free(error);
    return kept;
}

void forgetPassphrase(const std::string& key)
{
    GError* error = nullptr;
    secret_password_clear_sync(schema(), nullptr, &error, "key", key.c_str(), nullptr);
    if (error)
        g_error_free(error);
}

} // namespace askpass
