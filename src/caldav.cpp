#include "caldav.h"

#include <QAuthenticator>
#include <QCryptographicHash>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QXmlStreamReader>
#include <libical/ical.h>
#include <algorithm>
#include <memory>
#include <utility>
#include <stdexcept>

namespace {
const QString dav = QStringLiteral("DAV:");
const QString cal = QStringLiteral("urn:ietf:params:xml:ns:caldav");
constexpr qsizetype maxReplyBytes = 24 * 1024 * 1024;

struct Properties {
    QString href;
    QString principal;
    QString home;
    QString name;
    QString etag;
    QByteArray ical;
    bool calendar = false;
    bool componentsListed = false;
    bool tasksSupported = false;
};

void fail(const QString &message)
{
    throw std::runtime_error(message.toStdString());
}

QUrl resolve(const QUrl &base, const QString &href)
{
    if (href.isEmpty())
        fail(QStringLiteral("В ответе CalDAV отсутствует адрес"));
    auto target = base.resolved(QUrl(href, QUrl::StrictMode));
    if (!target.isValid() || target.scheme() != base.scheme() ||
        target.host() != base.host() || target.port(base.scheme() == "https" ? 443 : 80) !=
        base.port(base.scheme() == "https" ? 443 : 80) ||
        !target.userInfo().isEmpty() || target.hasFragment())
        fail(QStringLiteral("Сервер CalDAV вернул адрес вне исходного сервера"));
    return target;
}

QString nestedHref(QXmlStreamReader &xml)
{
    QString result;
    while (xml.readNextStartElement()) {
        if (xml.namespaceUri() == dav && xml.name() == u"href")
            result = xml.readElementText();
        else
            xml.skipCurrentElement();
    }
    return result;
}

void addProperties(QXmlStreamReader &xml, Properties &props)
{
    while (xml.readNextStartElement()) {
        const auto space = xml.namespaceUri().toString();
        const auto name = xml.name().toString();
        if (space == dav && name == "current-user-principal") {
            props.principal = nestedHref(xml);
        } else if (space == cal && name == "calendar-home-set") {
            props.home = nestedHref(xml);
        } else if (space == dav && name == "displayname") {
            props.name = xml.readElementText();
        } else if (space == dav && name == "getetag") {
            props.etag = xml.readElementText();
        } else if (space == cal && name == "calendar-data") {
            props.ical = xml.readElementText().toUtf8();
        } else if (space == dav && name == "resourcetype") {
            while (xml.readNextStartElement()) {
                props.calendar |= xml.namespaceUri() == cal && xml.name() == u"calendar";
                xml.skipCurrentElement();
            }
        } else if (space == cal && name == "supported-calendar-component-set") {
            props.componentsListed = true;
            while (xml.readNextStartElement()) {
                props.tasksSupported |= xml.namespaceUri() == cal && xml.name() == u"comp"
                    && xml.attributes().value(u"name") == u"VTODO";
                xml.skipCurrentElement();
            }
        } else {
            xml.skipCurrentElement();
        }
    }
}

bool successStatus(const QString &status)
{
    static const QRegularExpression pattern(QStringLiteral(R"(^HTTP/\S+ 2[0-9]{2}(?:\s|$))"));
    return pattern.match(status).hasMatch();
}

QList<Properties> readResponse(const QByteArray &data, bool strict)
{
    QXmlStreamReader xml(data);
    xml.setEntityExpansionLimit(64);
    while (!xml.atEnd() && !xml.isStartElement()) {
        if (xml.readNext() == QXmlStreamReader::DTD)
            fail(QStringLiteral("CalDAV вернул неподдерживаемый XML"));
    }
    if (xml.namespaceUri() != dav || xml.name() != u"multistatus")
        fail(QStringLiteral("CalDAV вернул ответ без DAV:multistatus"));

    QList<Properties> result;
    while (xml.readNextStartElement()) {
        if (xml.namespaceUri() != dav || xml.name() != u"response") {
            if (strict)
                fail(QStringLiteral("CalDAV вернул неполный список задач"));
            xml.skipCurrentElement();
            continue;
        }
        QString href;
        QList<Properties> successful;
        while (xml.readNextStartElement()) {
            if (xml.namespaceUri() == dav && xml.name() == u"href") {
                href = xml.readElementText();
            } else if (xml.namespaceUri() == dav && xml.name() == u"propstat") {
                QString status;
                Properties properties;
                while (xml.readNextStartElement()) {
                    if (xml.namespaceUri() == dav && xml.name() == u"prop")
                        addProperties(xml, properties);
                    else if (xml.namespaceUri() == dav && xml.name() == u"status")
                        status = xml.readElementText();
                    else
                        xml.skipCurrentElement();
                }
                if (successStatus(status))
                    successful.append(properties);
            } else {
                xml.skipCurrentElement();
            }
        }
        if (href.isEmpty())
            fail(QStringLiteral("CalDAV вернул неполный список задач"));
        Properties entry;
        entry.href = href;
        for (const auto &props : successful) {
            if (!props.principal.isEmpty()) entry.principal = props.principal;
            if (!props.home.isEmpty()) entry.home = props.home;
            if (!props.name.isEmpty()) entry.name = props.name;
            if (!props.etag.isEmpty()) entry.etag = props.etag;
            if (!props.ical.isEmpty()) entry.ical = props.ical;
            entry.calendar |= props.calendar;
            entry.componentsListed |= props.componentsListed;
            entry.tasksSupported |= props.tasksSupported;
        }
        result.append(entry);
    }
    while (!xml.atEnd())
        xml.readNext();
    if (xml.hasError())
        fail(QStringLiteral("Ответ CalDAV повреждён или прерван"));
    return result;
}

QString dataId(const QByteArray &value)
{
    return QString::fromLatin1(QCryptographicHash::hash(value, QCryptographicHash::Sha256).toHex());
}

std::optional<Task> readTask(const Project &project, const QString &href, const Properties &entry, bool &recovered)
{
    if (entry.ical.isEmpty() || entry.ical.contains('\0'))
        return {};
    using Component = std::unique_ptr<icalcomponent, decltype(&icalcomponent_free)>;
    Component calendar(icalparser_parse_string(entry.ical.constData()), &icalcomponent_free);
    if (!calendar || icalcomponent_isa(calendar.get()) != ICAL_VCALENDAR_COMPONENT)
        return {};

    icalcomponent *master = nullptr;
    icalcomponent *first = nullptr;
    QString uid;
    int instances = 0;
    for (auto *part = icalcomponent_get_first_component(calendar.get(), ICAL_VTODO_COMPONENT); part;
         part = icalcomponent_get_next_component(calendar.get(), ICAL_VTODO_COMPONENT)) {
        const auto partUid = QString::fromUtf8(icalcomponent_get_uid(part));
        if (partUid.trimmed().isEmpty() || (first && partUid != uid))
            return {};
        if (!first) {
            first = part;
            uid = partUid;
        }
        if (!icalcomponent_get_first_property(part, ICAL_RECURRENCEID_PROPERTY)) {
            if (master)
                return {};
            master = part;
        }
        ++instances;
    }
    if (!master && instances != 1)
        return {};
    auto *todo = master ? master : first;

    Task t;
    t.remoteUid = uid;
    const bool hasParseErrors = icalcomponent_count_errors(calendar.get()) > 0;
    t.title = QString::fromUtf8(icalcomponent_get_summary(todo));
    if (t.title.isEmpty() && hasParseErrors)
        return {};
    recovered = hasParseErrors || entry.etag.isEmpty();
    if (instances > 1)
        t.repeat = QStringLiteral("remote");
    t.id = dataId(project.id.toUtf8() + '\0' + t.remoteUid.toUtf8());
    t.project = project.id;
    t.href = href;
    t.etag = entry.etag;
    t.calendarData = entry.ical;
    if (t.title.isEmpty())
        t.title = QStringLiteral("Без названия");
    t.notes = QString::fromUtf8(icalcomponent_get_description(todo));
    auto date = icalcomponent_get_due(todo);
    if (!icaltime_is_null_time(date))
        t.due = QDate(date.year, date.month, date.day);
    date = icalcomponent_get_dtstart(todo);
    if (!icaltime_is_null_time(date)) {
        t.deadline = t.due;
        t.due = QDate(date.year, date.month, date.day);
    }
    const auto *priorityProperty = icalcomponent_get_first_property(todo, ICAL_PRIORITY_PROPERTY);
    const int priority = priorityProperty ? icalproperty_get_priority(priorityProperty) : 0;
    t.priority = priority == 0 ? 0 : priority <= 4 ? 1 : priority == 5 ? 2 : 3;
    const auto *percent = icalcomponent_get_first_property(todo, ICAL_PERCENTCOMPLETE_PROPERTY);
    t.done = icalcomponent_get_status(todo) == ICAL_STATUS_COMPLETED ||
             (percent && icalproperty_get_percentcomplete(percent) == 100);
    QStringList tags;
    for (auto *property = icalcomponent_get_first_property(todo, ICAL_ANY_PROPERTY);
         property; property = icalcomponent_get_next_property(todo, ICAL_ANY_PROPERTY)) {
        switch (icalproperty_isa(property)) {
        case ICAL_CATEGORIES_PROPERTY:
            tags.append(QString::fromUtf8(icalproperty_get_categories(property)).split(',', Qt::SkipEmptyParts));
            break;
        case ICAL_RELATEDTO_PROPERTY: {
            const auto *parameter = icalproperty_get_first_parameter(property, ICAL_RELTYPE_PARAMETER);
            if (!parameter || icalparameter_get_reltype(parameter) == ICAL_RELTYPE_PARENT)
                t.remoteParentUid = QString::fromUtf8(icalproperty_get_relatedto(property));
            break;
        }
        case ICAL_RRULE_PROPERTY:
            t.repeat = QStringLiteral("remote");
            break;
        case ICAL_X_PROPERTY:
            if (qstricmp(icalproperty_get_x_name(property), "X-PINNED") == 0) {
                const auto value = QString::fromUtf8(icalproperty_get_x(property));
                t.pinned = value == "1" || value.compare("true", Qt::CaseInsensitive) == 0;
            }
            break;
        default:
            break;
        }
    }
    for (auto &tag : tags)
        tag = tag.trimmed();
    tags.removeDuplicates();
    t.tags = tags.join(", ");
    return t;
}
}

