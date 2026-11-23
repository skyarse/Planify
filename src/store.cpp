#include "store.h"

#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QUrl>
#include <QUuid>
#include <algorithm>
#include <stdexcept>

namespace {
QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

void failUnless(bool success, const QSqlError &error)
{
    if (!success)
        throw std::runtime_error(error.text().toStdString());
}

void run(QSqlQuery &query)
{
    const bool success = query.exec();
    failUnless(success, query.lastError());
}

QString iso(const QDate &date)
{
    return date.toString(Qt::ISODate);
}

QString normalizedTags(const QString &input)
{
    QStringList result;
    for (const auto &tag : input.split(',')) {
        const auto trimmed = tag.simplified();
        if (!trimmed.isEmpty() && !result.contains(trimmed, Qt::CaseInsensitive))
            result += trimmed;
    }
    return result.join(", ");
}

Project readProject(const QSqlQuery &q)
{
    return {q.value(0).toString(), q.value(1).toString(), q.value(2).toString(), q.value(3).toString()};
}
}

Store::Store(const QString &path) : connection(newId()), db(QSqlDatabase::addDatabase("QSQLITE", connection))
{
    try {
        db.setDatabaseName(path);
        db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=4000");
        const bool opened = db.open();
        failUnless(opened, db.lastError());
        execute("PRAGMA foreign_keys=ON");
        execute("PRAGMA journal_mode=WAL");
        QSqlQuery version(db);
        failUnless(version.exec("PRAGMA user_version"), version.lastError());
        if (!version.next())
            throw std::runtime_error("Не удалось определить версию базы");
        const int current = version.value(0).toInt();
        version.finish();
        if (current > 1)
            throw std::runtime_error("Эта база создана более новой версией приложения");
        if (current == 1)
            return;
        failUnless(db.transaction(), db.lastError());
        execute(R"SQL(
            CREATE TABLE IF NOT EXISTS projects (
                id TEXT PRIMARY KEY, title TEXT NOT NULL, account TEXT NOT NULL DEFAULT '', href TEXT NOT NULL DEFAULT ''
            )
        )SQL");
        execute(R"SQL(
            CREATE TABLE IF NOT EXISTS sections (
                id TEXT PRIMARY KEY, project TEXT NOT NULL REFERENCES projects(id), title TEXT NOT NULL
            )
        )SQL");
        execute(R"SQL(
            CREATE TABLE IF NOT EXISTS tasks (
                id TEXT PRIMARY KEY, project TEXT NOT NULL REFERENCES projects(id),
                section TEXT NOT NULL, parent TEXT NOT NULL, title TEXT NOT NULL, notes TEXT NOT NULL,
                tags TEXT NOT NULL, due TEXT NOT NULL, deadline TEXT NOT NULL, priority INTEGER NOT NULL,
                done INTEGER NOT NULL, pinned INTEGER NOT NULL, repeat TEXT NOT NULL,
                href TEXT NOT NULL, etag TEXT NOT NULL, ical BLOB NOT NULL,
                remote_uid TEXT NOT NULL, remote_parent_uid TEXT NOT NULL,
                title_norm TEXT NOT NULL, notes_norm TEXT NOT NULL, tags_norm TEXT NOT NULL
            )
        )SQL");
        execute("CREATE INDEX IF NOT EXISTS tasks_project_done_due ON tasks(project, done, due)");
        execute("CREATE INDEX IF NOT EXISTS tasks_done_due ON tasks(done, due)");
        execute("CREATE INDEX IF NOT EXISTS tasks_parent ON tasks(parent)");
        execute("INSERT OR IGNORE INTO projects(id,title) VALUES('inbox','Входящие')");
        execute("PRAGMA user_version=1");
        failUnless(db.commit(), db.lastError());
    } catch (...) {
        db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(connection);
        throw;
    }
}

Store::~Store()
{
    db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connection);
}

void Store::execute(const QString &sql)
{
    QSqlQuery q(db);
    const bool success = q.exec(sql);
    failUnless(success, q.lastError());
}

QString Store::inbox()
{
    return QStringLiteral("inbox");
}

QList<Project> Store::projects() const
{
    QSqlQuery q(db);
    const bool success = q.exec("SELECT id,title,account,href FROM projects WHERE id <> 'inbox' ORDER BY account,title COLLATE NOCASE");
    failUnless(success, q.lastError());
    QList<Project> result;
    while (q.next())
        result += readProject(q);
    return result;
}

Project Store::project(const QString &id) const
{
    QSqlQuery q(db);
    q.prepare("SELECT id,title,account,href FROM projects WHERE id=?");
    q.addBindValue(id);
    run(q);
    return q.next() ? readProject(q) : Project{};
}

QList<QPair<QString, QString>> Store::sections(const QString &projectId) const
{
    QSqlQuery q(db);
    q.prepare("SELECT id,title FROM sections WHERE project=? ORDER BY rowid");
    q.addBindValue(projectId);
    run(q);
    QList<QPair<QString, QString>> result;
    while (q.next())
        result.append({q.value(0).toString(), q.value(1).toString()});
    return result;
}

