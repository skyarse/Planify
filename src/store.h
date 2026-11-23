#pragma once

#include <QDate>
#include <QHash>
#include <QList>
#include <QUrl>
#include <QSqlDatabase>
#include <QString>
#include <optional>

struct Project {
    QString id;
    QString title;
    QString account;
    QString href;
    bool remote() const { return !account.isEmpty(); }
};

struct Task {
    QString id;
    QString project;
    QString section;
    QString parent;
    QString title;
    QString notes;
    QString tags;
    QDate due;
    QDate deadline;
    int priority = 0;
    bool done = false;
    bool pinned = false;
    QString repeat;
    QString href;
    QString etag;
    QByteArray calendarData;
    QString remoteUid;
    QString remoteParentUid;
    bool remote() const { return !href.isEmpty(); }
};

struct RemoteList {
    Project project;
    QList<Task> tasks;
    qsizetype skipped = 0;
    qsizetype recovered = 0;
};

enum class Page { Inbox, Today, Scheduled, Pins, Labels, Completed, Project };

struct Filter {
    Page page = Page::Inbox;
    QString project;
    QString search;
    QString tag;
    std::optional<QString> section;
    QString parent;
    int sort = 0;
    bool showDone = false;
};

class QSqlQuery;
class Store {
public:
    explicit Store(const QString &path);
    ~Store();
    Store(const Store &) = delete;
    Store &operator=(const Store &) = delete;

    static QString inbox();
    QList<Project> projects() const;
    QList<QPair<QString, QString>> sections(const QString &project) const;
    Project project(const QString &id) const;
    Task task(const QString &id) const;
    QStringList labels() const;
    QHash<QString, int> projectCounts() const;
    int count(Page page, bool overdueOnly = false) const;
    void backup(const QString &path);
    void importBackup(const QString &path);
    QString addProject(const QString &title);
    QString addSection(const QString &project, const QString &title);
    QString saveTask(Task task);
    void toggle(const QString &id);
    void remove(const QString &id);
    void importReadOnly(const QList<RemoteList> &lists);
    QSqlQuery query(const Filter &filter) const;

private:
    QString connection;
    QSqlDatabase db;
    void execute(const QString &sql);
    void writeTask(const Task &task);
};
