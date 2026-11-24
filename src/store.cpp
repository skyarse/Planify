#include "store.h"

#include <QSet>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTemporaryDir>
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
        db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=4000;QSQLITE_OPEN_URI");
        const bool opened = db.open();
        failUnless(opened, db.lastError());
        execute("PRAGMA foreign_keys=ON");
        execute("PRAGMA trusted_schema=OFF");
        execute("PRAGMA journal_mode=WAL");
        QSqlQuery version(db);
        failUnless(version.exec("PRAGMA user_version"), version.lastError());
        if (!version.next())
            throw std::runtime_error("Не удалось определить версию базы");
        const int current = version.value(0).toInt();
        version.finish();
        if (current > 2)
            throw std::runtime_error("Эта база создана более новой версией приложения");
        if (current == 2)
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
        execute("ALTER TABLE tasks ADD COLUMN completed TEXT NOT NULL DEFAULT ''");
        execute("ALTER TABLE tasks ADD COLUMN attachments TEXT NOT NULL DEFAULT '[]'");
        execute("ALTER TABLE tasks ADD COLUMN reminder TEXT NOT NULL DEFAULT ''");
        execute("ALTER TABLE tasks ADD COLUMN task_template INTEGER NOT NULL DEFAULT 0");
        execute("PRAGMA user_version=2");
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
    t.completed = QDateTime::fromString(q.value(21).toString(), Qt::ISODate);
    for (const auto &link : QJsonDocument::fromJson(q.value(22).toByteArray()).array())
        t.attachments.append(link.toString());
    t.reminder = QDateTime::fromString(q.value(23).toString(), Qt::ISODate);
    t.taskTemplate = q.value(24).toBool();
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
        INSERT INTO tasks VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)
        ON CONFLICT(id) DO UPDATE SET
          project=excluded.project, section=excluded.section, parent=excluded.parent,
          title=excluded.title, notes=excluded.notes, tags=excluded.tags, due=excluded.due,
          deadline=excluded.deadline, priority=excluded.priority, done=excluded.done,
          pinned=excluded.pinned, repeat=excluded.repeat, href=excluded.href,
          etag=excluded.etag, ical=excluded.ical, remote_uid=excluded.remote_uid,
          remote_parent_uid=excluded.remote_parent_uid,
          title_norm=excluded.title_norm, notes_norm=excluded.notes_norm, tags_norm=excluded.tags_norm,
          completed=CASE
            WHEN excluded.href<>'' AND excluded.completed='' THEN tasks.completed
            ELSE excluded.completed END,
          attachments=excluded.attachments,
          reminder=excluded.reminder, task_template=excluded.task_template
    )SQL");
    const QVariantList fields = {t.id, t.project, t.section, t.parent, t.title, t.notes, t.tags,
        iso(t.due), iso(t.deadline), t.priority, t.done, t.pinned, t.repeat, t.href, t.etag,
        t.calendarData.isNull() ? QByteArray("") : t.calendarData, t.remoteUid, t.remoteParentUid,
        t.title.toCaseFolded(), t.notes.toCaseFolded(), t.tags.toCaseFolded(),
        t.completed.toUTC().toString(Qt::ISODate),
        QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(t.attachments)).toJson(QJsonDocument::Compact)),
        t.reminder.toUTC().toString(Qt::ISODate), t.taskTemplate};
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
    if (!t.done)
        t.completed = QDateTime::currentDateTimeUtc();
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

void Store::insertRemote(const Task &created)
{
    const auto owner = project(created.project);
    if (!owner.remote() || !created.remote() || created.id.isEmpty() || created.remoteUid.isEmpty() ||
        !created.parent.isEmpty() || !created.remoteParentUid.isEmpty())
        throw std::runtime_error("Не удалось сохранить в локальный список новую задачу Baïkal");
    failUnless(db.transaction(), db.lastError());
    try {
        const auto existing = task(created.id);
        if (existing.id.isEmpty())
            writeTask(created);
        else if (existing.href != created.href || existing.remoteUid != created.remoteUid || existing.project != created.project)
            throw std::runtime_error("Локальные данные задачи Baïkal конфликтуют. Обновите список");
        failUnless(db.commit(), db.lastError());
    } catch (...) {
        db.rollback();
        throw;
    }
}

