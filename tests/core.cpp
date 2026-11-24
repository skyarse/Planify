#include "caldav.h"
#include "store.h"

#include <QCoreApplication>
#include <QFile>
#include <QDebug>
#include <QEventLoop>
#include <QSqlQuery>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QTimeZone>
#include <algorithm>
#include <stdexcept>

namespace {
void expect(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

QByteArray multistatus(const QByteArray &content)
{
    return "<d:multistatus xmlns:d='DAV:' xmlns:c='urn:ietf:params:xml:ns:caldav'>" +
        content + "</d:multistatus>";
}

QByteArray response(const QByteArray &href, const QByteArray &properties)
{
    return "<d:response><d:href>" + href + "</d:href><d:propstat><d:prop>" + properties +
        "</d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response>";
}

QByteArray taskResponse(const QByteArray &name, const QByteArray &uid, const QByteArray &title,
                        const QByteArray &extra = {}, const QByteArray &instances = {}, bool withEtag = true)
{
    const QByteArray etag = withEtag ? "<d:getetag>\x22v1\x22</d:getetag>" : "";
    return response("/dav.php/calendars/demo/tasks/" + name + ".ics",
        etag + "<c:calendar-data><![CDATA["
        "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Test//CalDAV//EN\r\nBEGIN:VTODO\r\nUID:" +
        uid + "\r\nDTSTAMP:20261101T120000Z\r\nSUMMARY:" + title +
        "\r\nDUE;VALUE=DATE:20261203\r\n" + extra +
        "END:VTODO\r\n" + instances + "END:VCALENDAR\r\n]]></c:calendar-data>");
}

struct MockServer {
    QTcpServer server;
    QStringList methods;
    bool reportBroken = false;
    bool validQuery = false;
    bool deleted = false;
    bool changed = false;
    bool authenticated = false;
    bool incompatible = false;
    bool withoutEtag = false;
    bool recurring = false;
    bool malformedParent = false;
    bool duplicateParent = false;
    QByteArray serverCalendar;
    QByteArray receivedTag;
    QByteArray responseTag = R"("v1")";
    bool conflict = false;
    int writes = 0;
    int creates = 0;
    bool rejectCreate = false;
    bool rejectConfirmation = false;
    bool guardedCreate = false;
    QHash<QByteArray, QByteArray> createdCalendars;
    qsizetype skipped = 0;
    qsizetype recovered = 0;

    MockServer()
    {
        expect(server.listen(QHostAddress::LocalHost), "failed to open mock server");
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
            auto *socket = server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket] { reply(socket); });
        });
    }

    void reply(QTcpSocket *socket)
    {
        auto received = socket->property("http").toByteArray() + socket->readAll();
        const int split = received.indexOf("\r\n\r\n");
        if (split == -1) {
            socket->setProperty("http", received);
            return;
        }
        const auto headers = received.left(split).split('\n');
        int length = 0;
        bool hasAuth = false;
        for (const auto &header : headers) {
            if (header.toLower().startsWith("content-length:"))
                length = header.mid(15).trimmed().toInt();
            if (header.left(14).toLower() == "authorization:" && header.mid(14).trimmed() == "Basic ZGVtbzpsb2NhbA==")
                hasAuth = true;
        }
        if (received.size() < split + 4 + length) {
            socket->setProperty("http", received);
            return;
        }
        const auto request = headers.first().split(' ');
        const auto method = request.value(0);
        const auto path = request.value(1);
        methods += QString::fromUtf8(method);
        QByteArray body;
        QByteArray status = "207 Multi-Status";
        QByteArray challenge;
        QByteArray etag;
        if (!hasAuth) {
            status = "401 Unauthorized";
            challenge = "WWW-Authenticate: Basic realm=\"fixture\"\r\n";
        } else if (method == "PROPFIND" && path == "/dav.php") {
            authenticated = true;
            body = multistatus(response(path, "<d:current-user-principal><d:href>/dav.php/principals/demo/</d:href>"
                                                "</d:current-user-principal>"));
        } else if (method == "PROPFIND" && path == "/dav.php/principals/demo/") {
            body = multistatus(response(path, "<c:calendar-home-set><d:href>/dav.php/calendars/demo/</d:href>"
                                                "</c:calendar-home-set>"));
        } else if (method == "PROPFIND" && path == "/dav.php/calendars/demo/") {
            body = multistatus(response(path, "<d:resourcetype><d:collection/></d:resourcetype>") +
                response(path + "tasks/", "<d:resourcetype><d:collection/><c:calendar/></d:resourcetype>"
                "<d:displayname>Задачи</d:displayname><c:supported-calendar-component-set><c:comp name='VTODO'/>"
                "</c:supported-calendar-component-set>") +
                response(path + "events/", "<d:resourcetype><d:collection/><c:calendar/></d:resourcetype>"
                "<c:supported-calendar-component-set><c:comp name='VEVENT'/></c:supported-calendar-component-set>"));
        } else if (method == "PUT" && path == "/dav.php/calendars/demo/tasks/first.ics") {
            ++writes;
            for (const auto &header : headers)
                if (header.toLower().startsWith("if-match:"))
                    receivedTag = header.mid(9).trimmed();
            if (conflict || receivedTag != responseTag) {
                status = "412 Precondition Failed";
            } else {
                serverCalendar = received.mid(split + 4, length);
                responseTag = responseTag == "\"v1\"" ? "\"v2\"" : "\"v3\"";
                status = "204 No Content";
            }
        } else if (method == "GET" && path == "/dav.php/calendars/demo/tasks/first.ics" && !serverCalendar.isEmpty()) {
            status = "200 OK";
            etag = "ETag: " + responseTag + "\r\n";
            body = serverCalendar;
        } else if (path.startsWith("/dav.php/calendars/demo/tasks/") && path.endsWith(".ics") && method == "PUT") {
            guardedCreate = std::any_of(headers.cbegin(), headers.cend(), [](const auto &header) {
                return header.trimmed().toLower() == "if-none-match: *";
            });
            if (rejectCreate) {
                status = "403 Forbidden";
            } else if (createdCalendars.contains(path)) {
                status = "412 Precondition Failed";
            } else {
                ++creates;
                createdCalendars.insert(path, received.mid(split + 4, length));
                status = "201 Created";
            }
        } else if (method == "GET" && createdCalendars.contains(path)) {
            status = rejectConfirmation ? "503 Unavailable" : "200 OK";
            etag = "ETag: \"created\"\r\n";
            body = createdCalendars.value(path);
        } else if (method == "REPORT" && path == "/dav.php/calendars/demo/tasks/") {
            validQuery = received.mid(split + 4).contains("name='VTODO'");
            QByteArray tasks;
            if (malformedParent) {
                tasks = response(path + "first.ics", "<c:calendar-data>not a task</c:calendar-data>");
            } else {
                QByteArray fields = "CATEGORIES:work\r\nX-PINNED:TRUE\r\nX-CUSTOM:must-survive\r\n";
                QByteArray instances;
                if (incompatible)
                    fields += "DTSTART:not-a-date\r\n";
                if (recurring) {
                    fields += "DTSTART;VALUE=DATE:20261202\r\nRRULE:FREQ=WEEKLY\r\n";
                    instances = "BEGIN:VTODO\r\nUID:uid-a\r\nDTSTAMP:20261101T120000Z\r\n"
                                "RECURRENCE-ID;VALUE=DATE:20261209\r\nSUMMARY:Moved occurrence\r\n"
                                "DTSTART;VALUE=DATE:20261210\r\nEND:VTODO\r\n";
                }
                tasks = taskResponse("first", "uid-a", changed ? "Updated" : "Parent", fields, instances, !withoutEtag);
            }
            if (duplicateParent)
                tasks += taskResponse("duplicate", "uid-a", "Ambiguous replacement");
            if (!deleted)
                tasks += taskResponse("second", "uid-b", malformedParent ? "Updated child" : "Подзадача",
                    "RELATED-TO;RELTYPE=PARENT:uid-a\r\nDESCRIPTION:Строка\\nСледующая\r\n");
            body = reportBroken ? multistatus(tasks).chopped(8) : multistatus(tasks);
        } else {
            status = "404 Not Found";
        }
        socket->write("HTTP/1.1 " + status + "\r\nContent-Type: application/xml; charset=utf-8\r\n"
            "Connection: close\r\n" + challenge + etag + "Content-Length: " + QByteArray::number(body.size()) +
            "\r\n\r\n" + body);
        socket->disconnectFromHost();
    }

    bool create(Store &store, const Project &project, const Task &draft, const QString &token)
    {
        CalDav client;
        QEventLoop loop;
        bool created = false;
        QString error;
        QObject::connect(&client, &CalDav::created, &loop, [&](const Task &task) {
            store.insertRemote(task);
            created = true;
            loop.quit();
        });
        QObject::connect(&client, &CalDav::failed, &loop, [&](const QString &reason) {
            error = reason;
            loop.quit();
        });
        const auto url = QUrl(QString("http://127.0.0.1:%1/dav.php").arg(server.serverPort()));
        expect(client.connectAccount(url, "demo", "local"), "mock account not connected");
        if (client.create(project, draft, token)) {
            QTimer::singleShot(5000, &loop, &QEventLoop::quit);
            loop.exec();
        }
        expect(created || !error.isEmpty(), "mock creation timed out");
        return created;
    }

    bool edit(Store &store, const Task &original, Task draft)
    {
        CalDav client;
        QEventLoop loop;
        bool updated = false;
        QString error;
        QObject::connect(&client, &CalDav::updated, &loop, [&](const Task &before, const Task &after) {
            store.updateRemote(before, after);
            updated = true;
            loop.quit();
        });
        QObject::connect(&client, &CalDav::failed, &loop, [&](const QString &reason) {
            error = reason;
            loop.quit();
        });
        const auto url = QUrl(QString("http://127.0.0.1:%1/dav.php").arg(server.serverPort()));
        expect(client.connectAccount(url, "demo", "local"), "mock account not connected");
        if (client.update(store.project(original.project), original, draft)) {
            QTimer::singleShot(5000, &loop, &QEventLoop::quit);
            loop.exec();
        }
        expect(updated || !error.isEmpty(), "mock write timed out");
        return updated;
    }

    bool import(Store &store)
    {
        CalDav client;
        QEventLoop loop;
        bool ready = false;
        QString error;
        QObject::connect(&client, &CalDav::imported, &loop, [&](const QList<RemoteList> &data) {
            store.importReadOnly(data);
            skipped = data.first().skipped;
            recovered = data.first().recovered;
            ready = true;
            loop.quit();
        });
        QObject::connect(&client, &CalDav::failed, &loop, [&](const QString &reason) {
            error = reason;
            loop.quit();
        });
        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        const auto url = QUrl(QString("http://127.0.0.1:%1/dav.php").arg(server.serverPort()));
        expect(client.start(url, "demo", "local"), "request not started");
        loop.exec();
        if (!ready && !reportBroken)
            qWarning().noquote() << error;
        expect(ready || !error.isEmpty(), "mock network timed out");
        return ready;
    }
};

