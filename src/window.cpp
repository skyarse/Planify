#include "window.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCursor>
#include <QLocale>
#include <QSignalBlocker>
#include <QSqlQuery>
#include <stdexcept>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QStyleHints>
#include <QShortcut>
#include <QSqlError>
#include <QSqlQueryModel>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QVBoxLayout>
#include <array>
#include <functional>

namespace {
struct NavItem {
    Page page;
    const char *title;
    const char *icon;
    const char *color;
};

constexpr std::array items{
    NavItem{Page::Inbox, "Входящие", "▣", "#4799ee"},
    NavItem{Page::Today, "Сегодня", "☆", "#3ebc82"},
    NavItem{Page::Scheduled, "Предстоящие", "▦", "#b77adb"},
    NavItem{Page::Pins, "Закреплённые", "◆", "#ee747c"},
    NavItem{Page::Labels, "Метки", "◇", "#c69a71"},
    NavItem{Page::Completed, "Завершённые", "✓", "#ef9a48"},
};

const QDate noDate(1900, 1, 1);

QPushButton *button(const QString &text, QWidget *parent, const QString &name = {})
{
    auto *result = new QPushButton(text, parent);
    result->setObjectName(name);
    result->setCursor(Qt::PointingHandCursor);
    return result;
}

QLabel *caption(const QString &text, QWidget *parent, const QString &name)
{
    auto *result = new QLabel(text, parent);
    result->setObjectName(name);
    return result;
}

void selectData(QComboBox *box, const QString &data)
{
    box->setCurrentIndex(qMax(0, box->findData(data)));
}

class TaskDelegate : public QStyledItemDelegate {
public:
    TaskDelegate(std::function<void(const QString &)> toggle, QObject *parent)
        : QStyledItemDelegate(parent), toggle(std::move(toggle)) {}

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return {0, 55};
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const auto value = [&](int column) {
            return index.siblingAtColumn(column).data();
        };
        const bool done = value(2).toBool();
        const int priority = value(3).toInt();
        const bool child = !value(6).toString().isEmpty();
        const auto palette = option.widget ? option.widget->window()->palette() : option.palette;
        const auto rect = option.rect.adjusted(1, 1, -1, -2);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        QColor background = palette.color(QPalette::AlternateBase);
        if (option.state & QStyle::State_Selected)
            background = palette.color(QPalette::Highlight);
        else if (option.state & QStyle::State_MouseOver)
            background = palette.color(QPalette::Midlight);
        painter->setPen(Qt::NoPen);
        painter->setBrush(background);
        painter->drawRoundedRect(rect, 7, 7);

        QColor accent = palette.color(QPalette::PlaceholderText);
        if (priority == 1) accent = QColor("#ed727a");
        if (priority == 2) accent = QColor("#e19a52");
        if (priority == 3) accent = QColor("#559ce3");
        const QRect check(rect.left() + (child ? 26 : 12), rect.top() + 19, 17, 17);
        painter->setPen(QPen(done ? QColor("#4fb388") : accent, 1.6));
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(check);
        if (done) {
            painter->drawLine(check.topLeft() + QPoint(4, 8), check.topLeft() + QPoint(7, 11));
            painter->drawLine(check.topLeft() + QPoint(7, 11), check.topLeft() + QPoint(13, 5));
        }

        const int left = check.right() + 13;
        auto font = option.font;
        font.setBold(!done);
        font.setStrikeOut(done);
        painter->setFont(font);
        auto name = value(1).toString();
        if (value(8).toBool())
            name.prepend(QStringLiteral("◆  "));
        painter->setPen(palette.color(done ? QPalette::PlaceholderText : QPalette::Text));
        painter->drawText(QRect(left, rect.top() + 7, rect.right() - left - 10, 20), Qt::AlignVCenter,
            QFontMetrics(font).elidedText(name, Qt::ElideRight, rect.right() - left - 10));
        font.setBold(false);
        font.setStrikeOut(false);
        font.setPointSizeF(qMax(8.0, font.pointSizeF() - 1.0));
        painter->setFont(font);
        const auto date = QDate::fromString(value(4).toString(), Qt::ISODate);
        const auto deadline = QDate::fromString(value(9).toString(), Qt::ISODate);
        QString details = value(5).toString();
        if (date.isValid())
            details += QStringLiteral("  ·  ") + QLocale().toString(date, QLocale::ShortFormat);
        if (deadline.isValid())
            details += QStringLiteral("  ·  Срок ") + QLocale().toString(deadline, QLocale::ShortFormat);
        if (!value(7).toString().isEmpty())
            details += QStringLiteral("  ·  Baïkal");
        painter->setPen(palette.color(QPalette::PlaceholderText));
        painter->drawText(QRect(left, rect.top() + 29, rect.right() - left - 10, 17), Qt::AlignVCenter,
            QFontMetrics(font).elidedText(details, Qt::ElideRight, rect.right() - left - 10));
        painter->restore();
    }

