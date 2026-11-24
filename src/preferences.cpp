#include "window.h"
#include "appearance.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSqlQueryModel>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTimeZone>
#include <QUuid>
#include <QVBoxLayout>
#include <libical/ical.h>
#include <array>
#include <memory>
#include <stdexcept>

namespace {
struct Category {
    Mark mark;
    const char *title;
    const char *hint;
};

constexpr std::array categories{
    Category{Mark::Cloud,"Учётные записи","Подключение к спискам задач через CalDAV"},
    Category{Mark::Home,"Вид домашней страницы","Выберите раздел, который открывается при запуске"},
    Category{Mark::Gear,"Общие","Настройте поведение окна"},
    Category{Mark::Complete,"Настройки задач","Параметры новых задач и раздела «Сегодня»"},
    Category{Mark::Split,"Боковая панель","Настройте вашу боковую панель"},
    Category{Mark::Colors,"Оформление","Цветовая тема"},
    Category{Mark::Calendar,"События календаря","Выберите, какие календари отображать"},
    Category{Mark::Quick,"Быстрое добавление","Создавайте задачи с клавиатуры"},
    Category{Mark::Backup,"Резервные копии","Создайте или импортируйте копию задач"},
    Category{Mark::Help,"Создать обучающий проект","Изучите приложение с помощью небольшого проекта"},
};

constexpr qint64 maxCalendarBytes = 2 * 1024 * 1024;
using Calendar = std::unique_ptr<icalcomponent, decltype(&icalcomponent_free)>;

Calendar readCalendar(const QString &name)
{
    QFile file(name);
    Calendar result(nullptr, &icalcomponent_free);
    if (!file.open(QIODevice::ReadOnly) || file.size() > maxCalendarBytes)
        return result;
    const auto bytes = file.read(maxCalendarBytes+1);
    if (bytes.size() > maxCalendarBytes || bytes.contains('\0'))
        return result;
    result.reset(icalparser_parse_string(bytes.constData()));
    if (result && (icalcomponent_isa(result.get()) != ICAL_VCALENDAR_COMPONENT ||
                   icalcomponent_count_errors(result.get()) != 0))
        result.reset();
    return result;
}

QString calendarDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/calendars";
}

QString calendarFile(const QString &id)
{
    if (QUuid(id).isNull() || QUuid(id).toString(QUuid::WithoutBraces) != id)
        return {};
    return calendarDirectory() + "/" + id + ".ics";
}

QLabel *settingLabel(QWidget *parent, const QString &label, const QString &type)
{
    auto *text = new QLabel(label, parent);
    text->setObjectName(type);
    text->setTextFormat(Qt::PlainText);
    return text;
}

QPushButton *settingButton(QWidget *parent, const QString &label)
{
    auto *button = new QPushButton(label,parent);
    button->setObjectName("secondary");
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

QScrollArea *scroller(QWidget *parent, QWidget *content)
{
    auto *area = new QScrollArea(parent);
    area->setObjectName("preferencesScroll");
    area->setWidgetResizable(true);
    area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    area->setWidget(content);
    return area;
}
}