CalDav::CalDav(QObject *parent) : QObject(parent)
{
    connect(&network, &QNetworkAccessManager::authenticationRequired, this,
        [this](QNetworkReply *reply, QAuthenticator *auth) {
            if (!active || reply->property("credentialsSent").toBool())
                return;
            reply->setProperty("credentialsSent", true);
            auth->setUser(username);
            auth->setPassword(password);
        });
}

bool CalDav::start(const QUrl &address, const QString &user, const QString &secret)
{
    if (active)
        return false;
    const bool loopback = address.host() == "localhost" || address.host() == "127.0.0.1" || address.host() == "::1";
    if (!address.isValid() || address.host().isEmpty() || !address.userInfo().isEmpty() ||
        address.hasFragment() || address.hasQuery() || user.isEmpty() || secret.isEmpty() ||
        (address.scheme() != "https" && (address.scheme() != "http" || !loopback))) {
        emit failed(QStringLiteral("Укажите HTTPS-адрес Baïkal, логин и пароль"));
        return false;
    }

    network.clearAccessCache();
    origin = address;
    username = user;
    password = secret;
    active = true;
    collection = 0;
    lists.clear();
    emit progress(QStringLiteral("Подключение к Baïkal…"));
    request(Step::Principal, address);
    return true;
}