    bool editorEvent(QEvent *event, QAbstractItemModel *, const QStyleOptionViewItem &option,
                     const QModelIndex &index) override
    {
        if (event->type() != QEvent::MouseButtonRelease)
            return false;
        const auto *mouse = static_cast<QMouseEvent *>(event);
        const bool child = !index.siblingAtColumn(6).data().toString().isEmpty();
        const QRect hit(option.rect.left() + (child ? 23 : 8), option.rect.top() + 14, 26, 26);
        if (mouse->button() != Qt::LeftButton || !hit.contains(mouse->position().toPoint()))
            return false;
        const auto id = index.siblingAtColumn(0).data().toString();
        QTimer::singleShot(0, this, [this, id] { toggle(id); });
        return true;
    }

private:
    std::function<void(const QString &)> toggle;
};
}

Window::Window(Store &db, QWidget *parent) : QMainWindow(parent), store(db)
{
    setWindowTitle(QStringLiteral("План задач"));
    resize(1120, 740);
    setMinimumSize(720, 480);
    systemDark = QApplication::palette().color(QPalette::Window).lightness() < 128;

    auto *root = new QWidget(this);
    root->setObjectName("windowRoot");
    setCentralWidget(root);
    auto *layout = new QHBoxLayout(root);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *sidebar = new QWidget(root);
    sidebar->setObjectName("sidebar");
    sidebar->setFixedWidth(266);
    auto *left = new QVBoxLayout(sidebar);
    left->setContentsMargins(13, 9, 13, 14);
    left->setSpacing(12);
    auto *top = new QHBoxLayout;
    top->addStretch();
    auto *findButton = button(QStringLiteral("⌕"), sidebar, "tool");
    findButton->setAccessibleName(QStringLiteral("Поиск"));
    findButton->setToolTip(QStringLiteral("Поиск (Ctrl+F)"));
    top->addWidget(findButton);
    auto *menuButton = button(QStringLiteral("≡"), sidebar, "tool");
    menuButton->setAccessibleName(QStringLiteral("Меню"));
    top->addWidget(menuButton);
    left->addLayout(top);
    auto *menu = new QMenu(menuButton);
    menuButton->setMenu(menu);
    menu->addAction(QStringLiteral("Импорт из Baïkal…"), this, &Window::showSync);
    auto *appearance = menu->addMenu(QStringLiteral("Тема"));
    appearance->addAction(QStringLiteral("Системная"), this, [this] { setTheme("system"); });
    appearance->addAction(QStringLiteral("Светлая"), this, [this] { setTheme("light"); });
    appearance->addAction(QStringLiteral("Тёмная"), this, [this] { setTheme("dark"); });

    auto *grid = new QGridLayout;
    grid->setSpacing(7);
    for (int i = 0; i < int(items.size()); ++i) {
        const auto &item = items[i];
        auto *tile = button(QString::fromUtf8(item.icon) + "\n" + QString::fromUtf8(item.title), sidebar, "nav");
        tile->setAccessibleName(QString::fromUtf8(item.title));
        tile->setCheckable(true);
        connect(tile, &QPushButton::clicked, this, [this, page = item.page] { selectPage(page); });
        grid->addWidget(tile, i / 2, i % 2);
        navigation.append(tile);
    }
    left->addLayout(grid);

    auto *projectHeader = new QHBoxLayout;
    projectHeader->addWidget(caption(QStringLiteral("Проекты"), sidebar, "strong"));
    projectHeader->addStretch();
    auto *addProject = button(QStringLiteral("+"), sidebar, "tool");
    addProject->setAccessibleName(QStringLiteral("Новый проект"));
    addProject->setToolTip(addProject->accessibleName());
    projectHeader->addWidget(addProject);
    left->addLayout(projectHeader);
    projects = new QListWidget(sidebar);
    projects->setObjectName("projects");
    projects->setAccessibleName(QStringLiteral("Проекты"));
    left->addWidget(projects, 1);
    syncButton = button(QStringLiteral("↻  Baïkal"), sidebar, "sync");
    syncButton->setToolTip(QStringLiteral("Обновить локальную копию задач Baïkal"));
    left->addWidget(syncButton);
    layout->addWidget(sidebar);

    auto *center = new QWidget(root);
    auto *body = new QVBoxLayout(center);
    body->setContentsMargins(19, 11, 16, 15);
    body->setSpacing(10);
    auto *toolbar = new QHBoxLayout;
    pageTitle = caption({}, center, "pageTitle");
    toolbar->addWidget(pageTitle, 1);
    labels = new QComboBox(center);
    labels->setAccessibleName(QStringLiteral("Фильтр по метке"));
    labels->hide();
    toolbar->addWidget(labels);
    addSection = button(QStringLiteral("+ Секция"), center, "secondary");
    addSection->hide();
    toolbar->addWidget(addSection);
    boardButton = button(QStringLiteral("Доска"), center, "secondary");
    boardButton->hide();
    toolbar->addWidget(boardButton);
    body->addLayout(toolbar);
    search = new QLineEdit(center);
    search->setClearButtonEnabled(true);
    search->setPlaceholderText(QStringLiteral("Поиск по задачам"));
    search->setAccessibleName(QStringLiteral("Поиск по задачам"));
    search->hide();
    body->addWidget(search);

    model = new QSqlQueryModel(this);
    list = createTaskList(center, model);
    body->addWidget(list, 1);
    boardArea = new QScrollArea(center);
    boardArea->setObjectName("board");
    boardArea->hide();
    body->addWidget(boardArea, 1);
    empty = caption(QStringLiteral("Задач пока нет"), center, "empty");
    empty->setAlignment(Qt::AlignCenter);
    body->addWidget(empty, 1);

    auto *footer = new QHBoxLayout;
    status = caption({}, center, "muted");
    status->setWordWrap(true);
    footer->addWidget(status, 1);
    auto *addTask = button(QStringLiteral("+"), center, "addTask");
    addTask->setAccessibleName(QStringLiteral("Создать задачу"));
    addTask->setToolTip(QStringLiteral("Создать задачу (Ctrl+N)"));
    footer->addWidget(addTask, 0, Qt::AlignBottom);
    body->addLayout(footer);
    layout->addWidget(center, 1);

    auto *editorScroll = new QScrollArea(root);
    editor = editorScroll;
    editor->setObjectName("editor");
    editor->setFixedWidth(319);
    editorScroll->setWidgetResizable(true);
    auto *panel = new QWidget(editor);
    auto *details = new QVBoxLayout(panel);
    details->setContentsMargins(14, 9, 14, 14);
    details->setSpacing(9);
    auto *editorHeader = new QHBoxLayout;
    editorHeading = caption({}, panel, "strong");
    editorHeader->addWidget(editorHeading, 1);
    auto *closeEditor = button(QStringLiteral("×"), panel, "tool");
    closeEditor->setAccessibleName(QStringLiteral("Закрыть карточку"));
    editorHeader->addWidget(closeEditor);
    details->addLayout(editorHeader);
    title = new QLineEdit(panel);
    title->setPlaceholderText(QStringLiteral("Название задачи"));
    title->setAccessibleName(QStringLiteral("Название задачи"));
    details->addWidget(title);
    notes = new QPlainTextEdit(panel);
    notes->setPlaceholderText(QStringLiteral("Описание"));
    notes->setAccessibleName(QStringLiteral("Описание"));
    notes->setFixedHeight(100);
    details->addWidget(notes);
    auto *form = new QFormLayout;
    project = new QComboBox(panel);
    section = new QComboBox(panel);
    priority = new QComboBox(panel);
    repeat = new QComboBox(panel);
    tags = new QLineEdit(panel);
    tags->setPlaceholderText(QStringLiteral("Через запятую"));
    due = new QDateEdit(panel);
    deadline = new QDateEdit(panel);
    for (auto *date : {due, deadline}) {
        date->setCalendarPopup(true);
        date->setMinimumDate(noDate);
        date->setSpecialValueText(QStringLiteral("Без даты"));
        date->setDisplayFormat(QStringLiteral("dd.MM.yyyy"));
    }
    priority->addItems({QStringLiteral("Обычный"), QStringLiteral("Высокий"),
                        QStringLiteral("Средний"), QStringLiteral("Низкий")});
    repeat->addItem(QStringLiteral("Без повтора"), "");
    repeat->addItem(QStringLiteral("Каждый день"), "daily");
    repeat->addItem(QStringLiteral("Каждую неделю"), "weekly");
    repeat->addItem(QStringLiteral("Каждый месяц"), "monthly");
    form->addRow(QStringLiteral("Проект"), project);
    form->addRow(QStringLiteral("Секция"), section);
    for (auto *date : {due, deadline}) {
        auto *row = new QWidget(panel);
        auto *buttons = new QHBoxLayout(row);
        buttons->setContentsMargins(0, 0, 0, 0);
        buttons->setSpacing(3);
        buttons->addWidget(date, 1);
        auto *today = button(QStringLiteral("◉"), row, "tool");
        today->setAccessibleName(QStringLiteral("Выбрать сегодня"));
        today->setToolTip(today->accessibleName());
        buttons->addWidget(today);
        auto *clear = button(QStringLiteral("×"), row, "tool");
        clear->setAccessibleName(QStringLiteral("Очистить дату"));
        clear->setToolTip(clear->accessibleName());
        buttons->addWidget(clear);
        connect(today, &QPushButton::clicked, date, [date] { date->setDate(QDate::currentDate()); });
        connect(clear, &QPushButton::clicked, date, [date] { date->setDate(noDate); });
        form->addRow(date == due ? QStringLiteral("Дата") : QStringLiteral("Срок"), row);
        dateRows.append(row);
    }
    form->addRow(QStringLiteral("Приоритет"), priority);
    form->addRow(QStringLiteral("Повтор"), repeat);
    form->addRow(QStringLiteral("Метки"), tags);
    details->addLayout(form);
    pinned = new QCheckBox(QStringLiteral("Закрепить задачу"), panel);
    details->addWidget(pinned);
    auto *subHeader = new QHBoxLayout;
    subHeader->addWidget(caption(QStringLiteral("Подзадачи"), panel, "strong"), 1);
    addSubtask = button(QStringLiteral("+"), panel, "tool");
    addSubtask->setAccessibleName(QStringLiteral("Добавить подзадачу"));
    subHeader->addWidget(addSubtask);
    details->addLayout(subHeader);
    subtasks = new QListWidget(panel);
    subtasks->setMaximumHeight(130);
    subtasks->setAccessibleName(QStringLiteral("Подзадачи"));
    details->addWidget(subtasks);
    details->addStretch();
    auto *actions = new QHBoxLayout;
    remove = button(QStringLiteral("Удалить"), panel, "secondary");
    save = button(QStringLiteral("Сохранить"), panel, "primary");
    actions->addWidget(remove);
    actions->addStretch();
    actions->addWidget(save);
    details->addLayout(actions);
    editorScroll->setWidget(panel);
    editor->hide();
    layout->addWidget(editor);

    const auto markDirty = [this] { if (!loading && !current.remote()) dirty = true; };
    connect(title, &QLineEdit::textChanged, this, markDirty);
    connect(notes, &QPlainTextEdit::textChanged, this, markDirty);
    connect(tags, &QLineEdit::textChanged, this, markDirty);
    connect(pinned, &QCheckBox::toggled, this, markDirty);
    for (auto *combo : {section, priority, repeat})
        connect(combo, &QComboBox::currentIndexChanged, this, markDirty);
    for (auto *date : {due, deadline})
        connect(date, &QDateEdit::dateChanged, this, markDirty);
    connect(project, &QComboBox::currentIndexChanged, this, [this, markDirty] {
        if (!loading)
            reloadSections(project->currentData().toString());
        markDirty();
    });

    connect(closeEditor, &QPushButton::clicked, this, [this] {
        if (mayDiscard())
            editor->hide();
    });
    connect(addTask, &QPushButton::clicked, this, [this] { newTask(); });
    connect(addSubtask, &QPushButton::clicked, this, [this] { newTask(current.id); });
    connect(subtasks, &QListWidget::itemDoubleClicked, this, [this](const QListWidgetItem *item) {
        openTask(item->data(Qt::UserRole).toString());
    });
    connect(save, &QPushButton::clicked, this, &Window::saveTask);
    connect(title, &QLineEdit::returnPressed, this, &Window::saveTask);
    connect(remove, &QPushButton::clicked, this, [this] {
        if (current.id.isEmpty() || current.remote())
            return;
        if (QMessageBox::question(this, QStringLiteral("Удалить задачу?"), current.title) != QMessageBox::Yes)
            return;
        try {
            store.remove(current.id);
            dirty = false;
            editor->hide();
            refreshNavigation();
            refresh();
        } catch (const std::exception &error) {
            message(QString::fromUtf8(error.what()));
        }
    });
    connect(syncButton, &QPushButton::clicked, this, &Window::showSync);
    connect(boardButton, &QPushButton::clicked, this, [this] {
        board = !board;
        refresh();
    });
    connect(findButton, &QPushButton::clicked, this, [this] {
        if (search->isVisible()) {
            search->clear();
            search->hide();
        } else {
            search->show();
            search->setFocus();
        }
    });
    auto *searchShortcut = new QShortcut(QKeySequence::Find, this);
    connect(searchShortcut, &QShortcut::activated, this, [this] { search->show(); search->setFocus(); search->selectAll(); });
    auto *newShortcut = new QShortcut(QKeySequence::New, this);
    connect(newShortcut, &QShortcut::activated, this, [this] { newTask(); });
    auto *saveShortcut = new QShortcut(QKeySequence::Save, editor);
    saveShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(saveShortcut, &QShortcut::activated, this, &Window::saveTask);

    auto *searchDelay = new QTimer(this);
    searchDelay->setSingleShot(true);
    searchDelay->setInterval(115);
    connect(search, &QLineEdit::textChanged, searchDelay, qOverload<>(&QTimer::start));
    connect(searchDelay, &QTimer::timeout, this, [this] {
        filter.search = search->text().trimmed();
        refresh();
    });
    connect(labels, &QComboBox::currentIndexChanged, this, [this] {
        filter.tag = labels->currentData().toString();
        refresh();
    });
    connect(projects, &QListWidget::itemActivated, this, [this](const QListWidgetItem *item) {
        const auto id = item->data(Qt::UserRole).toString();
        if (!id.isEmpty())
            selectPage(Page::Project, id);
    });
    connect(projects, &QListWidget::itemClicked, this, [this](const QListWidgetItem *item) {
        const auto id = item->data(Qt::UserRole).toString();
        if (!id.isEmpty())
            selectPage(Page::Project, id);
    });
    connect(addProject, &QPushButton::clicked, this, [this] {
        bool ok;
        const auto name = QInputDialog::getText(this, QStringLiteral("Новый проект"), QStringLiteral("Название"),
            QLineEdit::Normal, {}, &ok);
        if (!ok)
            return;
        try {
            const auto id = store.addProject(name);
            refreshNavigation();
            selectPage(Page::Project, id);
        } catch (const std::exception &error) {
            message(QString::fromUtf8(error.what()));
        }
    });
    connect(addSection, &QPushButton::clicked, this, [this] {
        bool ok;
        const auto name = QInputDialog::getText(this, QStringLiteral("Новая секция"), QStringLiteral("Название"),
            QLineEdit::Normal, {}, &ok);
        if (!ok)
            return;
        try {
            store.addSection(filter.project, name);
            if (editor->isVisible() && project->currentData().toString() == filter.project) {
                const auto selection = section->currentData().toString();
                loading = true;
                reloadSections(filter.project, selection);
                loading = false;
            }
            refresh();
        } catch (const std::exception &error) {
            message(QString::fromUtf8(error.what()));
        }
    });
    connect(&caldav, &CalDav::progress, this, [this](const QString &text) { status->setText(text); });
    connect(&caldav, &CalDav::failed, this, [this](const QString &error) {
        syncing = false;
        syncButton->setEnabled(true);
        message(error);
    });
    connect(&caldav, &CalDav::imported, this, [this](const QList<RemoteList> &lists) {
        syncing = false;
        syncButton->setEnabled(true);
        try {
            store.importReadOnly(lists);
            refreshNavigation();
            refresh();
            if (editor->isVisible() && current.remote()) {
                const auto update = store.task(current.id);
                if (update.id.isEmpty())
                    editor->hide();
                else
                    editTask(update);
            }
            int count = 0;
            for (const auto &entry : lists)
                count += entry.tasks.size();
            message(QStringLiteral("Baïkal · задач загружено: %1").arg(count));
        } catch (const std::exception &error) {
            message(QString::fromUtf8(error.what()));
        }
    });

    const auto chosenTheme = QSettings().value("theme", "system").toString();
    setTheme(chosenTheme);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(QApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this](Qt::ColorScheme scheme) {
        systemDark = scheme == Qt::ColorScheme::Dark;
        if (theme == "system")
            setTheme(theme);
    });