QDialog *Window::makePreferences()
{
    const auto tint = palette().color(QPalette::WindowText);
    auto *dialog = new QDialog(this, Qt::Dialog | Qt::FramelessWindowHint);
    dialog->setObjectName("preferences");
    dialog->setWindowTitle(QStringLiteral("Параметры"));
    dialog->resize(455,620);
    dialog->setMinimumSize(360,400);
    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(0,4,0,4);
    auto *header = new QHBoxLayout;
    header->setContentsMargins(9,0,9,0);
    auto *back = iconButton(dialog, Mark::Back, QStringLiteral("Назад"), "windowControl");
    auto policy = back->sizePolicy();
    policy.setRetainSizeWhenHidden(true);
    back->setSizePolicy(policy);
    header->addWidget(back);
    auto *heading = settingLabel(dialog, QStringLiteral("Параметры"), "settingsTitle");
    heading->setAlignment(Qt::AlignCenter);
    header->addWidget(heading,1);
    auto *close = iconButton(dialog, Mark::Close, QStringLiteral("Закрыть"), "windowControl");
    header->addWidget(close);
    layout->addLayout(header);
    auto *stack = new QStackedWidget(dialog);
    layout->addWidget(stack,1);
    connect(close, &QPushButton::clicked,dialog,&QDialog::close);

    auto *index = new QWidget(stack);
    index->setObjectName("settingsPage");
    auto *indexLayout = new QVBoxLayout(index);
    indexLayout->setContentsMargins(12,38,12,15);
    indexLayout->setSpacing(22);
    stack->addWidget(scroller(stack,index));
    back->setVisible(false);
    connect(back,&QPushButton::clicked,dialog,[stack,heading,back] {
        stack->setCurrentIndex(0);
        heading->setText(QStringLiteral("Параметры"));
        back->setVisible(false);
    });
    QVBoxLayout *groupLayout = nullptr;
    QLabel *themeHint = nullptr;
    QList<QVBoxLayout *> pages;
    for (int i = 0; i < int(categories.size()); ++i) {
        const auto &category = categories[i];
        if (i<2) {
            auto *group = new QWidget(index);
            group->setObjectName("settingsGroup");
            groupLayout = new QVBoxLayout(group);
            groupLayout->setContentsMargins(1,1,1,1);
            groupLayout->setSpacing(0);
            indexLayout->addWidget(group);
        }
        auto *row = new QPushButton(index);
        row->setObjectName("settingsRow");
        row->setProperty("last", i == 0 || i+1 == int(categories.size()));
        const auto title = QString::fromUtf8(category.title);
        row->setAccessibleName(title);
        row->setCursor(Qt::PointingHandCursor);
        row->setMinimumHeight(i == 0 || i+1 == int(categories.size()) ? 64 : 55);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(13,5,13,5);
        rowLayout->setSpacing(11);
        auto *image = new QLabel(row);
        image->setProperty("categoryMark", int(category.mark));
        image->setPixmap(markIcon(category.mark,tint).pixmap(20));
        image->setAttribute(Qt::WA_TransparentForMouseEvents);
        rowLayout->addWidget(image);
        auto *column = new QVBoxLayout;
        column->setSpacing(3);
        column->setContentsMargins(0,0,0,0);
        auto *label = settingLabel(row,title,"settingsRowTitle");
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
        column->addWidget(label);
        auto hintText = QString::fromUtf8(category.hint);
        if (i==5)
            hintText = theme == "dark" ? QStringLiteral("Тёмная") : theme == "light" ? QStringLiteral("Светлая") : QStringLiteral("Системная");
        auto *hint = settingLabel(row,hintText,"settingsRowHint");
        if (i == 5) themeHint = hint;
        hint->setWordWrap(true);
        hint->setAttribute(Qt::WA_TransparentForMouseEvents);
        column->addWidget(hint);
        rowLayout->addLayout(column,1);
        auto *arrow = new QLabel(row);
        arrow->setProperty("categoryArrow", true);
        arrow->setPixmap(markIcon(Mark::Back,tint).pixmap(16).transformed(QTransform().scale(-1,1)));
        arrow->setAttribute(Qt::WA_TransparentForMouseEvents);
        rowLayout->addWidget(arrow);
        groupLayout->addWidget(row);

        auto *page = new QWidget(stack);
        page->setObjectName("settingsPage");
        auto *pageLayout = new QVBoxLayout(page);
        pageLayout->setAlignment(Qt::AlignTop);
        pageLayout->setContentsMargins(17,25,17,17);
        pages.append(pageLayout);
        stack->addWidget(scroller(stack,page));
        connect(row,&QPushButton::clicked,dialog,[stack,heading,back,i,title] {
            stack->setCurrentIndex(i+1);
            heading->setText(title);
            back->setVisible(true);
        });
    }
    indexLayout->addStretch();

    const auto info = [&](int page, const QString &text) {
        auto *label = settingLabel(dialog,text,"settingsHint");
        label->setWordWrap(true);
        pages[page]->addWidget(label);
    };
    const auto action = [&](int page, const QString &text, auto callback) {
        auto *button = settingButton(dialog,text);
        pages[page]->addWidget(button);
        connect(button,&QPushButton::clicked,dialog,callback);
    };
    const auto check = [&](int page, const QString &label, const QString &key, bool fallback) {
        auto *box = new QCheckBox(label,dialog);
        box->setChecked(QSettings().value(key,fallback).toBool());
        pages[page]->addWidget(box);
        connect(box,&QCheckBox::toggled,dialog,[this,key](bool checked) {
            QSettings().setValue(key,checked);
            reloadPreferences();
        });
    };
    const auto combo = [&](int page, const QString &label, const QStringList &names, const QVariantList &values,
        const QString &key, const QVariant &fallback) {
        auto *form = new QFormLayout;
        auto *box = new QComboBox(dialog);
        for (int i=0; i<names.size(); ++i)
            box->addItem(names[i],values[i]);
        box->setCurrentIndex(qMax(0,box->findData(QSettings().value(key,fallback))));
        form->addRow(label,box);
        pages[page]->addLayout(form);
        connect(box,&QComboBox::currentIndexChanged,dialog,[this,box,key](int) {
            const auto value = box->currentData();
            if (key == "theme")
                setTheme(value.toString());
            else
                QSettings().setValue(key,value);
            reloadPreferences();
        });
        return box;
    };

    const auto account = QSettings().value("dav/user").toString();
    const auto address = QSettings().value("dav/address").toString();
    info(0, account.isEmpty() ? QStringLiteral("Подключите Baïkal, чтобы загрузить и обновлять списки задач.")
        : QStringLiteral("%1\n%2\nЛокальная копия задач доступна без интернета.").arg(account,address));
    action(0, account.isEmpty() ? QStringLiteral("Подключить Baïkal…") : QStringLiteral("Настроить и обновить Baïkal…"),
        [this,dialog] { dialog->accept(); if (!syncing && !writing) authorize({}, true, true); });

    combo(1,QStringLiteral("При запуске"),{QStringLiteral("Входящие"),QStringLiteral("Сегодня"),QStringLiteral("Предстоящие"),
          QStringLiteral("Метки"),QStringLiteral("Закреплённые"),QStringLiteral("Завершённые")},
          {int(Page::Inbox),int(Page::Today),int(Page::Scheduled),int(Page::Labels),int(Page::Pins),int(Page::Completed)},
          "general/home",int(Page::Inbox));
    check(2,QStringLiteral("Запоминать размер и положение окна"),"general/rememberWindow",false);
    check(2,QStringLiteral("Подтверждать удаление задачи"),"general/confirmDelete",true);
    combo(3,QStringLiteral("Приоритет новой задачи"),{QStringLiteral("Обычный"),QStringLiteral("Высокий"),
          QStringLiteral("Средний"),QStringLiteral("Низкий")},{0,1,2,3},"tasks/defaultPriority",0);
    check(3,QStringLiteral("Показывать завершённые в разделе «Сегодня»"),"tasks/showDoneToday",false);
    check(4,QStringLiteral("Показывать локальные проекты"),"sidebar/local",true);
    check(4,QStringLiteral("Показывать «Закреплённые»"),"sidebar/pins",false);
    check(4,QStringLiteral("Показывать «Завершённые»"),"sidebar/completed",false);
    check(4,QStringLiteral("Показывать счётчики"),"sidebar/counts",true);
    auto *themeCombo = combo(5,QStringLiteral("Тема"),{QStringLiteral("Системная"),QStringLiteral("Светлая"),QStringLiteral("Тёмная")},
          {"system","light","dark"},"theme","system");
    connect(themeCombo,&QComboBox::currentIndexChanged,dialog,[this,dialog,themeCombo,themeHint] {
        themeHint->setText(themeCombo->currentText());
        const auto color = palette().color(QPalette::WindowText);
        for (auto *label : dialog->findChildren<QLabel *>()) {
            const auto mark = label->property("categoryMark");
            if (mark.isValid())
                label->setPixmap(markIcon(Mark(mark.toInt()),color).pixmap(20));
            if (label->property("categoryArrow").toBool())
                label->setPixmap(markIcon(Mark::Back,color).pixmap(16).transformed(QTransform().scale(-1,1)));
        }
    });

    info(6,QStringLiteral("Импортируйте календарь в формате .ics. Выбранные календари отображаются в разделах «Сегодня» и «Предстоящие»."));
    auto *calendarSources = new QWidget(dialog);
    auto *calendarLayout = new QVBoxLayout(calendarSources);
    calendarLayout->setContentsMargins(0,0,0,0);
    pages[6]->addWidget(calendarSources);
    const auto listSources = [this,calendarLayout,calendarSources,dialog] {
        while (auto *item = calendarLayout->takeAt(0)) {
            item->widget()->deleteLater();
            delete item;
        }
        const auto sources = QSettings().value("calendar/sources").toMap();
        for (auto it = sources.begin(); it != sources.end(); ++it) {
            auto *row = new QWidget(calendarSources);
            auto *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0,0,0,0);
            const auto id = it.key();
            const auto name = it.value().toString();
            auto *show = new QCheckBox(name,row);
            show->setChecked(QSettings().value("calendar/enabled/"+id,true).toBool());
            rowLayout->addWidget(show,1);
            auto *removeSource = iconButton(row,Mark::Close,QStringLiteral("Убрать календарь"));
            rowLayout->addWidget(removeSource);
            calendarLayout->addWidget(row);
            connect(show,&QCheckBox::toggled,dialog,[this,id](bool visible) {
                QSettings().setValue("calendar/enabled/"+id,visible);
                loadEvents();
                refreshEvents();
            });
            connect(removeSource,&QPushButton::clicked,dialog,[this,dialog,row,id] {
                QSettings settings;
                auto entries = settings.value("calendar/sources").toMap();
                entries.remove(id);
                settings.setValue("calendar/sources",entries);
                settings.remove("calendar/enabled/"+id);
                QFile::remove(calendarFile(id));
                row->deleteLater();
                loadEvents();
                refreshEvents();
            });
        }
    };
    listSources();
    action(6,QStringLiteral("Добавить календарь .ics…"),[this,dialog,listSources] {
        const auto path = QFileDialog::getOpenFileName(dialog,QStringLiteral("Добавить календарь"),{},QStringLiteral("Календарь (*.ics)"));
        if (path.isEmpty()) return;
        auto calendar = readCalendar(path);
        if (!calendar || !icalcomponent_get_first_component(calendar.get(),ICAL_VEVENT_COMPONENT)) {
            QMessageBox::warning(dialog,QStringLiteral("Календарь"),QStringLiteral("Файл не содержит поддерживаемых событий."));
            return;
        }
        QSettings settings;
        auto entries = settings.value("calendar/sources").toMap();
        if (entries.size()>=30 || !QDir().mkpath(calendarDirectory())) {
            QMessageBox::warning(dialog,QStringLiteral("Календарь"),QStringLiteral("Не удалось создать место для календаря."));
            return;
        }
        const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (!QFile::copy(path,calendarFile(id))) {
            QMessageBox::warning(dialog,QStringLiteral("Календарь"),QStringLiteral("Не удалось скопировать файл календаря."));
            return;
        }
        entries.insert(id,QFileInfo(path).completeBaseName());
        settings.setValue("calendar/sources",entries);
        listSources();
        loadEvents();
        refreshEvents();
    });

    check(7,QStringLiteral("Включить клавишу быстрого добавления"),"quick/enabled",true);
    combo(7,QStringLiteral("Сочетание"),{"A","Ctrl+Shift+A"},{false,true},"quick/control",false);
    info(7,QStringLiteral("Сочетание работает, когда открыто окно приложения. Ctrl+N тоже создаёт задачу."));

    info(8,QStringLiteral("Копия содержит проекты и задачи. Импорт добавляет отсутствующие записи и сохраняет текущие задачи."));
    action(8,QStringLiteral("Создать резервную копию…"),[this,dialog] {
        if (!mayDiscard()) return;
        const auto path = QFileDialog::getSaveFileName(dialog,QStringLiteral("Сохранить резервную копию"),
            QStringLiteral("plan-tasks-backup.sqlite"),QStringLiteral("Резервная копия (*.sqlite)"));
        if (path.isEmpty()) return;
        model->clear();
        for (auto *column : boardModels) column->clear();
        try {
            store.backup(path);
            QMessageBox::information(dialog,QStringLiteral("Резервные копии"),QStringLiteral("Резервная копия сохранена."));
        } catch (const std::exception &e) {
            QMessageBox::warning(dialog,QStringLiteral("Резервные копии"),QString::fromUtf8(e.what()));
        }
        refresh();
    });
    action(8,QStringLiteral("Импортировать резервную копию…"),[this,dialog] {
        if (!mayDiscard()) return;
        const auto path = QFileDialog::getOpenFileName(dialog,QStringLiteral("Импортировать резервную копию"),{},
            QStringLiteral("Резервная копия (*.sqlite)"));
        if (path.isEmpty()) return;
        model->clear();
        for (auto *column : boardModels) column->clear();
        try {
            store.importBackup(path);
            refreshNavigation();
            QMessageBox::information(dialog,QStringLiteral("Резервные копии"),QStringLiteral("Резервная копия импортирована."));
        } catch (const std::exception &e) {
            QMessageBox::warning(dialog,QStringLiteral("Резервные копии"),QString::fromUtf8(e.what()));
        }
        refresh();
    });

    info(9,QStringLiteral("Проект покажет, как создавать, группировать и завершать задачи."));
    action(9,QStringLiteral("Создать обучающий проект"),[this,dialog] {
        try {
            auto project = store.project(QSettings().value("general/tutorialProject").toString());
            if (project.id.isEmpty()) {
                project.id = store.addProject(QStringLiteral("Знакомство с приложением"));
                QSettings().setValue("general/tutorialProject",project.id);
                const auto section = store.addSection(project.id,QStringLiteral("Первые шаги"));
                for (const auto &label : {QStringLiteral("Добавьте задачу кнопкой «+»"),
                    QStringLiteral("Назначьте задаче дату или метку"),QStringLiteral("Отметьте задачу выполненной")}) {
                    Task task;
                    task.project = project.id;
                    task.section = section;
                    task.title = label;
                    store.saveTask(task);
                }
            }
            dialog->accept();
            refreshNavigation();
            selectPage(Page::Project,project.id);
        } catch (const std::exception &e) {
            QMessageBox::warning(dialog,QStringLiteral("Обучающий проект"),QString::fromUtf8(e.what()));
        }
    });
    return dialog;
}

