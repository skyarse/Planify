#pragma once

#include "caldav.h"
#include "store.h"

#include <QMainWindow>
#include <QMap>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QSet>
#include <QThreadPool>
#include <optional>

class QAction;
class QCheckBox;
class QCloseEvent;
class QHBoxLayout;
class QDialog;
class QShortcut;
class QComboBox;
class QDateEdit;
class QDateTimeEdit;
class QStackedWidget;
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
    bool selectingTasks = false;
    bool syncing = false;
    struct PasswordChange {
        QUrl address;
        QString user;
        QString password;
    };
    std::optional<PasswordChange> pendingPassword;
    QThreadPool credentialWorker;
    QSet<QPair<QUrl, QString>> triedCredentials;
    bool systemDark = false;
    QString theme;
    QMap<QDate, QStringList> calendarEvents;
    QComboBox *labels;
    QListWidget *projects;
    QLineEdit *search;
    QLabel *pageTitle;
    QLabel *pageIcon;
    QLabel *projectNote;
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
    QStackedWidget *cardFields;
    QPushButton *completion;
    QPushButton *completedDate;
    QPushButton *dateButton;
    QPushButton *composerDate;
    QPushButton *closeEditor;
    QWidget *composerDivider;
    QPushButton *priorityButton;
    QPushButton *reminderButton;
    QDateTimeEdit *reminder;
    QAction *templateAction;
    QAction *moveAction;
    QList<QWidget *> editableFields;
    QDateTime lastReminderCheck;
    QSqlQueryModel *model;
    QList<QSqlQueryModel *> boardModels;
    QList<QListView *> boardViews;
    QDialog *composer;
    QHBoxLayout *composerFooter;
    QHBoxLayout *cardToolbar;
    QPushButton *destination;
    QLabel *composerStatus;
    QLabel *composerHeading;
    QWidget *composerShade;
    QString creationToken;
    QWidget *parking;
    QPointer<QListView> expandedView;
    QPersistentModelIndex expandedIndex;
    bool cardOpen = false;
    bool writing = false;
    QString writingId;
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
    QCheckBox *pinned;
    QListWidget *subtasks;
    QPushButton *addSubtask;
    QPushButton *save;

    void setTheme(const QString &option);
    void selectPage(Page page, const QString &id = {});
    void refreshNavigation();
    void refreshCounts();
    void reloadPreferences();
    void refreshEvents();
    void loadEvents();
    void addLocalProject();
    void showProjectMenu(const QString &id, const QPoint &position, bool header);
    void showViewMenu(QWidget *anchor);
    void editProject(const Project &owner, bool deadlineOnly = false);
    void addProjectSection(const QString &projectId);
    void manageProjectSections(const Project &owner);
    void chooseArchivedProject();
    void exportProjectPdf(const Project &owner);
    QString projectText(const Project &owner);
    void updateProject(Project owner);
    void updateProjectNote(const Project &owner);
    void loadProjectView();
    void setBoard(bool enabled);
    void setTaskSelection(bool enabled);
    void copySelectedTasks();
    void showPreferences();
    void refresh();
    void rebuildBoard();
    QListView *createTaskList(QWidget *parent, QSqlQueryModel *taskModel);
    void openTask(const QString &id);
    void editTask(const Task &task);
    void newTask(const QString &parent = {});
    void chooseDestination();
    void updateDestination();
    void taskCreated(const Task &task);
    void reloadSections(const QString &projectId, const QString &selected = {});
    void reloadSubtasks();
    void saveTask();
    void mountCard();
    void unmountCard();
    void closeCard();
    void resizeCard();
    void fitComposer();
    void updateCardActions();
    void showCardField(QWidget *field);
    void showAttachments(QWidget *anchor);
    void duplicateTask();
    bool authorize(const Project &owner, bool refreshLists = false, bool configure = false);
    void saveConfirmedPassword();
    bool writeRemote(const Task &before, const Task &draft);
    void refreshAfterMutation(const QString &id);
    void checkReminders();
    void toggleTask(const QString &id);
    bool mayDiscard();
    void showSync();
    void message(const QString &text);
};