QStringList ids(QSqlQuery result)
{
    QStringList list;
    while (result.next())
        list += result.value(0).toString();
    return list;
}

void localTasks(const QString &path)
{
    QString id;
    {
        Store store(path);
        const auto project = store.addProject("Проект");
        const auto section = store.addSection(project, "Секция");
        Task task;
        task.title = "Ёжик и задачи";
        task.notes = "Заметки";
        task.project = project;
        task.section = section;
        task.priority = 1;
        task.tags = "Работа, работа";
        task.due = QDate::currentDate();
        task.repeat = "weekly";
        id = store.saveTask(task);
        Filter search;
        search.search = "ЁЖИК";
        expect(ids(store.query(search)) == QStringList{id}, "case-insensitive Russian search");
        expect(store.labels() == QStringList{"Работа"}, "deduplicated label");
        store.toggle(id);
        const auto repeated = store.task(id);
        expect(repeated.due == task.due.addDays(7) && !repeated.done && repeated.completed.isValid(), "weekly repeat");
        task.reminder = QDateTime::currentDateTimeUtc().addSecs(60);
        task.attachments = {"https://example.org/fixture"};
        task.taskTemplate = true;
        task.parent = id;
        task.title = "Подзадача";
        const auto child = store.saveTask(task);
        auto invalid = store.task(id);
        invalid.parent = child;
        bool rejected = false;
        try {
            store.saveTask(invalid);
        } catch (const std::exception &) {
            rejected = true;
        }
        expect(rejected, "task parent cycles rejected");
        store.remove(id);
        expect(!store.task(child).id.isEmpty() && store.task(child).parent.isEmpty(), "children preserved on removal");
        id = child;
    }
    Store reopened(path);
    const auto reopenedTask = reopened.task(id);
    expect(reopenedTask.title == "Подзадача" && reopenedTask.reminder.isValid() &&
           reopenedTask.attachments == QStringList{"https://example.org/fixture"} && reopenedTask.taskTemplate,
           "task card fields did not persist across restart");
}