Task Store::task(const QString &id) const
{
    QSqlQuery q(db);
    q.prepare("SELECT * FROM tasks WHERE id=?");
    q.addBindValue(id);
    run(q);
    if (!q.next())
        return {};
    Task t;
    t.id = q.value(0).toString();
    t.project = q.value(1).toString();
    t.section = q.value(2).toString();
    t.parent = q.value(3).toString();
    t.title = q.value(4).toString();
    t.notes = q.value(5).toString();
    t.tags = q.value(6).toString();
    t.due = QDate::fromString(q.value(7).toString(), Qt::ISODate);
    t.deadline = QDate::fromString(q.value(8).toString(), Qt::ISODate);
    t.priority = q.value(9).toInt();
    t.done = q.value(10).toBool();
    t.pinned = q.value(11).toBool();
    t.repeat = q.value(12).toString();
    t.href = q.value(13).toString();
    t.etag = q.value(14).toString();
    t.calendarData = q.value(15).toByteArray();
    t.remoteUid = q.value(16).toString();
    t.remoteParentUid = q.value(17).toString();
    return t;
}

QStringList Store::labels() const
{
    QSqlQuery q(db);
    const bool success = q.exec("SELECT DISTINCT tags FROM tasks WHERE tags<>''");
    failUnless(success, q.lastError());
    QStringList result;
    while (q.next())
        for (const auto &tag : q.value(0).toString().split(", "))
            if (!result.contains(tag, Qt::CaseInsensitive))
                result += tag;
    result.sort(Qt::CaseInsensitive);
    return result;
}

QString Store::addProject(const QString &title)
{
    const QString clean = title.trimmed();
    if (clean.isEmpty())
        throw std::runtime_error("Введите название проекта");
    QSqlQuery q(db);
    const QString id = newId();
    q.prepare("INSERT INTO projects(id,title) VALUES(?,?)");
    q.addBindValue(id);
    q.addBindValue(clean);
    run(q);
    return id;
}

QString Store::addSection(const QString &projectId, const QString &title)
{
    const auto owner = project(projectId);
    if (owner.id.isEmpty() || owner.remote() || title.trimmed().isEmpty())
        throw std::runtime_error("Выберите локальный проект и введите название секции");
    const QString id = newId();
    QSqlQuery q(db);
    q.prepare("INSERT INTO sections VALUES(?,?,?)");
    q.addBindValue(id);
    q.addBindValue(projectId);
    q.addBindValue(title.trimmed());
    run(q);
    return id;
}

void Store::writeTask(const Task &t)
{
    QSqlQuery q(db);
    q.prepare(R"SQL(
        INSERT INTO tasks VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)
        ON CONFLICT(id) DO UPDATE SET
          project=excluded.project, section=excluded.section, parent=excluded.parent,
          title=excluded.title, notes=excluded.notes, tags=excluded.tags, due=excluded.due,
          deadline=excluded.deadline, priority=excluded.priority, done=excluded.done,
          pinned=excluded.pinned, repeat=excluded.repeat, href=excluded.href,
          etag=excluded.etag, ical=excluded.ical, remote_uid=excluded.remote_uid,
          remote_parent_uid=excluded.remote_parent_uid
    )SQL");
    const QVariantList fields = {t.id, t.project, t.section, t.parent, t.title, t.notes, t.tags,
        iso(t.due), iso(t.deadline), t.priority, t.done, t.pinned, t.repeat, t.href, t.etag,
        t.calendarData.isNull() ? QByteArray("") : t.calendarData, t.remoteUid, t.remoteParentUid,
        t.title.toCaseFolded(), t.notes.toCaseFolded(), t.tags.toCaseFolded()};
    for (const auto &value : fields)
        q.addBindValue(value.metaType().id() == QMetaType::QString && value.toString().isNull() ? QVariant(QStringLiteral("")) : value);
    run(q);
}

QString Store::saveTask(Task t)
{
    const auto owner = project(t.project);
    const auto previous = task(t.id);
    if (owner.id.isEmpty() || owner.remote() || previous.remote() || t.remote())
        throw std::runtime_error("Изменять можно только локальные задачи");
    t.title = t.title.trimmed();
    if (t.title.isEmpty())
        throw std::runtime_error("Введите название задачи");
    t.tags = normalizedTags(t.tags);
    if (t.id.isEmpty())
        t.id = newId();
    for (auto parentId = t.parent; !parentId.isEmpty();) {
        const auto parent = task(parentId);
        if (parent.id.isEmpty() || parent.project != t.project || parent.id == t.id)
            throw std::runtime_error("Недопустимая родительская задача");
        parentId = parent.parent;
    }
    if (previous.project != t.project && !previous.id.isEmpty()) {
        QSqlQuery children(db);
        children.prepare("SELECT 1 FROM tasks WHERE parent=? LIMIT 1");
        children.addBindValue(t.id);
        run(children);
        if (children.next())
            throw std::runtime_error("Сначала перенесите или отвяжите подзадачи");
    }
    if (!t.section.isEmpty()) {
        const auto options = sections(t.project);
        if (std::none_of(options.begin(), options.end(), [&](const auto &s) { return s.first == t.section; }))
            throw std::runtime_error("Секция принадлежит другому проекту");
    }
    writeTask(t);
    return t.id;
}

