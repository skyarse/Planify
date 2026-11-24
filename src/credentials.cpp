#include <libsecret/secret.h>

#include "credentials.h"
#include <QCryptographicHash>

namespace {
const SecretSchema schema = {
    "local.plan.tasks.Baikal", SECRET_SCHEMA_NONE,
    {{"account", SECRET_SCHEMA_ATTRIBUTE_STRING}, {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING}},
};
QByteArray accountKey(const QUrl &address, const QString &user)
{
    return QCryptographicHash::hash(address.adjusted(QUrl::StripTrailingSlash).toEncoded() + '\0' + user.toUtf8(),
                                    QCryptographicHash::Sha256).toHex();
}
}

QString Credentials::lookup(const QUrl &address, const QString &user)
{
    const auto key = accountKey(address, user);
    GError *error = nullptr;
    auto *password = secret_password_lookup_sync(&schema, nullptr, &error, "account", key.constData(), nullptr);
    const auto result = QString::fromUtf8(password);
    secret_password_free(password);
    g_clear_error(&error);
    return result;
}

bool Credentials::store(const QUrl &address, const QString &user, const QString &password)
{
    const auto key = accountKey(address, user);
    auto secret = password.toUtf8();
    GError *error = nullptr;
    const bool saved = password.isEmpty()
        ? secret_password_clear_sync(&schema, nullptr, &error, "account", key.constData(), nullptr)
        : secret_password_store_sync(&schema, SECRET_COLLECTION_DEFAULT, "Plan Tasks — Baïkal", secret.constData(),
                                     nullptr, &error, "account", key.constData(), nullptr);
    secret.fill('\0');
    const bool success = saved || (password.isEmpty() && !error);
    g_clear_error(&error);
    return success;
}
