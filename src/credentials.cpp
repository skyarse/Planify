#include <libsecret/secret.h>

#include "credentials.h"
#include <QCryptographicHash>

namespace {
const SecretSchema schema = {
    "local.plan.tasks.Baikal", SECRET_SCHEMA_NONE,
    {{"account", SECRET_SCHEMA_ATTRIBUTE_STRING}, {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING}},
};
const char label[] = "Plan Tasks — Baïkal";

QByteArray accountKey(const QUrl &address, const QString &user)
{
    return QCryptographicHash::hash(address.adjusted(QUrl::StripTrailingSlash).toEncoded() + '\0' + user.toUtf8(),
                                    QCryptographicHash::Sha256).toHex();
}

GHashTable *accountAttributes(const QByteArray &key)
{
    return secret_attributes_build(&schema, "account", key.constData(), nullptr);
}

SecretService *directService()
{
    return secret_service_open_sync(SECRET_TYPE_SERVICE, "org.freedesktop.secrets",
                                    SECRET_SERVICE_OPEN_SESSION, nullptr, nullptr);
}
}

Credentials::Lookup Credentials::lookup(const QUrl &address, const QString &user)
{
    const auto key = accountKey(address, user);
    GError *error = nullptr;
    auto *password = secret_password_lookup_sync(&schema, nullptr, &error, "account", key.constData(), nullptr);
    const bool defaultAvailable = !error;
    g_clear_error(&error);
    if (password) {
        const auto result = QString::fromUtf8(password);
        secret_password_free(password);
        return {result, true};
    }
    g_autoptr(SecretService) service = directService();
    if (!service)
        return {{}, defaultAvailable};
    g_autoptr(GHashTable) attributes = accountAttributes(key);
    g_autoptr(SecretValue) value = secret_service_lookup_sync(service, &schema, attributes, nullptr, &error);
    const bool available = defaultAvailable || !error;
    g_clear_error(&error);
    return {value ? QString::fromUtf8(secret_value_get_text(value)) : QString(), available};
}

bool Credentials::store(const QUrl &address, const QString &user, const QString &password)
{
    const auto key = accountKey(address, user);
    auto bytes = password.toUtf8();
    GError *error = nullptr;
    const bool portalSaved = password.isEmpty()
        ? secret_password_clear_sync(&schema, nullptr, &error, "account", key.constData(), nullptr)
        : secret_password_store_sync(&schema, SECRET_COLLECTION_DEFAULT, label, bytes.constData(),
                                     nullptr, &error, "account", key.constData(), nullptr);
    const bool defaultAvailable = !error;
    g_clear_error(&error);
    if (portalSaved && !password.isEmpty()) {
        bytes.fill('\0');
        return true;
    }
    // In a Flatpak, the default backend can fail even when the desktop Secret Service is reachable.
    g_autoptr(SecretService) service = directService();
    if (password.isEmpty()) {
        if (!service)
            return defaultAvailable;
        g_autoptr(GHashTable) attributes = accountAttributes(key);
        secret_service_clear_sync(service, &schema, attributes, nullptr, &error);
        const bool success = !error;
        g_clear_error(&error);
        return success;
    }
    g_autoptr(SecretValue) value = secret_value_new(bytes.constData(), bytes.size(), "text/plain");
    bytes.fill('\0');
    if (!service)
        return false;
    g_autoptr(GHashTable) attributes = accountAttributes(key);
    return secret_service_store_sync(service, &schema, attributes, SECRET_COLLECTION_DEFAULT, label,
                                     value, nullptr, nullptr);
}