void backupTasks(const QTemporaryDir &root)
{
    Store source(root.filePath("backup-source.sqlite"));
    Task first;
    first.title = "Alpha";
    first.project = source.addProject("New project");
    const auto section = source.addSection(first.project, "Section");
    first.section = section;
    first.due = QDate::currentDate();
    const auto firstId = source.saveTask(first);
    Task second = first;
    second.parent = firstId;
    second.title = "Beta";
    second.priority = 1;
    second.due = first.due.addDays(-1);
    const auto secondId = source.saveTask(second);
    expect(source.count(Page::Today, true) == 1, "overdue count wrong");
    Filter today;
    today.page = Page::Today;
    today.sort = 1;
    expect(ids(source.query(today)) == QStringList{firstId, secondId}, "title sort wrong");
    second.attachments = {"https://example.org/backup"};
    second.id = secondId;
    source.saveTask(second);
    source.toggle(firstId);
    expect(ids(source.query(today)) == QStringList{secondId}, "completed tasks shown by default");
    today.showDone = true;
    expect(ids(source.query(today)) == QStringList{secondId, firstId}, "completed task setting ignored");

    const auto snapshot = root.filePath("backup # copy.sqlite");
    source.backup(snapshot);
    Store destination(root.filePath("backup-destination.sqlite"));
    destination.importBackup(snapshot);
    expect(destination.projectCounts().value(first.project) == 1 &&
           destination.task(secondId).parent == firstId &&
           destination.task(secondId).attachments == second.attachments &&
           destination.task(firstId).completed.isValid() &&
           destination.sections(first.project).first().first == section, "backup structure lost");
    auto changed = destination.task(secondId);
    changed.title = "Local edit";
    destination.saveTask(changed);
    destination.importBackup(snapshot);
    expect(destination.task(secondId).title == "Local edit" && destination.projectCounts().value(first.project) == 1,
           "backup import overwrote newer data or created duplicates");
    QFile invalid(root.filePath("not-a-backup.sqlite"));
    expect(invalid.open(QIODevice::WriteOnly) && invalid.write("invalid") == 7, "failed to create corrupt fixture");
    invalid.close();
    bool rejected = false;
    try {
        destination.importBackup(invalid.fileName());
    } catch (const std::exception &) {
        rejected = true;
    }
    expect(rejected && destination.task(secondId).title == "Local edit", "invalid backup changed live data");
}