#endif
    refreshNavigation();
    selectPage(Page::Inbox);
}

void Window::setTheme(const QString &option)
{
    theme = option == "light" || option == "dark" ? option : QStringLiteral("system");
    if (!qApp->arguments().contains("--smoke-test") && !qApp->arguments().contains("--demo"))
        QSettings().setValue("theme", theme);
    const bool dark = theme == "dark" || (theme == "system" && systemDark);
    const QColor canvas(dark ? "#101116" : "#fafafa");
    const QColor side(dark ? "#191a21" : "#f1f1f4");
    const QColor card(dark ? "#1c1e25" : "#f0f1f4");
    const QColor ink(dark ? "#f0f0f4" : "#202126");
    const QColor muted(dark ? "#a6a8b5" : "#626572");
    const QColor line(dark ? "#32343e" : "#d8dbe2");
    QPalette colors = QApplication::palette();
    colors.setColor(QPalette::Window, canvas);
    colors.setColor(QPalette::WindowText, ink);
    colors.setColor(QPalette::Text, ink);
    colors.setColor(QPalette::Base, canvas);
    colors.setColor(QPalette::AlternateBase, card);
    colors.setColor(QPalette::Midlight, dark ? QColor("#272a35") : QColor("#e6e9f0"));
    colors.setColor(QPalette::Highlight, dark ? QColor("#263650") : QColor("#dce9fb"));
    colors.setColor(QPalette::HighlightedText, ink);
    colors.setColor(QPalette::PlaceholderText, muted);
    colors.setColor(QPalette::ButtonText, ink);
    colors.setColor(QPalette::Button, card);
    QApplication::setPalette(colors);
    setPalette(colors);
    auto css = QStringLiteral(R"CSS(
        QLabel, QCheckBox, QListWidget#projects, QPushButton { color: %2; }
        QWidget#windowRoot { background: %1; color: %2; }
        QWidget#sidebar, QScrollArea#editor, QScrollArea#editor > QWidget > QWidget { background: %3; color: %2; }
        QWidget#sidebar { border-right: 1px solid %4; }
        QScrollArea#editor { border: 0; border-left: 1px solid %4; }
        QListView#taskList, QListWidget#projects, QScrollArea#board { border: 0; background: transparent; outline: none; }
        QListWidget#projects::item { padding: 6px 5px; border-radius: 5px; }
        QListWidget#projects::item:selected { background: %5; color: %2; }
        QLabel#strong, QLabel#pageTitle { font-weight: 650; }
        QLabel#pageTitle { font-size: 16px; padding: 5px 1px; }
        QLabel#empty, QLabel#muted { color: %6; }
        QPushButton#tool { background: transparent; border: 0; border-radius: 6px; min-width: 30px; min-height: 29px; font-size: 17px; }
        QPushButton#tool:hover, QPushButton#secondary:hover, QPushButton#sync:hover { background: %5; }
        QPushButton#secondary, QPushButton#sync { padding: 6px 9px; border-radius: 6px; border: 1px solid %4; }
        QPushButton#primary { background: #287bcc; color: white; border: 0; border-radius: 6px; padding: 8px 14px; }
        QPushButton#addTask { background: #3283d7; color: white; font-size: 24px; border: 0; border-radius: 22px;
                             min-width: 44px; max-width: 44px; min-height: 44px; max-height: 44px; }
        QPushButton#addTask:hover, QPushButton#primary:hover { background: #2670b7; }
        QPushButton:disabled { color: %6; }
        QLineEdit, QPlainTextEdit, QComboBox, QDateEdit { border: 1px solid %4; border-radius: 6px; padding: 5px;
                                                        background: %1; color: %2; }
        QLineEdit:focus, QPlainTextEdit:focus, QPushButton#nav:focus { border: 1px solid #539be6; }
        QPushButton#nav { min-height: 48px; border-radius: 8px; padding: 2px 9px; text-align: left; font-weight: 650; }
    )CSS").arg(canvas.name(), ink.name(), side.name(), line.name(), colors.color(QPalette::Highlight).name(), muted.name());
    for (int i = 0; i < navigation.size(); ++i) {
        auto *tile = navigation[i];
        const auto id = QStringLiteral("nav%1").arg(i);
        tile->setObjectName(id);
        const QColor accent(QString::fromLatin1(items[i].color));
        const auto tint = [&](qreal strength) {
            return QColor::fromRgbF(side.redF() + (accent.redF() - side.redF()) * strength,
                                    side.greenF() + (accent.greenF() - side.greenF()) * strength,
                                    side.blueF() + (accent.blueF() - side.blueF()) * strength).name();
        };
        css += QStringLiteral(
            "QPushButton#%1 { min-height:48px; border-radius:8px; padding:2px 9px; text-align:left;"
            " font-weight:650; background:%2; color:%3; border:1px solid %2; }"
            " QPushButton#%1:hover { background:%4; }"
            " QPushButton#%1:checked, QPushButton#%1:focus { border-color:%3; }")
            .arg(id, tint(dark ? 0.15 : 0.12), dark ? accent.name() : accent.darker(165).name(),
                 tint(dark ? 0.26 : 0.20));
    }
    setStyleSheet(css);
}