void Window::showPreferences()
{
    std::unique_ptr<QDialog> dialog(makePreferences());
    dialog->exec();
}

void Window::loadEvents()
{
    calendarEvents.clear();
    const QSettings settings;
    const auto sources = settings.value("calendar/sources").toMap();
    const auto today = QDate::currentDate();
    auto *utc = icaltimezone_get_utc_timezone();
    const auto start = icaltime_from_timet_with_zone(today.startOfDay().toSecsSinceEpoch(),0,utc);
    const auto end = icaltime_from_timet_with_zone(today.addDays(91).startOfDay().toSecsSinceEpoch(),0,utc);
    for (auto it = sources.begin(); it != sources.end(); ++it) {
        if (!settings.value("calendar/enabled/"+it.key(),true).toBool())
            continue;
        auto calendar = readCalendar(calendarFile(it.key()));
        if (!calendar) continue;
        int count = 0;
        for (auto *event = icalcomponent_get_first_component(calendar.get(),ICAL_VEVENT_COMPONENT); event && count++ < 3000;
             event = icalcomponent_get_next_component(calendar.get(),ICAL_VEVENT_COMPONENT)) {
            if (icalcomponent_get_status(event) == ICAL_STATUS_CANCELLED)
                continue;
            const auto title = QString::fromUtf8(icalcomponent_get_summary(event)).simplified().left(160);
            if (title.isEmpty()) continue;
            if (auto *property = icalcomponent_get_first_property(event,ICAL_RRULE_PROPERTY)) {
                const auto rule = icalproperty_get_rrule(property);
                if (rule.freq < ICAL_DAILY_RECURRENCE || rule.by_second[0] != ICAL_RECURRENCE_ARRAY_MAX ||
                    rule.by_minute[0] != ICAL_RECURRENCE_ARRAY_MAX || rule.by_hour[0] != ICAL_RECURRENCE_ARRAY_MAX)
                {
                    icalcomponent_remove_property(event,property);
                    icalproperty_free(property);
                }
            }
            struct Occurrences {
                QMap<QDate,QStringList> &days;
                QString title;
                bool allDay;
            } entries{calendarEvents,title, bool(icaltime_is_date(icalcomponent_get_dtstart(event)))};
            icalcomponent_foreach_recurrence(event,start,end,[](icalcomponent *, icaltime_span *span, void *data) {
                auto &occurrences = *static_cast<Occurrences *>(data);
                const auto day = occurrences.allDay
                    ? QDateTime::fromSecsSinceEpoch(span->start,QTimeZone::utc()).date()
                    : QDateTime::fromSecsSinceEpoch(span->start).date();
                auto &list = occurrences.days[day];
                if (list.size() < 100)
                    list.append(occurrences.title);
            },&entries);
        }
    }
}

void Window::refreshEvents()
{
    const bool today = filter.page == Page::Today;
    if ((!today && filter.page != Page::Scheduled) || !filter.search.isEmpty() || calendarEvents.isEmpty()) {
        eventList->hide();
        return;
    }
    QStringList visible;
    const auto current = QDate::currentDate();
    for (auto it = calendarEvents.lowerBound(current); it != calendarEvents.end(); ++it) {
        if (today && it.key()!=current) break;
        for (const auto &name : it.value())
            visible.append(today ? name : QLocale().toString(it.key(),QLocale::ShortFormat) + "  ·  " + name);
    }
    eventList->setVisible(!visible.isEmpty());
    eventList->setFixedHeight(qMin(125, 29 + 21 * (visible.size() > 4 ? 5 : int(visible.size()))));
    eventList->setPlainText(QStringLiteral("События календаря\n") + visible.join('\n'));
}
