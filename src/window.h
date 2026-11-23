#pragma once

#include "caldav.h"
#include "store.h"

#include <QMainWindow>
#include <QMap>
#include <optional>

class QCheckBox;
class QCloseEvent;
class QDialog;
class QShortcut;
class QComboBox;
class QDateEdit;
class QLabel;
class QLineEdit;
class QListView;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSqlQueryModel;
class QVBoxLayout;

class Window : public QMainWindow {
public:
    explicit Window(Store &store, QWidget *parent = nullptr);
    bool smokeTest();
    QDialog *makePreferences();

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    Store &store;
    CalDav caldav;
    Filter filter;
    Task current;
    bool loading = false;
    bool dirty = false;
    bool board = false;
    bool syncing = false;
    bool systemDark = false;
    QString theme;
    QMap<QDate, QStringList> calendarEvents;
    QComboBox *labels;
    QListWidget *projects;
    QLineEdit *search;
    QLabel *pageTitle;
    QLabel *pageIcon;
    QLabel *emptyTitle;
    QLabel *emptyHint;
    QPlainTextEdit *eventList;
    QWidget *sidebar;
    QWidget *frame;
    QWidget *localHeading;
    QShortcut *quickShortcut;
    QLabel *status;
    QWidget *empty;
    QListView *list;
    QWidget *boardArea;
    QWidget *editor;
    QLabel *editorHeading;
    QSqlQueryModel *model;
    QList<QSqlQueryModel *> boardModels;
    QPushButton *syncButton;
    QPushButton *boardButton;
    QPushButton *addSection;
    QList<QPushButton *> navigation;
    QLineEdit *title;
    QPlainTextEdit *notes;
    QComboBox *project;
    QComboBox *section;
    QComboBox *priority;
    QComboBox *repeat;
    QLineEdit *tags;
    QDateEdit *due;
    QDateEdit *deadline;
    QList<QWidget *> dateRows;
    QCheckBox *pinned;
    QListWidget *subtasks;
    QPushButton *addSubtask;
    QPushButton *save;
    QPushButton *remove;

    void setTheme(const QString &option);
    void selectPage(Page page, const QString &id = {});
    void refreshNavigation();
    void refreshCounts();
    void reloadPreferences();
    void refreshEvents();
    void loadEvents();
    void addLocalProject();
    void showPreferences();
    void refresh();
    void rebuildBoard();
    QListView *createTaskList(QWidget *parent, QSqlQueryModel *taskModel);
    void openTask(const QString &id);
    void editTask(const Task &task);
    void newTask(const QString &parent = {});
    void reloadSections(const QString &projectId, const QString &selected = {});
    void reloadSubtasks();
    void saveTask();
    void toggleTask(const QString &id);
    bool mayDiscard();
    void showSync();
    void message(const QString &text);
};