void Window::selectPage(Page page, const QString &id)
{
    filter = Filter{page, id, {}, {}, {}, {}};
    search->clear();
    search->hide();
    board = false;
    for (int i = 0; i < navigation.size(); ++i)
        navigation[i]->setChecked(items[i].page == page);
    projects->clearSelection();
    if (page == Page::Project) {
        for (int i = 0; i < projects->count(); ++i)
            if (projects->item(i)->data(Qt::UserRole).toString() == id) {
                projects->setCurrentItem(projects->item(i));
                break;
            }
    }
    refresh();
}

void Window::refreshNavigation()
{
    const auto selected = filter.project;
    projects->clear();
    QString group;
    const auto entries = store.projects();
    for (const auto &entry : entries) {
        if (projects->count() == 0 || group != entry.account) {
            group = entry.account;
            auto *header = new QListWidgetItem(group.isEmpty() ? QStringLiteral("На этом компьютере")
                : QStringLiteral("Baïkal · %1").arg(group), projects);
            header->setFlags(Qt::NoItemFlags);
            QFont font = header->font();
            font.setBold(true);
            header->setFont(font);
            header->setForeground(palette().color(QPalette::PlaceholderText));
        }
        auto *item = new QListWidgetItem(QStringLiteral("◯  %1").arg(entry.title), projects);
        item->setData(Qt::UserRole, entry.id);
        if (entry.id == selected)
            projects->setCurrentItem(item);
    }
    const QSignalBlocker block(labels);
    labels->clear();
    labels->addItem(QStringLiteral("Все метки"), "");
    for (const auto &tag : store.labels())
        labels->addItem(tag, tag);
    selectData(labels, filter.tag);
    filter.tag = labels->currentData().toString();
}