void remoteTasks(const QString &path)
{
    Store store(path);
    Task local;
    local.project = Store::inbox();
    local.title = "Не отправлять";
    const auto localId = store.saveTask(local);
    MockServer mock;
    expect(mock.import(store), "initial mock import failed");
    expect(mock.authenticated && mock.validQuery && store.projects().size() == 1, "DAV discovery and component filtering");
    Filter page;
    page.page = Page::Project;
    page.project = store.projects().first().id;
    auto tasks = ids(store.query(page));
    expect(tasks.size() == 2, "two remote tasks");
    const auto parent = store.task(tasks.first());
    const auto child = store.task(tasks.last());
    expect(parent.pinned && parent.calendarData.contains("X-CUSTOM:must-survive"), "unmodeled data retained");
    expect(child.parent == parent.id && child.notes.contains('\n'), "subtask and description imported");
    bool rejected = false;
    try {
        store.toggle(parent.id);
    } catch (const std::exception &) {
        rejected = true;
    }
    expect(rejected && !store.task(parent.id).done, "remote edits prohibited");

    expect(mock.import(store) && ids(store.query(page)) == tasks, "reimport produced duplicates");
    mock.reportBroken = true;
    mock.changed = true;
    expect(!mock.import(store) && ids(store.query(page)) == tasks &&
        store.task(parent.id).title == "Parent", "partial response replaced local snapshot");

    mock.reportBroken = false;
    mock.incompatible = true;
    expect(mock.import(store) && mock.recovered == 1 && mock.skipped == 0, "partial iCalendar fields blocked import");
    auto updatedParent = store.task(parent.id);
    expect(updatedParent.title == "Updated" && updatedParent.calendarData.contains("DTSTART:not-a-date") &&
           updatedParent.due == QDate(2026,12,3), "recoverable task lost data");

    mock.incompatible = false;
    mock.withoutEtag = true;
    expect(mock.import(store) && mock.recovered == 1 && store.task(parent.id).etag.isEmpty(),
        "missing ETag blocked read-only import");

    mock.withoutEtag = false;
    mock.recurring = true;
    expect(mock.import(store) && mock.skipped == 0 && mock.recovered == 0, "recurring task with an exception failed");
    updatedParent = store.task(parent.id);
    expect(updatedParent.title == "Updated" && updatedParent.repeat == "remote" &&
           updatedParent.due == QDate(2026,12,2) && updatedParent.calendarData.contains("RECURRENCE-ID"),
           "recurrence master or raw exception lost");

    mock.recurring = false;
    mock.changed = false;
    mock.malformedParent = true;
    expect(mock.import(store) && mock.skipped == 1 && mock.recovered == 0 &&
           store.task(parent.id).title == "Updated" && store.task(child.id).parent == parent.id &&
           store.task(child.id).title == "Updated child",
           "malformed object hid the collection or erased existing tasks");

    mock.malformedParent = false;
    mock.duplicateParent = true;
    mock.deleted = true;
    expect(mock.import(store) && mock.skipped == 2 && ids(store.query(page)) == tasks &&
           store.task(parent.id).title == "Updated", "ambiguous snapshot changed the local copy");

    mock.duplicateParent = false;
    mock.changed = true;
    expect(mock.import(store), "updated mock import failed");
    expect(ids(store.query(page)) == QStringList{parent.id} &&
        store.task(parent.id).title == "Updated", "server changes not mirrored");
    expect(store.task(localId).title == local.title, "import touched local task");
    for (const auto &method : mock.methods)
        expect(method == "PROPFIND" || method == "REPORT", "unexpected write request");
}