void Store::updateRemote(const Task &previous, Task updated)
{
    const auto cached = task(previous.id);
    if (!cached.remote() || cached.href != previous.href || cached.etag != previous.etag ||
        updated.id != cached.id || updated.project != cached.project || updated.href != cached.href ||
        updated.remoteUid != cached.remoteUid || updated.remoteParentUid != cached.remoteParentUid)
        throw std::runtime_error("Локальная копия изменилась. Обновите задачи Baïkal");
    if (!updated.completed.isValid())
        updated.completed = cached.completed;
    updated.parent = cached.parent;
    updated.section = cached.section;
    writeTask(updated);
}

QList<QPair<QString, QString>> Store::reminders(const QDateTime &from, const QDateTime &through) const
{
    QSqlQuery q(db);
    q.prepare("SELECT id,title FROM tasks WHERE done=0 AND reminder>? AND reminder<=? ORDER BY reminder");
    q.addBindValue(from.toUTC().toString(Qt::ISODate));
    q.addBindValue(through.toUTC().toString(Qt::ISODate));
    run(q);
    QList<QPair<QString, QString>> result;
    while (q.next())
        result.append({q.value(0).toString(), q.value(1).toString()});
    return result;
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
            for (auto t : list.tasks) {
                if (t.id.isEmpty() || t.project != list.project.id || !t.remote() || seen.contains(t.id))
                    throw std::runtime_error("Сервер вернул неоднозначный список задач");
                if (list.skipped && t.parent.isEmpty() && !t.remoteParentUid.isEmpty()
                    && t.remoteParentUid != t.remoteUid) {
                    q.prepare("SELECT id FROM tasks WHERE project=? AND remote_uid=? AND href<>'' LIMIT 2");
                    q.addBindValue(t.project);
                    q.addBindValue(t.remoteParentUid);
                    run(q);
                    if (q.next()) {
                        const auto parentId = q.value(0).toString();
                        if (!q.next())
                            t.parent = parentId;
                    }
                    q.finish();
                }
                writeTask(t);
                seen.insert(t.id);
            }
            if (list.skipped)
                continue;

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
            if (!f.showDone)
                terms += "t.done=0";
            terms += "((t.due<>'' AND t.due<=?) OR (t.deadline<>'' AND t.deadline<=?))";
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
    QString order = "coalesce(nullif(t.due,''),nullif(t.deadline,''),'9999-12-31')";
    if (f.sort == 1) order = "t.title_norm";
    if (f.sort == 2) order = "case when t.priority=0 then 4 else t.priority end";
    QSqlQuery q(db);
    q.prepare("SELECT t.id,t.title,t.done,t.priority,t.due,coalesce(s.title,p.title),t.parent,t.href,t.pinned,t.deadline,"
              " t.section,t.project FROM tasks AS t JOIN projects AS p ON p.id=t.project"
              " LEFT JOIN sections AS s ON s.id=t.section WHERE " + terms.join(" AND ") +
              " ORDER BY t.done ASC, t.pinned DESC, " + order + ", t.rowid");
    for (const auto &v : values)
        q.addBindValue(v);
    run(q);
    return q;
}

QHash<QString, int> Store::projectCounts() const
{
    QSqlQuery q(db);
    const bool ok = q.exec("SELECT project,count(*) FROM tasks WHERE done=0 GROUP BY project");
    failUnless(ok, q.lastError());
    QHash<QString,int> counts;
    while (q.next())
        counts.insert(q.value(0).toString(), q.value(1).toInt());
    return counts;
}

int Store::count(Page page, bool overdueOnly) const
{
    QString condition;
    switch (page) {
    case Page::Inbox: condition = "project='inbox' AND done=0"; break;
    case Page::Today:
        condition = overdueOnly
            ? "done=0 AND ((due<>'' AND due<?) OR (deadline<>'' AND deadline<?))"
            : "done=0 AND ((due<>'' AND due<=?) OR (deadline<>'' AND deadline<=?))";
        break;
    case Page::Pins: condition = "done=0 AND pinned=1"; break;
    case Page::Completed: condition = "done=1"; break;
    default: return 0;
    }
    QSqlQuery q(db);
    q.prepare("SELECT count(*) FROM tasks WHERE " + condition);
    if (page == Page::Today) {
        q.addBindValue(iso(QDate::currentDate()));
        q.addBindValue(iso(QDate::currentDate()));
    }
    run(q);
    return q.next() ? q.value(0).toInt() : 0;
}