void Window::refresh()
{
    try {
        const auto selected = store.project(filter.project);
        const bool projectPage = filter.page == Page::Project && filter.search.isEmpty();
        labels->setVisible(filter.page == Page::Labels && filter.search.isEmpty());
        addSection->setVisible(projectPage && !selected.remote());
        boardButton->setVisible(projectPage);
        boardButton->setText(board ? QStringLiteral("Список") : QStringLiteral("Доска"));
        if (!filter.search.isEmpty()) {
            pageTitle->setText(QStringLiteral("Результаты поиска"));
        } else if (projectPage) {
            pageTitle->setText(selected.title);
        } else {
            for (const auto &item : items)
                if (filter.page == item.page)
                    pageTitle->setText(QString::fromUtf8(item.title));
        }

        const bool showBoard = board && projectPage;
        auto *scroll = static_cast<QScrollArea *>(boardArea);
        delete scroll->takeWidget();
        qDeleteAll(boardModels);
        boardModels.clear();
        if (showBoard) {
            model->clear();
            rebuildBoard();
        } else {
            model->setQuery(store.query(filter));
            if (model->lastError().isValid())
                throw std::runtime_error(model->lastError().text().toStdString());
        }
        boardArea->setVisible(showBoard);
        list->setVisible(!showBoard && model->rowCount() > 0);
        empty->setText(filter.search.isEmpty() ? QStringLiteral("Задач пока нет") : QStringLiteral("Ничего не найдено"));
        empty->setVisible(!showBoard && model->rowCount() == 0);
    } catch (const std::exception &error) {
        message(QString::fromUtf8(error.what()));
    }
}