void remoteWrites(const QString &path)
{
    Store store(path);
    MockServer mock;
    expect(mock.import(store), "mock import for write failed");
    const auto project = store.projects().first();
    Filter page;
    page.page = Page::Project;
    page.project = project.id;
    const auto tasks = ids(store.query(page));
    const auto original = store.task(tasks.first());
    const auto child = store.task(tasks.last());
    auto draft = original;
    draft.done = true;
    draft.priority = 2;
    draft.tags = "alpha, beta";
    draft.due = QDate(2026, 12, 5);
    draft.reminder = QDateTime(QDate(2026, 12, 4), QTime(14, 35), QTimeZone::utc());
    draft.attachments = {"https://example.org/main"};
    expect(mock.edit(store, original, draft), "conditional completion failed");
    const auto completed = store.task(original.id);
    expect(mock.writes == 1 && mock.receivedTag == original.etag.toLatin1() &&
           completed.etag == QString::fromLatin1(mock.responseTag) && completed.done &&
           completed.completed.isValid() && completed.due == draft.due && completed.priority == draft.priority &&
           completed.tags == draft.tags && completed.reminder == draft.reminder && completed.attachments == draft.attachments,
           "confirmed fields or fresh ETag missing");
    expect(mock.serverCalendar.contains("X-CUSTOM:must-survive") && mock.serverCalendar.contains("UID:uid-a") &&
           store.task(child.id).parent == original.id && ids(store.query(page)) == QStringList{child.id},
           "write damaged unmodeled data or child task");
    mock.conflict = true;
    draft = completed;
    draft.done = false;
    expect(!mock.edit(store, completed, draft) && store.task(original.id).done && mock.writes == 2,
           "conflict overwrote local or server state");
    mock.conflict = false;
    expect(mock.edit(store, completed, draft) && !store.task(original.id).done &&
           store.task(original.id).completed.isValid(), "reopening failed or lost completion history");
    mock.recurring = true;
    expect(mock.import(store), "could not import recurring task");
    const auto recurring = store.task(original.id);
    draft = recurring;
    draft.done = true;
    expect(!mock.edit(store, recurring, draft) && mock.writes == 3 &&
           store.task(original.id).calendarData.contains("RECURRENCE-ID"),
           "update overwrote a recurring VTODO");
    expect(CalDav::exportTask(completed) == completed.calendarData, "export changed remote calendar");
}