void CalDav::request(Step step, const QUrl &url)
{
    QNetworkRequest settings(url);
    settings.setTransferTimeout(20000);
    settings.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    settings.setHeader(QNetworkRequest::ContentTypeHeader, "application/xml; charset=utf-8");

    QByteArray body;
    switch (step) {
    case Step::Principal:
        body = "<d:propfind xmlns:d='DAV:'><d:prop><d:current-user-principal/></d:prop></d:propfind>";
        break;
    case Step::Home:
        body = "<d:propfind xmlns:d='DAV:' xmlns:c='urn:ietf:params:xml:ns:caldav'>"
               "<d:prop><c:calendar-home-set/></d:prop></d:propfind>";
        break;
    case Step::Collections:
        body = "<d:propfind xmlns:d='DAV:' xmlns:c='urn:ietf:params:xml:ns:caldav'>"
               "<d:prop><d:resourcetype/><d:displayname/><c:supported-calendar-component-set/></d:prop></d:propfind>";
        break;
    case Step::Tasks:
        body = "<c:calendar-query xmlns:d='DAV:' xmlns:c='urn:ietf:params:xml:ns:caldav'>"
               "<d:prop><d:getetag/><c:calendar-data/></d:prop>"
               "<c:filter><c:comp-filter name='VCALENDAR'><c:comp-filter name='VTODO'/></c:comp-filter></c:filter>"
               "</c:calendar-query>";
        break;
    }

    settings.setRawHeader("Depth", step == Step::Tasks || step == Step::Collections ? "1" : "0");
    auto *reply = network.sendCustomRequest(settings, step == Step::Tasks ? "REPORT" : "PROPFIND", body);
    reply->setReadBufferSize(maxReplyBytes + 1);
    connect(reply, &QNetworkReply::readyRead, this, [reply] {
        if (reply->bytesAvailable() > maxReplyBytes) {
            reply->setProperty("overLimit", true);
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, step, url] {
        reply->deleteLater();
        if (reply->property("overLimit").toBool()) {
            finish(QStringLiteral("Ответ Baïkal слишком большой. Локальная копия сохранена"));
            return;
        }
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 207 || reply->error() != QNetworkReply::NoError) {
            finish(status >= 300 && status < 400
                ? QStringLiteral("Baïkal перенаправил запрос. Укажите конечный адрес dav.php")
                : QStringLiteral("Ошибка подключения к Baïkal (%1)").arg(status ? QString::number(status) : reply->errorString()));
            return;
        }
        try {
            parse(step, url, reply->readAll());
        } catch (const std::exception &e) {
            finish(QString::fromUtf8(e.what()));
        }
    });
}