void Window::rebuildBoard()
{
    auto *scroll = static_cast<QScrollArea *>(boardArea);
    auto *container = new QWidget;
    auto *columns = new QHBoxLayout(container);
    columns->setContentsMargins(0, 0, 0, 0);
    auto sections = store.sections(filter.project);
    sections.prepend({{}, QStringLiteral("Без секции")});
    for (const auto &[id, name] : sections) {
        auto *column = new QWidget(container);
        column->setMinimumWidth(230);
        auto *vertical = new QVBoxLayout(column);
        vertical->setContentsMargins(0, 0, 0, 0);
        vertical->addWidget(caption(name, column, "strong"));
        auto columnFilter = filter;
        columnFilter.section = id;
        auto *columnModel = new QSqlQueryModel(this);
        boardModels.append(columnModel);
        columnModel->setQuery(store.query(columnFilter));
        vertical->addWidget(createTaskList(column, columnModel), 1);
        columns->addWidget(column, 1);
    }
    scroll->setWidgetResizable(true);
    scroll->setWidget(container);
}

QListView *Window::createTaskList(QWidget *parent, QSqlQueryModel *taskModel)
{
    auto *view = new QListView(parent);
    view->setObjectName("taskList");
    view->setAccessibleName(QStringLiteral("Задачи"));
    view->setModel(taskModel);
    view->setUniformItemSizes(true);
    view->setMouseTracking(true);
    view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view->setItemDelegate(new TaskDelegate([this](const QString &id) { toggleTask(id); }, view));
    connect(view, &QListView::clicked, this, [this](const QModelIndex &idx) {
        const auto *mouse = qobject_cast<QListView *>(sender());
        if (mouse && mouse->indexAt(mouse->viewport()->mapFromGlobal(QCursor::pos())) == idx) {
            const bool child = !idx.siblingAtColumn(6).data().toString().isEmpty();
            const auto point = mouse->viewport()->mapFromGlobal(QCursor::pos());
            const auto row = mouse->visualRect(idx);
            if (QRect(row.left() + (child ? 23 : 8), row.top() + 14, 26, 26).contains(point))
                return;
        }
        openTask(idx.data().toString());
    });
    connect(view, &QListView::activated, this, [this](const QModelIndex &idx) { openTask(idx.data().toString()); });
    auto *complete = new QShortcut(QKeySequence(Qt::Key_Space), view);
    complete->setContext(Qt::WidgetShortcut);
    connect(complete, &QShortcut::activated, this, [this, view] {
        toggleTask(view->currentIndex().data().toString());
    });
    return view;
}

