#include "window.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QDialog>
#include <QSettings>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QDebug>
#include <memory>
#include <stdexcept>

namespace {
void seedDemo(Store &store)
{
    const auto work = store.addProject(QStringLiteral("Работа"));
    const auto personal = store.addProject(QStringLiteral("Личное"));
    store.addProject(QStringLiteral("Учёба"));
    const auto weekly = store.addSection(work, QStringLiteral("На этой неделе"));
    const auto later = store.addSection(work, QStringLiteral("Позже"));
    const auto today = QDate::currentDate();
    Task task;
    task.project = work;
    task.section = weekly;
    task.title = QStringLiteral("Разобрать задачи на неделю");
    task.notes = QStringLiteral("Проверить сроки и расставить приоритеты.");
    task.tags = QStringLiteral("Планирование");
    task.due = today;
    task.priority = 1;
    const auto parent = store.saveTask(task);
    task = Task{};
    task.project = work;
    task.section = weekly;
    task.parent = parent;
    task.title = QStringLiteral("Проверить входящие");
    task.due = today;
    store.saveTask(task);
    task = Task{};
    task.project = work;
    task.section = later;
    task.title = QStringLiteral("Подготовить материалы для встречи");
    task.due = today.addDays(2);
    task.priority = 2;
    store.saveTask(task);
    task = Task{};
    task.project = personal;
    task.title = QStringLiteral("Забрать книги");
    task.due = today.addDays(5);
    task.pinned = true;
    store.saveTask(task);
    task = Task{};
    task.project = Store::inbox();
    task.title = QStringLiteral("Проверить расписание");
    task.due = today;
    store.saveTask(task);
    task.title = QStringLiteral("Записать идею для проекта");
    task.due = {};
    store.saveTask(task);
    task.title = QStringLiteral("Собрать ссылки");
    store.saveTask(task);
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("plan-tasks");
    QApplication::setDesktopFileName("local.plan.tasks");
    QApplication::setApplicationDisplayName(QStringLiteral("План задач"));
    QApplication::setOrganizationName("plan-tasks");
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"demo", "Запустить с демонстрационными данными"});
    parser.addOption({"smoke-test", "Выполнить проверку интерфейса"});
    parser.addOption({"screenshot", "Сохранить снимок окна", "path"});
    parser.addOption({"preferences", "Открыть параметры при запуске"});
    parser.process(app);

    const bool demo = parser.isSet("demo");
    const bool smoke = parser.isSet("smoke-test");
    try {
        std::unique_ptr<QTemporaryDir> temporary;
        if (demo || smoke)
            temporary = std::make_unique<QTemporaryDir>();
        const auto root = temporary ? temporary->path() : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if ((temporary && !temporary->isValid()) || !QDir().mkpath(root))
            throw std::runtime_error("Не удалось создать каталог базы данных");
        if (smoke) {
            qputenv("XDG_DATA_HOME", root.toUtf8());
            QSettings::setDefaultFormat(QSettings::IniFormat);
            QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root);
        }
        Store store(root + "/tasks.sqlite");
        if (demo)
            seedDemo(store);
        Window window(store);
        window.show();
        std::unique_ptr<QDialog> preferences;
        if (parser.isSet("preferences")) {
            preferences.reset(window.makePreferences());
            preferences->show();
        }
        if (smoke)
            QTimer::singleShot(0, &app, [&] { app.exit(window.smokeTest() ? 0 : 1); });
        else if (parser.isSet("screenshot"))
            QTimer::singleShot(150, &app, [&] {
                const auto snapshot = preferences ? preferences->grab() : window.grab();
                app.exit(snapshot.save(parser.value("screenshot")) ? 0 : 1);
            });
        return app.exec();
    } catch (const std::exception &error) {
        if (smoke || parser.isSet("screenshot"))
            qCritical().noquote() << QString::fromUtf8(error.what());
        else
            QMessageBox::critical(nullptr, QStringLiteral("План задач"), QString::fromUtf8(error.what()));
        return 1;
    }
}