void remoteCreation(const QString &path)
{
    Store store(path);
    MockServer mock;
    expect(mock.import(store), "mock import for creation failed");
    const auto project = store.projects().first();
    Filter page;
    page.page = Page::Project;
    page.project = project.id;
    Task draft;
    draft.project = project.id;
    draft.title = "Новая задача";
    draft.notes = "Проверить создание";
    draft.tags = "alpha, alpha, beta";
    draft.due = QDate(2026, 12, 1);
    draft.deadline = QDate(2026, 12, 2);
    draft.priority = 2;
    draft.pinned = true;
    const QString uuid = "e1676916-e089-45b2-9074-8707d4ce4059";
    mock.rejectCreate = true;
    expect(!mock.create(store, project, draft, uuid) && ids(store.query(page)).size() == 2 && mock.creates == 0,
           "failed creation modified the cache");
    mock.rejectCreate = false;
    mock.rejectConfirmation = true;
    expect(!mock.create(store, project, draft, uuid) && ids(store.query(page)).size() == 2 &&
           mock.guardedCreate && mock.creates == 1, "unconfirmed remote creation treated as saved");
    mock.rejectConfirmation = false;
    expect(mock.create(store, project, draft, uuid) && mock.creates == 1,
           "retry of an accepted creation duplicated it or failed to recover");
    const auto tasks = ids(store.query(page));
    const auto created = store.task(tasks.first());
    expect(tasks.size() == 3 && created.remote() && created.remoteUid == uuid + "@plan-tasks" &&
           created.etag == "\"created\"" && created.title == draft.title && created.notes == draft.notes &&
           created.due == draft.due && created.deadline == draft.deadline && created.priority == draft.priority &&
           created.tags == "alpha, beta", "created task was not cached in the chosen calendar");
    draft.notes = "Edited after uncertainty";
    expect(!mock.create(store, project, draft, uuid) && mock.creates == 1 &&
           store.task(created.id).notes == created.notes, "retry overwrote existing remote task");
    const QString secondUuid = "9d2bbe21-0153-42fc-9e09-38c479757253";
    expect(mock.create(store, project, draft, secondUuid) && mock.creates == 2 && ids(store.query(page)).size() == 4,
           "new request and confirmation did not persist a second task");
}


