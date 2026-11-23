#include "window.h"
#include "appearance.h"

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
#include <QTextBoundaryFinder>
#include <QVBoxLayout>
#include <QWindow>
#include <QGuiApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <array>
#include <functional>
#include <memory>

namespace {
struct NavItem {
    Page page;
    const char *title;
    Mark mark;
    const char *color;
};

constexpr std::array items{
    NavItem{Page::Inbox, "Входящие", Mark::Inbox, "#3284ed"},
    NavItem{Page::Today, "Сегодня", Mark::Today, "#20c978"},
    NavItem{Page::Scheduled, "Предстоящие", Mark::Calendar, "#963bb2"},
    NavItem{Page::Labels, "Метки", Mark::Tag, "#996940"},
    NavItem{Page::Pins, "Закреплённые", Mark::Pin, "#d94d63"},
    NavItem{Page::Completed, "Завершённые", Mark::Complete, "#e78629"},
};

class NavTile : public QPushButton {
public:
    NavTile(const NavItem &info, QWidget *parent) : QPushButton(QString::fromUtf8(info.title), parent), mark(info.mark)
    {
        setCheckable(true);
        setAccessibleName(text());
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(65);
        QFont label = font();
        label.setPixelSize(14);
        setFont(label);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    }

    QColor accent;
    int count = 0;
    bool overdue = false;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QColor tint = accent;
        tint.setAlpha(isChecked() || underMouse() ? 46 : 36);
        painter.setBrush(tint);
        QColor outline = accent;
        outline.setAlpha(isChecked() ? 165 : 110);
        if (isChecked() || hasFocus())
            painter.setPen(QPen(outline, isChecked() ? 1.8 : 1.0));
        else
            painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(QRectF(rect()).adjusted(1,1,-1,-1), 10, 10);
        drawMark(painter, mark, QRectF(12,10,18,18), accent, 2.1);
        painter.setPen(accent);
        painter.drawText(QRect(13,32,width()-23,27), Qt::AlignVCenter,
            fontMetrics().elidedText(text(), Qt::ElideRight, width()-23));
        if (count > 0) {
            QFont numberFont = font();
            numberFont.setBold(true);
            painter.setFont(numberFont);
            painter.drawText(QRect(33,7,width()-45,24), Qt::AlignRight | Qt::AlignVCenter, QString::number(count));
        }
        if (overdue) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor("#fa665a"));
            painter.drawEllipse(QPointF(width()-17,50), 4.5, 4.5);
        }
    }

private:
    Mark mark;
};

class EmptyMark : public QWidget {
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        drawMark(painter, Mark::Complete, QRectF(rect()).adjusted(2,2,-2,-2),
                 palette().color(QPalette::PlaceholderText), 2.8);
    }
};

class ProjectDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        return {option.rect.width(), index.data(Qt::UserRole).toString().isEmpty() ? 55 : 31};
    }

    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override
    {
        const bool account = idx.data(Qt::UserRole).toString().isEmpty();
        const auto rect = opt.rect.adjusted(2,0,-2,-1);
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        if (opt.state & (QStyle::State_Selected | QStyle::State_MouseOver)) {
            p->setPen(Qt::NoPen);
            p->setBrush(opt.palette.highlight());
            p->drawRoundedRect(rect, 7, 7);
        }
        p->setFont(opt.font);
        auto label = idx.data().toString();
        const auto ink = opt.palette.color(QPalette::Text);
        const auto muted = opt.palette.color(QPalette::PlaceholderText);
        p->setPen(ink);
        if (account) {
            QFont bold = opt.font;
            bold.setBold(true);
            p->setFont(bold);
            p->drawText(rect.adjusted(2,4,-64,-22), Qt::AlignVCenter,
                QFontMetrics(bold).elidedText(label,Qt::ElideRight,rect.width()-68));
            p->setFont(opt.font);
            p->setPen(muted);
            p->drawText(rect.adjusted(2,25,0,-7), Qt::AlignVCenter, QStringLiteral("протокол CalDAV"));
            drawMark(*p, Mark::Sync, QRectF(rect.right()-24,15+rect.top(),19,19), ink);
            p->setPen(opt.palette.color(QPalette::Mid));
            p->drawLine(rect.bottomLeft(),rect.bottomRight());
        } else {
            const auto glyph = idx.data(Qt::UserRole+2).toString();
            if (glyph.isEmpty())
                drawMark(*p, Mark::Project, QRectF(rect.left()+1,rect.center().y()-8,16,16), muted, 1.6);
            else
                p->drawText(rect.adjusted(1,0,-rect.width()+22,0), Qt::AlignVCenter, glyph);
            p->drawText(rect.adjusted(26,0,-34,0), Qt::AlignVCenter,
                opt.fontMetrics.elidedText(label,Qt::ElideRight,rect.width()-62));
            const int count = idx.data(Qt::UserRole+1).toInt();
            if (count > 0) {
                p->setPen(muted);
                QFont small = opt.font;
                small.setPointSizeF(qMax(8.0, opt.font.pointSizeF()-1.5));
                p->setFont(small);
                p->drawText(rect.adjusted(0,0,-8,0), Qt::AlignRight | Qt::AlignVCenter, QString::number(count));
            }
        }
        p->restore();
    }
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
    result->setTextFormat(Qt::PlainText);
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
    setWindowFlag(Qt::FramelessWindowHint);
    resize(1060, 680);
    setMinimumSize(760, 470);
    setWindowIcon(QIcon::fromTheme("local.plan.tasks"));
    if (QSettings().value("general/rememberWindow", false).toBool() && !qApp->arguments().contains("--smoke-test"))
        restoreGeometry(QSettings().value("general/geometry").toByteArray());
    systemDark = QApplication::palette().color(QPalette::Window).lightness() < 128;

    frame = new QWidget(this);
    frame->setObjectName("frame");
    frame->installEventFilter(this);
    setCentralWidget(frame);
    auto *frameLayout = new QHBoxLayout(frame);
    frameLayout->setContentsMargins(3, 3, 3, 3);

    auto *root = new QWidget(frame);
    root->setObjectName("windowRoot");
    frameLayout->addWidget(root);
    auto *layout = new QHBoxLayout(root);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    sidebar = new QWidget(root);
    sidebar->setObjectName("sidebar");
    sidebar->setFixedWidth(260);
    auto *left = new QVBoxLayout(sidebar);
    left->setContentsMargins(10, 0, 12, 12);
    left->setSpacing(9);
    auto *sideHeader = new QWidget(sidebar);
    sideHeader->setFixedHeight(46);
    sideHeader->setProperty("windowHeader", true);
    sideHeader->installEventFilter(this);
    auto *top = new QHBoxLayout(sideHeader);
    top->setContentsMargins(0,0,0,0);
    auto *appIcon = new QLabel(sideHeader);
    appIcon->setPixmap(windowIcon().isNull() ? markIcon(Mark::Quick, QColor("#3684d9")).pixmap(19) : windowIcon().pixmap(18));
    top->addWidget(appIcon);
    auto *findButton = iconButton(sideHeader, Mark::Search, QStringLiteral("Поиск (Ctrl+F)"));
    top->addWidget(findButton);
    auto *brand = caption(QStringLiteral("План задач"), sideHeader, "brand");
    brand->setAlignment(Qt::AlignCenter);
    brand->setAttribute(Qt::WA_TransparentForMouseEvents);
    top->addWidget(brand, 1);
    auto *menuButton = iconButton(sideHeader, Mark::Menu, QStringLiteral("Главное меню"));
    top->addWidget(menuButton);
    left->addWidget(sideHeader);
    auto *menu = new QMenu(menuButton);
    menu->addAction(QStringLiteral("Новый проект"), this, &Window::addLocalProject);
    menu->addAction(QStringLiteral("Обновить из Baïkal…"), this, &Window::showSync);
    menu->addSeparator();
    auto *preferencesAction = menu->addAction(QStringLiteral("Параметры"), QKeySequence(Qt::CTRL | Qt::Key_Comma), this, &Window::showPreferences);
    addAction(preferencesAction);
    menu->addAction(QStringLiteral("Выйти"), QKeySequence::Quit, this, &Window::close);
    connect(menuButton, &QPushButton::clicked, this, [menuButton, menu] {
        menu->popup(menuButton->mapToGlobal(QPoint(0,menuButton->height())));
    });

    auto *grid = new QGridLayout;
    grid->setSpacing(8);
    for (int i = 0; i < int(items.size()); ++i) {
        const auto &item = items[i];
        auto *tile = new NavTile(item, sidebar);
        connect(tile, &QPushButton::clicked, this, [this, page = item.page] { selectPage(page); });
        grid->addWidget(tile, i / 2, i % 2);
        navigation.append(tile);
    }
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    left->addLayout(grid);
    left->addSpacing(10);

    localHeading = new QWidget(sidebar);
    auto *projectHeader = new QHBoxLayout(localHeading);
    projectHeader->setContentsMargins(1,0,0,0);
    projectHeader->addWidget(caption(QStringLiteral("На этом компьютере"), localHeading, "strong"));
    projectHeader->addStretch();
    auto *addProject = iconButton(localHeading, Mark::Plus, QStringLiteral("Новый локальный проект"));
    projectHeader->addWidget(addProject);
    left->addWidget(localHeading);
    projects = new QListWidget(sidebar);
    projects->setObjectName("projects");
    projects->setAccessibleName(QStringLiteral("Проекты"));
    projects->setItemDelegate(new ProjectDelegate(projects));
    QFont projectFont = projects->font();
    projectFont.setPixelSize(14);
    projects->setFont(projectFont);
    projects->setMouseTracking(true);
    projects->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    projects->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    left->addWidget(projects, 1);
    syncButton = button(QStringLiteral("Подключить Baïkal…"), sidebar, "sync");
    left->addWidget(syncButton, 0, Qt::AlignLeft);
    layout->addWidget(sidebar);

    auto *center = new QWidget(root);
    auto *body = new QVBoxLayout(center);
    body->setContentsMargins(13, 0, 25, 19);
    body->setSpacing(8);
    auto *windowHeader = new QWidget(center);
    windowHeader->setFixedHeight(46);
    windowHeader->setProperty("windowHeader", true);
    windowHeader->installEventFilter(this);
    auto *controls = new QHBoxLayout(windowHeader);
    controls->setContentsMargins(0,0,0,0);
    controls->setSpacing(9);
    auto *split = iconButton(windowHeader, Mark::Split, QStringLiteral("Показать или скрыть боковую панель"));
    controls->addWidget(split);
    controls->addStretch();
    auto *sort = iconButton(windowHeader, Mark::Sort, QStringLiteral("Сортировка"));
    auto *sortMenu = new QMenu(sort);
    const QStringList sortLabels{QStringLiteral("По дате"), QStringLiteral("По названию"), QStringLiteral("По приоритету")};
    for (int i = 0; i < sortLabels.size(); ++i)
        sortMenu->addAction(sortLabels[i], this, [this,i] {
            QSettings().setValue("tasks/sort", i);
            refresh();
        });
    auto *more = iconButton(windowHeader, Mark::More, QStringLiteral("Действия с разделом"));
    auto *moreMenu = new QMenu(more);
    moreMenu->addAction(QStringLiteral("Закреплённые"), this, [this] { selectPage(Page::Pins); });
    moreMenu->addAction(QStringLiteral("Завершённые"), this, [this] { selectPage(Page::Completed); });
    moreMenu->addSeparator();
    moreMenu->addAction(QStringLiteral("Параметры"), this, &Window::showPreferences);
    for (auto entry : {qMakePair(sort,sortMenu),qMakePair(more,moreMenu)}) {
        controls->addWidget(entry.first);
        connect(entry.first, &QPushButton::clicked, this, [entry] {
            entry.second->popup(entry.first->mapToGlobal(QPoint(0,entry.first->height())));
        });
    }
    controls->addSpacing(18);
    auto *minimize = iconButton(windowHeader, Mark::Minus, QStringLiteral("Свернуть"), "windowControl");
    auto *maximize = iconButton(windowHeader, Mark::Maximize, QStringLiteral("Развернуть или восстановить"), "windowControl");
    auto *close = iconButton(windowHeader, Mark::Close, QStringLiteral("Закрыть"), "windowControl");
    for (auto *control : {minimize, maximize, close})
        controls->addWidget(control);
    connect(split, &QPushButton::clicked, this, [this] { sidebar->setVisible(sidebar->isHidden()); });
    connect(minimize, &QPushButton::clicked, this, &Window::showMinimized);
    connect(maximize, &QPushButton::clicked, this, [this] { isMaximized() ? showNormal() : showMaximized(); });
    connect(close, &QPushButton::clicked, this, &Window::close);
    body->addWidget(windowHeader);
    auto *toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(34,0,0,0);
    pageIcon = caption({}, center, "pageIcon");
    pageIcon->setAlignment(Qt::AlignCenter);
    pageIcon->setFixedSize(23,23);
    toolbar->addWidget(pageIcon);
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
    empty = new QWidget(center);
    auto *emptyLayout = new QVBoxLayout(empty);
    emptyLayout->setContentsMargins(0,0,0,0);
    emptyLayout->setSpacing(0);
    emptyLayout->addStretch(5);
    auto *emptyMark = new EmptyMark(empty);
    emptyMark->setFixedSize(153,153);
    emptyLayout->addWidget(emptyMark, 0, Qt::AlignHCenter);
    emptyLayout->addSpacing(24);
    emptyTitle = caption({}, empty, "emptyTitle");
    emptyTitle->setAlignment(Qt::AlignCenter);
    emptyTitle->setWordWrap(true);
    emptyLayout->addWidget(emptyTitle);
    emptyLayout->addSpacing(10);
    emptyHint = caption({}, empty, "emptyHint");
    emptyHint->setAlignment(Qt::AlignCenter);
    emptyHint->setWordWrap(true);
    emptyLayout->addWidget(emptyHint);
    emptyLayout->addStretch(6);
    eventList = new QPlainTextEdit(center);
    eventList->setObjectName("eventList");
    eventList->setAccessibleName(QStringLiteral("События календаря"));
    eventList->setReadOnly(true);
    eventList->setMaximumHeight(125);
    eventList->hide();
    body->insertWidget(3, eventList);
    body->addWidget(empty, 1);

    auto *footer = new QHBoxLayout;
    status = caption({}, center, "muted");
    status->setWordWrap(true);
    footer->addWidget(status, 1);
    auto *addTask = iconButton(center, Mark::Plus, QStringLiteral("Создать задачу (Ctrl+N)"), "addTask");
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
        if (QSettings().value("general/confirmDelete", true).toBool()
            && QMessageBox::question(this, QStringLiteral("Удалить задачу?"), current.title) != QMessageBox::Yes)
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
    quickShortcut = new QShortcut(QKeySequence(Qt::Key_A), this);
    connect(quickShortcut, &QShortcut::activated, this, [this] {
        auto *focus = QApplication::focusWidget();
        if (!qobject_cast<QLineEdit *>(focus) && !qobject_cast<QPlainTextEdit *>(focus)
            && !qobject_cast<QDateEdit *>(focus))
            newTask();
    });
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
    connect(addProject, &QPushButton::clicked, this, &Window::addLocalProject);
    connect(projects, &QListWidget::itemClicked, this, [this](const QListWidgetItem *item) {
        if (item->data(Qt::UserRole).toString().isEmpty())
            showSync();
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
            qsizetype count = 0;
            qsizetype recovered = 0;
            qsizetype skipped = 0;
            for (const auto &entry : lists) {
                count += entry.tasks.size();
                recovered += entry.recovered;
                skipped += entry.skipped;
            }
            auto summary = QStringLiteral("Baïkal · загружено задач: %1").arg(count);
            if (recovered)
                summary += QStringLiteral(" · с неполными полями: %1").arg(recovered);
            if (skipped)
                summary += QStringLiteral(" · пропущено: %1. Предыдущие записи сохранены.").arg(skipped);
            message(summary);
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
    loadEvents();
    reloadPreferences();
    const auto home = QSettings().value("general/home", int(Page::Inbox)).toInt();
    selectPage(!qApp->arguments().contains("--smoke-test") && home >= int(Page::Inbox) && home <= int(Page::Completed)
        ? Page(home) : Page::Inbox);
}

void Window::setTheme(const QString &option)
{
    theme = option == "light" || option == "dark" ? option : QStringLiteral("system");
    if (!qApp->arguments().contains("--smoke-test") && !qApp->arguments().contains("--demo"))
        QSettings().setValue("theme", theme);
    const bool dark = theme == "dark" || (theme == "system" && systemDark);
    const QColor canvas(dark ? "#101116" : "#f7f7f7");
    const QColor side(dark ? "#191a21" : "#f0f1f3");
    const QColor card(dark ? "#24242b" : "#ffffff");
    const QColor ink(dark ? "#f0f0f4" : "#303137");
    const QColor muted(dark ? "#a6a8b5" : "#88898e");
    const QColor line(dark ? "#32343e" : "#dadce0");
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
    colors.setColor(QPalette::Mid, line);
    QApplication::setPalette(colors);
    setPalette(colors);
    auto css = QStringLiteral(R"CSS(
        QLabel, QCheckBox, QListWidget#projects, QPushButton { color: %2; }
        QWidget#frame, QWidget#windowRoot, QDialog#preferences, QWidget#settingsPage { background: %1; color: %2; }
        QDialog#preferences { border: 1px solid %4; border-radius: 11px; }
        QLabel#brand, QLabel#settingsTitle { font-size: 14px; font-weight: bold; }
        QLabel#pageIcon { background: rgba(51,132,237,30); border-radius: 6px; }
        QScrollArea#preferencesScroll { background: transparent; border: 0; }
        QScrollBar:vertical { background: transparent; border: 0; width: 6px; margin: 2px 0; }
        QScrollBar::handle:vertical { background: %4; border-radius: 3px; min-height: 24px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
        QWidget#settingsGroup { background: %7; border: 1px solid %4; border-radius: 13px; }
        QPushButton#settingsRow { text-align: left; padding: 8px 12px; border: 0; border-bottom: 1px solid %4; background: transparent; }
        QPushButton#settingsRow[last="true"] { border-bottom: 0; }
        QPushButton#settingsRow:hover, QPushButton#windowControl:hover { background: %5; }
        QPushButton#windowControl { background: %4; border: 0; border-radius: 14px; }
        QLabel#settingsRowTitle { font-size: 14px; }
        QLabel#settingsRowHint { color: %6; font-size: 11px; }
        QLabel#settingsHint { color: %6; }
        QPlainTextEdit#eventList { background: %7; border: 1px solid %4; border-radius: 8px; padding: 4px 7px; }
        QWidget#sidebar, QScrollArea#editor, QScrollArea#editor > QWidget > QWidget { background: %3; color: %2; }
        QWidget#sidebar { border-right: 1px solid %4; }
        QScrollArea#editor { border: 0; border-left: 1px solid %4; }
        QListView#taskList, QListWidget#projects, QScrollArea#board { border: 0; background: transparent; outline: none; }
        QListWidget#projects::item { padding: 6px 5px; border-radius: 5px; }
        QListWidget#projects::item:selected { background: %5; color: %2; }
        QLabel#strong, QLabel#pageTitle { font-weight: 650; }
        QLabel#pageTitle { font-size: 19px; padding: 2px 0; }
        QLabel#emptyTitle { font-size: 25px; font-weight: bold; }
        QLabel#emptyHint { font-size: 14px; }
        QLabel#muted { color: %6; }
        QPushButton#tool { background: transparent; border: 0; border-radius: 6px; min-width: 30px; min-height: 29px; font-size: 17px; }
        QPushButton#tool:hover, QPushButton#secondary:hover, QPushButton#sync:hover { background: %5; }
        QPushButton#secondary, QPushButton#sync { padding: 6px 9px; border-radius: 6px; border: 1px solid %4; }
        QPushButton#primary { background: #287bcc; color: white; border: 0; border-radius: 6px; padding: 8px 14px; }
        QPushButton#addTask { background: #3283d7; color: white; font-size: 24px; border: 0; border-radius: 25px;
                             min-width: 50px; max-width: 50px; min-height: 50px; max-height: 50px; }
        QPushButton#addTask:hover, QPushButton#primary:hover { background: #2670b7; }
        QPushButton:disabled { color: %6; }
        QLineEdit, QPlainTextEdit, QComboBox, QDateEdit { border: 1px solid %4; border-radius: 6px; padding: 5px;
                                                        background: %1; color: %2; }
        QLineEdit:focus, QPlainTextEdit:focus, QPushButton#nav:focus { border: 1px solid #539be6; }
    )CSS").arg(canvas.name(), ink.name(), side.name(), line.name(), colors.color(QPalette::Highlight).name(), muted.name(), card.name());
    for (int i = 0; i < navigation.size(); ++i) {
        auto *tile = static_cast<NavTile *>(navigation[i]);
        tile->accent = QColor(QString::fromLatin1(items[i].color));
        if (dark && (items[i].page == Page::Scheduled || items[i].page == Page::Labels))
            tile->accent = tile->accent.lighter(140);
        tile->update();
    }
    setStyleSheet(css);
}

bool Window::eventFilter(QObject *object, QEvent *event)
{
    if (event->type() != QEvent::MouseButtonPress && event->type() != QEvent::MouseButtonDblClick)
        return QMainWindow::eventFilter(object, event);
    auto *mouse = static_cast<QMouseEvent *>(event);
    if (mouse->button() != Qt::LeftButton || !windowHandle())
        return QMainWindow::eventFilter(object, event);
    if (object->property("windowHeader").toBool()) {
        if (event->type() == QEvent::MouseButtonDblClick)
            isMaximized() ? showNormal() : showMaximized();
        else
            windowHandle()->startSystemMove();
        return true;
    }
    if (object == frame && !isMaximized()) {
        const auto point = mouse->position();
        Qt::Edges edges;
        if (point.x() < 4) edges |= Qt::LeftEdge;
        else if (point.x() >= frame->width()-4) edges |= Qt::RightEdge;
        if (point.y() < 4) edges |= Qt::TopEdge;
        else if (point.y() >= frame->height()-4) edges |= Qt::BottomEdge;
        if (edges)
            return windowHandle()->startSystemResize(edges);
    }
    return QMainWindow::eventFilter(object, event);
}

void Window::addLocalProject()
{
    bool ok = false;
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
}

void Window::reloadPreferences()
{
    const QSettings settings;
    for (int i = 0; i < navigation.size(); ++i) {
        const auto page = items[i].page;
        const bool visible = (page != Page::Pins || settings.value("sidebar/pins", false).toBool())
            && (page != Page::Completed || settings.value("sidebar/completed", false).toBool());
        navigation[i]->setVisible(visible);
    }
    quickShortcut->setEnabled(settings.value("quick/enabled", true).toBool());
    quickShortcut->setKey(settings.value("quick/control", false).toBool()
        ? QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A) : QKeySequence(Qt::Key_A));
    refreshNavigation();
    refresh();
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
    bool hasLocal = false;
    bool hasRemote = false;
    const bool showLocal = QSettings().value("sidebar/local", true).toBool();
    const auto entries = store.projects();
    for (const auto &entry : entries) {
        if (!entry.remote() && !showLocal)
            continue;
        if (entry.remote() && group != entry.account) {
            group = entry.account;
            auto *header = new QListWidgetItem(group, projects);
            header->setFlags(Qt::ItemIsEnabled);
            header->setToolTip(QStringLiteral("Обновить локальную копию из Baïkal"));
        }
        hasLocal |= !entry.remote();
        hasRemote |= entry.remote();
        auto name = entry.title;
        auto *item = new QListWidgetItem(name, projects);
        const auto first = name.toUcs4().value(0);
        if (first >= 0x1F000 || (first >= 0x2300 && first <= 0x27BF)) {
            QTextBoundaryFinder grapheme(QTextBoundaryFinder::Grapheme, name);
            const auto boundary = grapheme.toNextBoundary();
            if (boundary > 0 && boundary < name.size() && name.at(boundary).isSpace()) {
                item->setData(Qt::UserRole+2, name.left(boundary));
                item->setText(name.mid(boundary).trimmed());
            }
        }
        item->setData(Qt::UserRole, entry.id);
        item->setToolTip(entry.title);
        if (entry.id == selected)
            projects->setCurrentItem(item);
    }
    localHeading->setVisible(showLocal && (hasLocal || !hasRemote));
    syncButton->setVisible(!hasRemote);
    const QSignalBlocker block(labels);
    labels->clear();
    labels->addItem(QStringLiteral("Все метки"), "");
    for (const auto &tag : store.labels())
        labels->addItem(tag, tag);
    selectData(labels, filter.tag);
    filter.tag = labels->currentData().toString();
    refreshCounts();
}

