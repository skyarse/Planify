#pragma once

#include "store.h"

#include <QNetworkAccessManager>
#include <QObject>

class CalDav : public QObject {
    Q_OBJECT

public:
    explicit CalDav(QObject *parent = nullptr);
    bool connected(const Project &project) const;
    bool connectAccount(const QUrl &address, const QString &username, const QString &password);
    bool create(const Project &project, const Task &draft, const QString &token);
    bool update(const Project &project, const Task &previous, const Task &draft);
    static void populateDetails(Task &task);
    static QByteArray exportTask(const Task &task);
    bool refreshAccount();
    bool start(const QUrl &address, const QString &username, const QString &password);

signals:
    void imported(const QList<RemoteList> &lists);
    void created(const Task &task);
    void updated(const Task &previous, const Task &task);
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

    void requestWrite(const Project &project, const QUrl &url, const Task &expected, const QByteArray &body,
                      bool verifyRetry = false);
    void request(Step step, const QUrl &url);
    void parse(Step step, const QUrl &url, const QByteArray &data);
    void finish(const QString &error = {});
    void nextCollection();
};