void caldavCredentials()
{
    MockServer server;
    CalDav client;
    const QUrl address(QString("http://127.0.0.1:%1/dav.php").arg(server.server.serverPort()));
    int verified = 0;
    int rejected = 0;
    bool finished = false;
    QEventLoop loop;
    QTimer alarm;
    alarm.setSingleShot(true);
    QObject::connect(&alarm, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&client, &CalDav::credentialsVerified, &loop, [&] { ++verified; });
    QObject::connect(&client, &CalDav::credentialsRejected, &loop, [&](const QUrl &url, const QString &user) {
        expect(url == address && user == "demo", "failure was assigned to the wrong account");
        ++rejected;
    });
    QObject::connect(&client, &CalDav::imported, &loop, [&] { finished = true; loop.quit(); });
    QObject::connect(&client, &CalDav::failed, &loop, &QEventLoop::quit);
    const auto wait = [&] { alarm.start(5000); loop.exec(); };
    expect(client.start(address, "demo", "incorrect"), "failed to start invalid auth fixture");
    wait();
    expect(!finished && rejected == 1 && verified == 0 && !client.refreshAccount(),
           "invalid password remained active or was considered verified");
    server.reportBroken = true;
    expect(client.start(address, "demo", "local"), "failed to start authenticated fixture");
    wait();
    expect(!finished && verified == 1, "valid credentials were lost after a separate calendar error");
    server.reportBroken = false;
    expect(client.refreshAccount(), "the refresh did not reuse the connection");
    wait();
    expect(finished && verified == 2 && rejected == 1, "refresh with the accepted password failed");
}

void projectControls(const QTemporaryDir &root)
{
    Store store(root.filePath("project-controls.sqlite"));
    const auto id = store.addProject("Original project");
    auto details = store.project(id);
    details.title = "Renamed project";
    details.notes = "Project note";
    details.emoji = "🧪";
    details.deadline = QDate::currentDate().addDays(5);
    details.favorite = true;
    details.markdown = 1;
    store.saveProject(details);
    const auto first = store.addSection(id, "First");
    const auto second = store.addSection(id, "Second");
    store.moveSection(id, first, 1);
    store.renameSection(id, second, "Renamed section");
    expect(store.sections(id).first() == qMakePair(second, QString("Renamed section")),
           "section order or title was not saved");
    Task task;
    task.project = id;
    task.section = first;
    task.title = "Parent due today";
    task.priority = 1;
    task.tags = "Alpha, beta";
    task.due = QDate::currentDate();
    const auto parent = store.saveTask(task);
    task.parent = parent;
    task.title = "Child without a date";
    task.due = {};
    task.tags = "alpha";
    task.priority = 2;
    const auto child = store.saveTask(task);
    store.toggle(child);
    Filter filter;
    filter.page = Page::Project;
    filter.project = id;
    filter.showDone = true;
    filter.dateFilter = DateFilter::NoDate;
    filter.priorityFilter = 2;
    filter.projectTag = "ALPHA";
    expect(ids(store.query(filter)) == QStringList{child}, "filters did not intersect correctly");
    filter.showDone = false;
    expect(ids(store.query(filter)).isEmpty(), "completed filtering was ignored");
    const auto copy = store.duplicateProject(id);
    filter = Filter{.page = Page::Project, .project = copy, .showDone = true};
    const auto tasks = ids(store.query(filter));
    expect(tasks.size() == 2 && tasks.first() != parent && store.task(tasks.last()).parent == tasks.first() &&
           store.task(tasks.last()).done && store.sections(copy).size() == 2, "duplication lost project structure");
    details.archived = true;
    store.saveProject(details);
    expect(store.project(id).archived && store.count(Page::Today) == 1, "archived project's tasks stayed in Today");
    const auto snapshot = root.filePath("project-controls-backup.sqlite");
    store.backup(snapshot);
    Store restored(root.filePath("project-controls-restored.sqlite"));
    restored.importBackup(snapshot);
    const auto verified = restored.project(id);
    expect(verified.title == details.title && verified.notes == details.notes && verified.deadline == details.deadline &&
           verified.favorite && verified.archived && verified.markdown == 1, "backup lost project details");
    store.removeProject(copy);
    expect(store.project(copy).id.isEmpty() && store.task(parent).id == parent, "copy removal modified original tasks");
}

}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QTemporaryDir root;
    try {
        expect(root.isValid(), "temporary directory unavailable");
        localTasks(root.filePath("local.sqlite"));
        remoteTasks(root.filePath("remote.sqlite"));
        remoteWrites(root.filePath("remote-writes.sqlite"));
        remoteCreation(root.filePath("remote-creation.sqlite"));
        backupTasks(root);
        projectControls(root);
        caldavCredentials();
        return 0;
    } catch (const std::exception &error) {
        qCritical("%s", error.what());
        return 1;
    }
}