void CalDav::parse(Step step, const QUrl &url, const QByteArray &data)
{
    const auto entries = readResponse(data, step == Step::Tasks);
    if (step == Step::Principal || step == Step::Home) {
        for (const auto &entry : entries) {
            const auto &href = step == Step::Principal ? entry.principal : entry.home;
            if (!href.isEmpty()) {
                request(step == Step::Principal ? Step::Home : Step::Collections, resolve(url, href));
                return;
            }
        }
        fail(QStringLiteral("Сервер не сообщил адрес календарей. Проверьте, что указан dav.php"));
    }
    if (step == Step::Collections) {
        QSet<QUrl> seen;
        for (const auto &entry : entries) {
            if (!entry.calendar || (entry.componentsListed && !entry.tasksSupported))
                continue;
            const auto target = resolve(url, entry.href);
            if (seen.contains(target))
                continue;
            seen.insert(target);
            const auto id = dataId(username.toUtf8() + '\0' + target.toEncoded());
            auto name = entry.name;
            if (name.isEmpty()) {
                const auto parts = target.path().split('/', Qt::SkipEmptyParts);
                name = parts.isEmpty() ? QStringLiteral("Задачи Baïkal") : parts.last();
            }
            lists.append(RemoteList{Project{id, name, username + '@' + origin.host(), target.toString()}, {}});
        }
        if (lists.isEmpty())
            fail(QStringLiteral("В этой учётной записи не найдено списков задач"));
        nextCollection();
        return;
    }
    auto &list = lists[collection];
    QHash<QString, QString> ids;
    QSet<QString> duplicateUids;
    QSet<QString> recoveredUids;
    QSet<QUrl> resources;
    QString basePath = url.path(QUrl::FullyEncoded);
    if (!basePath.endsWith('/'))
        basePath += '/';
    for (const auto &entry : entries) {
        const auto href = resolve(url, entry.href);
        if (!href.path(QUrl::FullyEncoded).startsWith(basePath) || href == url || resources.contains(href))
            fail(QStringLiteral("CalDAV вернул неоднозначный список задач"));
        resources.insert(href);
        bool recovered = false;
        auto task = readTask(list.project, href.toString(), entry, recovered);
        if (!task) {
            ++list.skipped;
            continue;
        }
        if (duplicateUids.contains(task->remoteUid)) {
            ++list.skipped;
            continue;
        }
        if (ids.remove(task->remoteUid)) {
            duplicateUids.insert(task->remoteUid);
            recoveredUids.remove(task->remoteUid);
            list.skipped += 2;
            continue;
        }
        ids.insert(task->remoteUid, task->id);
        if (recovered)
            recoveredUids.insert(task->remoteUid);
        list.tasks.append(std::move(*task));
    }
    list.recovered = recoveredUids.size();
    list.tasks.removeIf([&](const Task &task) { return duplicateUids.contains(task.remoteUid); });
    for (auto &t : list.tasks)
        if (t.remoteParentUid != t.remoteUid)
            t.parent = ids.value(t.remoteParentUid);
    ++collection;
    nextCollection();
}

void CalDav::nextCollection()
{
    if (collection == lists.size()) {
        finish();
        return;
    }
    const auto &project = lists[collection].project;
    emit progress(QStringLiteral("Baïkal · %1").arg(project.title));
    request(Step::Tasks, QUrl(project.href));
}

void CalDav::finish(const QString &error)
{
    active = false;
    username.clear();
    password.clear();
    network.clearAccessCache();
    if (error.isEmpty()) {
        const auto result = std::exchange(lists, {});
        emit imported(result);
    } else {
        lists.clear();
        emit failed(error);
    }
}
