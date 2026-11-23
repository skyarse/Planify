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
                        const QByteArray &extra = {})
{
    return response("/dav.php/calendars/demo/tasks/" + name + ".ics",
        "<d:getetag>\x22v1\x22</d:getetag><c:calendar-data><![CDATA["
        "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Test//CalDAV//EN\r\nBEGIN:VTODO\r\nUID:" +
        uid + "\r\nDTSTAMP:20261101T120000Z\r\nSUMMARY:" + title +
        "\r\nDUE;VALUE=DATE:20261203\r\n" + extra +
        "END:VTODO\r\nEND:VCALENDAR\r\n]]></c:calendar-data>");
}

struct MockServer {
    QTcpServer server;
    QStringList methods;
    bool reportBroken = false;
    bool validQuery = false;
    bool deleted = false;
    bool changed = false;
    bool authenticated = false;

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
        } else if (method == "REPORT" && path == "/dav.php/calendars/demo/tasks/") {
            validQuery = received.mid(split + 4).contains("name='VTODO'");
            auto tasks = taskResponse("first", "uid-a", changed ? "Updated" : "Parent",
                "CATEGORIES:work\r\nX-PINNED:TRUE\r\nX-CUSTOM:must-survive\r\n");
            if (!deleted)
                tasks += taskResponse("second", "uid-b", "Подзадача",
                    "RELATED-TO;RELTYPE=PARENT:uid-a\r\nDESCRIPTION:Строка\\nСледующая\r\n");
            body = reportBroken ? multistatus(tasks).chopped(8) : multistatus(tasks);
        } else {
            status = "404 Not Found";
        }
        socket->write("HTTP/1.1 " + status + "\r\nContent-Type: application/xml; charset=utf-8\r\n"
            "Connection: close\r\n" + challenge + "Content-Length: " + QByteArray::number(body.size()) +
            "\r\n\r\n" + body);
        socket->disconnectFromHost();
    }

    bool import(Store &store)
    {
        CalDav client;
        QEventLoop loop;
        bool ready = false;
        QString error;
        QObject::connect(&client, &CalDav::imported, &loop, [&](const QList<RemoteList> &data) {
            store.importReadOnly(data);
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
        expect(repeated.due == task.due.addDays(7) && !repeated.done, "weekly repeat");
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
    expect(reopened.task(id).title == "Подзадача", "task persists across restart");
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
    mock.deleted = true;
    expect(mock.import(store), "updated mock import failed");
    expect(ids(store.query(page)) == QStringList{parent.id} &&
        store.task(parent.id).title == "Updated", "server changes not mirrored");
    expect(store.task(localId).title == local.title, "import touched local task");
    for (const auto &method : mock.methods)
        expect(method == "PROPFIND" || method == "REPORT", "unexpected write request");
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
        backupTasks(root);
        return 0;
    } catch (const std::exception &error) {
        qCritical("%s", error.what());
        return 1;
    }
}