void Store::backup(const QString &path)
{
    if (QFileInfo(path).absoluteFilePath() == QFileInfo(db.databaseName()).absoluteFilePath()
        || (!QFileInfo(path).canonicalFilePath().isEmpty()
            && QFileInfo(path).canonicalFilePath() == QFileInfo(db.databaseName()).canonicalFilePath()))
        throw std::runtime_error("Выберите отдельный файл для резервной копии");

    QTemporaryDir temp;
    if (!temp.isValid())
        throw std::runtime_error("Не удалось создать временный каталог");
    QFile snapshot(temp.filePath("backup.sqlite"));
    QSqlQuery q(db);
    q.prepare("VACUUM INTO ?");
    q.addBindValue(snapshot.fileName());
    run(q);
    QSaveFile output(path);
    if (!snapshot.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly))
        throw std::runtime_error("Не удалось открыть файл резервной копии");
    while (!snapshot.atEnd()) {
        const auto part = snapshot.read(65536);
        if (part.isEmpty() || output.write(part) != part.size())
            throw std::runtime_error("Не удалось записать резервную копию");
    }
    if (!output.commit())
        throw std::runtime_error("Не удалось сохранить резервную копию");
}

void Store::importBackup(const QString &path)
{
    const QFileInfo input(path);
    if (!input.isFile() || input.canonicalFilePath() == QFileInfo(db.databaseName()).canonicalFilePath())
        throw std::runtime_error("Выберите отдельный файл резервной копии");

    QSqlQuery attach(db);
    attach.prepare("ATTACH DATABASE ? AS snapshot");
    auto url = QUrl::fromLocalFile(input.canonicalFilePath());
    url.setQuery("mode=ro");
    attach.addBindValue(QString::fromUtf8(url.toEncoded()));
    run(attach);
    attach.finish();

    try {
        int version = 0;
        {
            QSqlQuery check(db);
            bool ok = check.exec("PRAGMA snapshot.user_version");
            failUnless(ok, check.lastError());
            if (!check.next() || ((version = check.value(0).toInt()) != 1 && version != 2))
                throw std::runtime_error("Формат резервной копии не поддерживается");
            ok = check.exec("PRAGMA snapshot.quick_check");
            failUnless(ok, check.lastError());
            if (!check.next() || check.value(0).toString() != "ok")
                throw std::runtime_error("Файл резервной копии повреждён");
            check.finish();
        }
        QList<QPair<QString, QString>> tables{
            {"projects","id,title,account,href"},
            {"sections","id,project,title"},
            {"tasks","id,project,section,parent,title,notes,tags,due,deadline,priority,done,pinned,repeat,href,etag,ical,remote_uid,remote_parent_uid,title_norm,notes_norm,tags_norm"},
        };
        if (version == 2)
            tables.last().second += ",completed,attachments,reminder,task_template";
        failUnless(db.transaction(), db.lastError());
        for (const auto &[name,columns] : tables) {
            QSqlQuery type(db);
            type.prepare("SELECT type FROM snapshot.sqlite_master WHERE name=?");
            type.addBindValue(name);
            run(type);
            if (!type.next() || type.value(0).toString() != "table")
                throw std::runtime_error("В резервной копии отсутствуют таблицы задач");
            type.finish();
            execute("INSERT INTO main." + name + "(" + columns + ") SELECT " + columns + " FROM snapshot." + name
                + " WHERE 1 ON CONFLICT(id) DO NOTHING");
        }
        failUnless(db.commit(), db.lastError());
    } catch (...) {
        db.rollback();
        execute("DETACH DATABASE snapshot");
        throw;
    }
    execute("DETACH DATABASE snapshot");
}