void Window::refreshCounts()
{
    const bool showCounts = QSettings().value("sidebar/counts", true).toBool();
    const auto counts = showCounts ? store.projectCounts() : QHash<QString,int>();
    for (int i = 0; i < projects->count(); ++i) {
        auto *item = projects->item(i);
        item->setData(Qt::UserRole+1, counts.value(item->data(Qt::UserRole).toString()));
    }
    const int labelCount = labels->count()-1;
    for (int i = 0; i < navigation.size(); ++i) {
        auto *tile = static_cast<NavTile *>(navigation[i]);
        const auto page = items[i].page;
        tile->count = !showCounts || page == Page::Scheduled ? 0 : page == Page::Labels ? labelCount : store.count(page);
        tile->overdue = page == Page::Today && store.count(Page::Today,true) > 0;
        tile->update();
    }
}

void Window::refresh()
{
    try {
        const auto selected = store.project(filter.project);
        const QSettings settings;
        filter.sort = settings.value("tasks/sort", 0).toInt();
        filter.showDone = settings.value("tasks/showDoneToday", false).toBool();
        refreshEvents();
        const bool projectPage = filter.page == Page::Project && filter.search.isEmpty();
        labels->setVisible(filter.page == Page::Labels && filter.search.isEmpty());
        addSection->setVisible(projectPage && !selected.remote());
        boardButton->setVisible(projectPage);
        boardButton->setText(board ? QStringLiteral("Список") : QStringLiteral("Доска"));
        auto mark = Mark::Project;
        auto color = palette().color(QPalette::WindowText);
        if (!filter.search.isEmpty()) {
            mark = Mark::Search;
            pageTitle->setText(QStringLiteral("Результаты поиска"));
        } else if (projectPage) {
            pageTitle->setText(selected.title);
        } else {
            for (const auto &item : items)
                if (filter.page == item.page) {
                    pageTitle->setText(QString::fromUtf8(item.title));
                    mark = item.mark;
                    color = QColor(QString::fromLatin1(item.color));
                }
        }

        pageIcon->setPixmap(markIcon(mark,color).pixmap(20));
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
        emptyTitle->setText(!filter.search.isEmpty() ? QStringLiteral("Ничего не найдено")
            : filter.page == Page::Inbox ? QStringLiteral("Добавить несколько задач")
            : QStringLiteral("Задач пока нет"));
        emptyHint->setVisible(filter.search.isEmpty());
        const auto action = !quickShortcut->isEnabled() ? QStringLiteral("Ctrl+N") : quickShortcut->key().toString();
        emptyHint->setText(QStringLiteral("Нажмите клавишу «%1», чтобы создать новую задачу").arg(action.toLower()));
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
    task.priority = qBound(0, QSettings().value("tasks/defaultPriority", 0).toInt(), 3);
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
        refreshCounts();
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
    if (mayDiscard()) {
        if (QSettings().value("general/rememberWindow", false).toBool())
            QSettings().setValue("general/geometry", saveGeometry());
        event->accept();
    }
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
    if (!boardArea->isVisible() || boardModels.size() != 2)
        return false;

    std::unique_ptr<QDialog> prefs(makePreferences());
    int categories = 0;
    for (auto *button : prefs->findChildren<QPushButton *>()) {
        if (button->objectName() == "settingsRow")
            ++categories;
        if (button->accessibleName() == QStringLiteral("Боковая панель"))
            button->click();
    }
    if (categories != 10 || !navigation[4]->isHidden())
        return false;
    for (auto *check : prefs->findChildren<QCheckBox *>()) {
        if (check->text() == QStringLiteral("Показывать «Закреплённые»")) {
            check->click();
            if (navigation[4]->isHidden() || !QSettings().value("sidebar/pins").toBool())
                return false;

            const QString id = QStringLiteral("4dbbef64-4dcb-4cc3-a7d2-6acb9752f07e");
            const auto dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/calendars";
            QDir().mkpath(dir);
            QFile source(dir + '/' + id + ".ics");
            if (!source.open(QIODevice::WriteOnly))
                return false;
            const auto date = QDate::currentDate().toString("yyyyMMdd").toLatin1();
            source.write("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Test//EN\r\nBEGIN:VEVENT\r\n"
                         "UID:sample-event\r\nDTSTAMP:" + date + "T070000Z\r\nDTSTART;VALUE=DATE:" + date
                         + "\r\nSUMMARY:Sample calendar event\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n");
            source.close();
            QSettings().setValue("calendar/sources", QVariantMap{{id,"Fixture"}});
            loadEvents();
            selectPage(Page::Today);
            if (!eventList->isVisible() || !eventList->toPlainText().contains("Sample calendar event"))
                return false;
            QSettings().setValue("calendar/enabled/"+id,false);
            loadEvents();
            refreshEvents();
            return eventList->isHidden();
        }
    }
    return false;
}