void Window::openTask(const QString &id)
{
    if (editor->isVisible() && current.id == id)
        return;
    if (!mayDiscard())
        return;
    try {
        const auto found = store.task(id);
        if (!found.id.isEmpty())
            editTask(found);
    } catch (const std::exception &error) {
        message(QString::fromUtf8(error.what()));
    }
}

void Window::editTask(const Task &task)
{
    loading = true;
    dirty = false;
    current = task;
    const bool writable = !task.remote();
    editorHeading->setText(!writable ? QStringLiteral("Задача Baïkal") :
                           task.id.isEmpty() ? QStringLiteral("Новая задача") : QStringLiteral("Задача"));
    title->setText(task.title);
    notes->setPlainText(task.notes);
    tags->setText(task.tags);
    due->setDate(task.due.isValid() ? task.due : noDate);
    deadline->setDate(task.deadline.isValid() ? task.deadline : noDate);
    priority->setCurrentIndex(qBound(0, task.priority, 3));
    if (repeat->findData("remote") >= 0)
        repeat->removeItem(repeat->findData("remote"));
    if (task.repeat == "remote")
        repeat->addItem(QStringLiteral("Повтор Baïkal"), "remote");
    selectData(repeat, task.repeat);
    pinned->setChecked(task.pinned);
    project->clear();
    project->addItem(QStringLiteral("Входящие"), Store::inbox());
    for (const auto &p : store.projects())
        if (!p.remote() || p.id == task.project)
            project->addItem(p.title, p.id);
    selectData(project, task.project);
    reloadSections(task.project, task.section);
    title->setReadOnly(!writable);
    notes->setReadOnly(!writable);
    tags->setReadOnly(!writable);
    project->setEnabled(writable && task.parent.isEmpty());
    for (QWidget *field : QList<QWidget *>{section, priority, repeat, pinned} + dateRows)
        field->setEnabled(writable);
    save->setVisible(writable);
    remove->setVisible(writable && !task.id.isEmpty());
    addSubtask->setVisible(writable && !task.id.isEmpty());
    reloadSubtasks();
    loading = false;
    editor->show();
}

void Window::newTask(const QString &parent)
{
    if (!mayDiscard())
        return;
    Task task;
    task.project = Store::inbox();
    task.parent = parent;
    if (!parent.isEmpty()) {
        task.project = current.project;
        task.section = current.section;
    } else if (filter.page == Page::Project && !store.project(filter.project).remote()) {
        task.project = filter.project;
    } else if (filter.page == Page::Today) {
        task.due = QDate::currentDate();
    }
    editTask(task);
    title->setFocus();
}

