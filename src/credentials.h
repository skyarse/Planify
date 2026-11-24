#pragma once

#include <QString>
#include <QUrl>

namespace Credentials {
QString lookup(const QUrl &address, const QString &user);
bool store(const QUrl &address, const QString &user, const QString &password);
}
