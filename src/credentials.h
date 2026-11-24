#pragma once

#include <QString>
#include <QUrl>

namespace Credentials {
struct Lookup {
    QString password;
    bool available;
};
Lookup lookup(const QUrl &address, const QString &user);
bool store(const QUrl &address, const QString &user, const QString &password);
}