void Window::reloadSections(const QString &projectId, const QString &selected)
{
    section->clear();
    section->addItem(QStringLiteral("Без секции"), "");
    for (const auto &[id, name] : store.sections(projectId))
        section->addItem(name, id);
    selectData(section, selected);
}

void Window::reloadSubtasks()
{
    subtasks->clear();
    if (current.id.isEmpty())
        return;
    Filter children;
    children.parent = current.id;
    auto query = store.query(children);
    while (query.next()) {
        auto *item = new QListWidgetItem((query.value(2).toBool() ? QStringLiteral("✓  ")
                                                                 : QStringLiteral("◯  ")) + query.value(1).toString(), subtasks);
        item->setData(Qt::UserRole, query.value(0));
    }
}

void Window::saveTask()
{
    if (current.remote())
        return;
    try {
        auto draft = current;
        draft.title = title->text();
        draft.notes = notes->toPlainText();
        draft.tags = tags->text();
        draft.project = project->currentData().toString();
        draft.section = section->currentData().toString();
        draft.due = due->date() == noDate ? QDate() : due->date();
        draft.deadline = deadline->date() == noDate ? QDate() : deadline->date();
        draft.priority = priority->currentIndex();
        draft.repeat = repeat->currentData().toString();
        draft.pinned = pinned->isChecked();
        current = store.task(store.saveTask(draft));
        dirty = false;
        refreshNavigation();
        refresh();
        editTask(current);
        message(QStringLiteral("Задача сохранена"));
    } catch (const std::exception &error) {
        message(QString::fromUtf8(error.what()));
    }
}

void Window::toggleTask(const QString &id)
{
    if (id.isEmpty())
        return;
    if (dirty && current.id == id && !mayDiscard())
        return;
    try {
        if (store.task(id).remote()) {
            message(QStringLiteral("Задачи Baïkal доступны для просмотра"));
            return;
        }
        store.toggle(id);
        refresh();
        if (editor->isVisible() && current.id == id)
            editTask(store.task(id));
        else if (editor->isVisible())
            reloadSubtasks();
    } catch (const std::exception &error) {
        message(QString::fromUtf8(error.what()));
    }
}

bool Window::mayDiscard()
{
    if (!dirty)
        return true;
    const auto answer = QMessageBox::question(this, QStringLiteral("Сохранить изменения?"),
        QStringLiteral("Изменения в карточке ещё не сохранены."),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (answer == QMessageBox::Save) {
        saveTask();
        return !dirty;
    }
    if (answer == QMessageBox::Discard) {
        dirty = false;
        return true;
    }
    return false;
}

void Window::closeEvent(QCloseEvent *event)
{
    if (mayDiscard())
        event->accept();
    else
        event->ignore();
}

void Window::showSync()
{
    if (syncing)
        return;
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Импорт задач из Baïkal"));
    auto *layout = new QFormLayout(&dialog);
    auto *help = new QLabel(QStringLiteral("Приложение скачает списки и задачи для просмотра."), &dialog);
    help->setWordWrap(true);
    layout->addRow(help);
    auto *address = new QLineEdit(QSettings().value("dav/address").toString(), &dialog);
    address->setPlaceholderText(QStringLiteral("https://…/dav.php"));
    auto *login = new QLineEdit(QSettings().value("dav/user").toString(), &dialog);
    auto *passwordInput = new QLineEdit(&dialog);
    passwordInput->setEchoMode(QLineEdit::Password);
    layout->addRow(QStringLiteral("Адрес"), address);
    layout->addRow(QStringLiteral("Логин"), login);
    layout->addRow(QStringLiteral("Пароль"), passwordInput);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Загрузить"));
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.setMinimumWidth(390);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const auto url = QUrl(address->text().trimmed(), QUrl::StrictMode);
    const auto user = login->text().trimmed();
    const auto password = passwordInput->text();
    passwordInput->clear();
    syncing = caldav.start(url, user, password);
    syncButton->setEnabled(!syncing);
    if (syncing) {
        QSettings settings;
        settings.setValue("dav/address", url.toString());
        settings.setValue("dav/user", user);
    }
}

void Window::message(const QString &text)
{
    status->setText(text);
}

bool Window::smokeTest()
{
    newTask();
    if (!editor->isVisible())
        return false;
    title->setText(QStringLiteral("Проверка карточки"));
    due->setDate(QDate::currentDate());
    saveTask();
    if (current.id.isEmpty() || model->rowCount() != 1)
        return false;
    const auto id = current.id;
    toggleTask(id);
    selectPage(Page::Completed);
    if (model->rowCount() != 1 || !store.task(id).done)
        return false;
    const auto sample = store.addProject(QStringLiteral("Проект"));
    store.addSection(sample, QStringLiteral("Испытание доски"));
    selectPage(Page::Project, sample);
    board = true;
    refresh();
    return boardArea->isVisible() && boardModels.size() == 2;
}
