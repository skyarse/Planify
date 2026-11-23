#pragma once

#include "store.h"

#include <QNetworkAccessManager>
#include <QObject>

class CalDav : public QObject {
    Q_OBJECT

public:
    explicit CalDav(QObject *parent = nullptr);
    bool start(const QUrl &address, const QString &username, const QString &password);

signals:
    void imported(const QList<RemoteList> &lists);
    void failed(const QString &reason);
    void progress(const QString &message);

private:
    enum class Step { Principal, Home, Collections, Tasks };

    QNetworkAccessManager network;
    QUrl origin;
    QString username;
    QString password;
    bool active = false;
    int collection = 0;
    QList<RemoteList> lists;

    void request(Step step, const QUrl &url);
    void parse(Step step, const QUrl &url, const QByteArray &data);
    void finish(const QString &error = {});
    void nextCollection();
};