void Store::toggle(const QString &id)
{
    auto t = task(id);
    if (t.id.isEmpty())
        return;
    if (!t.done && t.due.isValid() && !t.repeat.isEmpty()) {
        if (t.repeat == "daily")
            t.due = t.due.addDays(1);
        else if (t.repeat == "weekly")
            t.due = t.due.addDays(7);
        else if (t.repeat == "monthly")
            t.due = t.due.addMonths(1);
        else
            t.done = true;
    } else {
        t.done = !t.done;
    }
    saveTask(t);
}

void Store::remove(const QString &id)
{
    if (task(id).remote())
        throw std::runtime_error("Удаление задач Baïkal в этой сборке отключено");
    failUnless(db.transaction(), db.lastError());
    try {
        QSqlQuery q(db);
        q.prepare("UPDATE tasks SET parent='' WHERE parent=? AND href=''");
        q.addBindValue(id);
        run(q);
        q.prepare("DELETE FROM tasks WHERE id=? AND href=''");
        q.addBindValue(id);
        run(q);
        failUnless(db.commit(), db.lastError());
    } catch (...) {
        db.rollback();
        throw;
    }
}

void Store::importReadOnly(const QList<RemoteList> &lists)
{
    failUnless(db.transaction(), db.lastError());
    try {
        for (const auto &list : lists) {
            QSqlQuery q(db);
            q.prepare(R"SQL(
                INSERT INTO projects(id,title,account,href) VALUES(?,?,?,?)
                ON CONFLICT(id) DO UPDATE SET title=excluded.title
                WHERE projects.account=excluded.account
            )SQL");
            q.addBindValue(list.project.id);
            q.addBindValue(list.project.title);
            q.addBindValue(list.project.account);
            q.addBindValue(list.project.href);
            run(q);

            QSet<QString> seen;
            for (const auto &t : list.tasks) {
                if (t.id.isEmpty() || t.project != list.project.id || !t.remote() || seen.contains(t.id))
                    throw std::runtime_error("Сервер вернул неоднозначный список задач");
                writeTask(t);
                seen.insert(t.id);
            }
            q.prepare("SELECT id FROM tasks WHERE project=? AND href<>''");
            q.addBindValue(list.project.id);
            run(q);
            QStringList missing;
            while (q.next())
                if (!seen.contains(q.value(0).toString()))
                    missing += q.value(0).toString();
            for (const auto &id : missing) {
                q.prepare("DELETE FROM tasks WHERE id=? AND project=? AND href<>''");
                q.addBindValue(id);
                q.addBindValue(list.project.id);
                run(q);
            }
        }
        failUnless(db.commit(), db.lastError());
    } catch (...) {
        db.rollback();
        throw;
    }
}

QSqlQuery Store::query(const Filter &f) const
{
    QStringList terms;
    QVariantList values;
    const QString today = iso(QDate::currentDate());
    if (!f.parent.isEmpty()) {
        terms += "t.parent=?";
        values << f.parent;
    } else if (f.search.isEmpty()) {
        switch (f.page) {
        case Page::Inbox:
            terms += "t.project='inbox' AND t.done=0";
            break;
        case Page::Today:
            terms += "t.done=0 AND ((t.due<>'' AND t.due<=?) OR (t.deadline<>'' AND t.deadline<=?))";
            values << today << today;
            break;
        case Page::Scheduled:
            terms += "t.done=0 AND (t.due<>'' OR t.deadline<>'')";
            break;
        case Page::Pins:
            terms += "t.done=0 AND t.pinned=1";
            break;
        case Page::Labels:
            terms += "t.done=0 AND t.tags<>''";
            if (!f.tag.isEmpty()) {
                terms += "instr(', ' || t.tags_norm || ', ', ', ' || ? || ', ') > 0";
                values << f.tag.toCaseFolded();
            }
            break;
        case Page::Completed:
            terms += "t.done=1";
            break;
        case Page::Project:
            terms += "t.done=0 AND t.project=?";
            values << f.project;
            break;
        }
    } else {
        terms += "(instr(t.title_norm,?) > 0 OR instr(t.notes_norm,?) > 0 OR instr(t.tags_norm,?) > 0)";
        const auto keyword = f.search.toCaseFolded();
        values << keyword << keyword << keyword;
    }
    if (f.section) {
        terms += "t.section=?";
        values << *f.section;
    }
    QSqlQuery q(db);
    q.prepare("SELECT t.id,t.title,t.done,t.priority,t.due,coalesce(s.title,p.title),t.parent,t.href,t.pinned,t.deadline,"
              " t.section,t.project FROM tasks AS t JOIN projects AS p ON p.id=t.project"
              " LEFT JOIN sections AS s ON s.id=t.section WHERE " + terms.join(" AND ") +
              " ORDER BY t.pinned DESC, coalesce(nullif(t.due,''),nullif(t.deadline,''),'9999-12-31'), t.rowid");
    for (const auto &v : values)
        q.addBindValue(v);
    run(q);
    return q;
}
