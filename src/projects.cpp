#include "window.h"
#include "appearance.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QComboBox>
#include <QDateEdit>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QLocale>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPdfWriter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QSqlQuery>
#include <QTextDocument>
#include <QUrlQuery>
#include <QWidgetAction>

namespace {
QString viewKey(const QString &id)
{
    return QStringLiteral("projectView/") + id + '/';
}

const QStringList orderNames{QStringLiteral("По дате"), QStringLiteral("По названию"),
    QStringLiteral("По приоритету"), QStringLiteral("По порядку добавления")};
const QStringList dateNames{QStringLiteral("Все даты"), QStringLiteral("Просроченные"),
    QStringLiteral("Сегодня"), QStringLiteral("Предстоящие"), QStringLiteral("Без даты")};
const QStringList priorityNames{QStringLiteral("Все приоритеты"), QStringLiteral("Обычный"),
    QStringLiteral("Высокий"), QStringLiteral("Средний"), QStringLiteral("Низкий")};
const QDate noDeadline(1900, 1, 1);

QDateEdit *deadlineField(const QDate &date, QWidget *parent)
{
    auto *field = new QDateEdit(parent);
    field->setMinimumDate(noDeadline);
    field->setDate(date.isValid() ? date : noDeadline);
    field->setSpecialValueText(QStringLiteral("Без срока"));
    field->setDisplayFormat("dd.MM.yyyy");
    field->setCalendarPopup(true);
    return field;
}
}

void Window::loadProjectView()
{
    if (filter.page != Page::Project)
        return;
    const QSettings settings;
    const auto key = viewKey(filter.project);
    board = settings.value(key + "board", false).toBool();
    filter.sort = qBound(0, settings.value(key + "sort", settings.value("tasks/sort", 0)).toInt(), 3);
    filter.showDone = settings.value(key + "completed", false).toBool();
    filter.descending = settings.value(key + "descending", false).toBool();
    filter.dateFilter = DateFilter(qBound(0, settings.value(key + "date", 0).toInt(), 4));
    filter.priorityFilter = qBound(-1, settings.value(key + "priority", -1).toInt(), 3);
    filter.projectTag = settings.value(key + "tag").toString();
}

void Window::setBoard(bool enabled)
{
    if (filter.page != Page::Project)
        return;
    QSettings().setValue(viewKey(filter.project) + "board", enabled);
    board = enabled;
    refresh();
}

void Window::setTaskSelection(bool enabled)
{
    selectingTasks = enabled;
    for (auto *view : QList<QListView *>{list} + boardViews) {
        view->clearSelection();
        view->setSelectionMode(enabled ? QAbstractItemView::ExtendedSelection : QAbstractItemView::SingleSelection);
    }
}

void Window::copySelectedTasks()
{
    QStringList titles;
    const auto views = boardArea->isVisible() ? boardViews : QList<QListView *>{list};
    for (auto *view : views) {
        for (const auto &index : view->selectionModel()->selectedRows())
            titles.append(index.siblingAtColumn(1).data().toString());
    }
    if (titles.isEmpty()) {
        message(QStringLiteral("Выберите задачи для копирования"));
        return;
    }
    QApplication::clipboard()->setText(titles.join(QStringLiteral("\n")));
    setTaskSelection(false);
    message(QStringLiteral("Задачи скопированы"));
}

void Window::showViewMenu(QWidget *anchor)
{
    auto *menu = new QMenu(anchor);
    menu->setObjectName("projectViewMenu");
    menu->setAttribute(Qt::WA_DeleteOnClose);
    if (filter.page != Page::Project) {
        for (int i = 0; i < 3; ++i)
            menu->addAction(orderNames.at(i), this, [this, i] {
                QSettings().setValue("tasks/sort", i);
                refresh();
            });
    } else {
        auto *action = new QWidgetAction(menu);
        auto *panel = new QWidget(menu);
        auto *form = new QFormLayout(panel);
        form->setContentsMargins(8, 6, 8, 8);
        auto *modes = new QWidget(panel);
        auto *modeRow = new QHBoxLayout(modes);
        modeRow->setContentsMargins(0, 0, 0, 0);
        auto *listMode = new QPushButton(QStringLiteral("Список"), modes);
        auto *boardMode = new QPushButton(QStringLiteral("Доска"), modes);
        for (auto *mode : {listMode, boardMode}) {
            mode->setCheckable(true);
            mode->setAutoExclusive(true);
            modeRow->addWidget(mode);
        }
        (board ? boardMode : listMode)->setChecked(true);
        connect(listMode, &QPushButton::clicked, panel, [this] { setBoard(false); });
        connect(boardMode, &QPushButton::clicked, panel, [this] { setBoard(true); });
        form->addRow(modes);
        const auto key = viewKey(filter.project);
        const auto combo = [this, panel, form, key](const QString &caption, const QString &setting,
                                                   const QStringList &entries, int value, int offset = 0) {
            auto *field = new QComboBox(panel);
            field->addItems(entries);
            field->setCurrentIndex(value + offset);
            form->addRow(caption, field);
            connect(field, &QComboBox::activated, panel, [this, key, setting, offset](int index) {
                QSettings().setValue(key + setting, index - offset);
                loadProjectView();
                refresh();
            });
        };
        const auto check = [this, panel, form, key](const QString &caption, const QString &setting,
                                                    bool checked, bool reversed = false) {
            auto *field = new QCheckBox(caption, panel);
            field->setChecked(checked);
            form->addRow(field);
            connect(field, &QCheckBox::toggled, panel, [this, key, setting, reversed](bool enabled) {
                QSettings().setValue(key + setting, enabled != reversed);
                loadProjectView();
                refresh();
            });
        };
        combo(QStringLiteral("Сортировка"), "sort", orderNames, filter.sort);
        check(QStringLiteral("По возрастанию"), "descending", !filter.descending, true);
        combo(QStringLiteral("Срок выполнения"), "date", dateNames, int(filter.dateFilter));
        combo(QStringLiteral("Приоритет"), "priority", priorityNames, filter.priorityFilter, 1);
        auto *tag = new QComboBox(panel);
        tag->addItem(QStringLiteral("Все метки"), QString());
        for (const auto &label : store.labels())
            tag->addItem(label, label);
        tag->setCurrentIndex(qMax(0, tag->findData(filter.projectTag)));
        connect(tag, &QComboBox::activated, panel, [this, key, tag] {
            QSettings().setValue(key + "tag", tag->currentData());
            loadProjectView();
            refresh();
        });
        form->addRow(QStringLiteral("Метка"), tag);
        check(QStringLiteral("Показать завершённые"), "completed", filter.showDone);
        action->setDefaultWidget(panel);
        menu->addAction(action);
    }
    menu->popup(anchor->mapToGlobal(QPoint(0, anchor->height() + 5)));
}

void Window::updateProject(Project owner)
{
    try {
        store.saveProject(owner);
        refreshNavigation();
        refresh();
    } catch (const std::exception &error) {
        message(QString::fromUtf8(error.what()));
    }
}

void Window::editProject(const Project &owner, bool deadlineOnly)
{
    QDialog dialog(this);
    dialog.setWindowTitle(deadlineOnly ? QStringLiteral("Крайний срок проекта") : QStringLiteral("Изменить проект"));
    auto *layout = new QFormLayout(&dialog);
    if (owner.remote()) {
        auto *info = new QLabel(QStringLiteral("Настройки отображения этого проекта на компьютере."), &dialog);
        info->setWordWrap(true);
        layout->addRow(info);
    }
    auto *name = new QLineEdit(owner.title, &dialog);
    auto *description = new QPlainTextEdit(owner.notes, &dialog);
    description->setFixedHeight(80);
    auto *emoji = new QLineEdit(owner.emoji, &dialog);
    emoji->setMaxLength(16);
    emoji->setPlaceholderText(QStringLiteral("Например, 📁"));
    auto *useEmoji = new QCheckBox(QStringLiteral("Использовать emoji"), &dialog);
    useEmoji->setChecked(!owner.emoji.isEmpty());
    emoji->setEnabled(useEmoji->isChecked());
    connect(useEmoji, &QCheckBox::toggled, emoji, &QLineEdit::setEnabled);
    auto *color = new QPushButton(owner.color.isEmpty() ? QStringLiteral("Выбрать цвет…") : owner.color, &dialog);
    QString selectedColor = owner.color;
    connect(color, &QPushButton::clicked, &dialog, [&] {
        const auto chosen = QColorDialog::getColor(QColor(selectedColor), &dialog, QStringLiteral("Цвет проекта"));
        if (chosen.isValid()) {
            selectedColor = chosen.name();
            color->setText(selectedColor);
        }
    });
    auto *markdown = new QComboBox(&dialog);
    markdown->addItems({QStringLiteral("По умолчанию"), QStringLiteral("Включено"), QStringLiteral("Выключено")});
    markdown->setCurrentIndex(qBound(0, owner.markdown, 2));
    if (!deadlineOnly) {
        layout->addRow(QStringLiteral("Название"), name);
        layout->addRow(QStringLiteral("Описание"), description);
        layout->addRow(useEmoji, emoji);
        layout->addRow(QStringLiteral("Цвет"), color);
        layout->addRow(QStringLiteral("Markdown"), markdown);
    } else {
        for (auto *field : QList<QWidget *>{name, description, emoji, useEmoji, color, markdown})
            field->hide();
    }
    auto *date = deadlineField(owner.deadline, &dialog);
    layout->addRow(QStringLiteral("Крайний срок"), date);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("Сохранить проект"));
    buttons->button(QDialogButtonBox::Save)->setEnabled(!name->text().trimmed().isEmpty());
    connect(name, &QLineEdit::textChanged, buttons, [buttons](const QString &text) {
        buttons->button(QDialogButtonBox::Save)->setEnabled(!text.trimmed().isEmpty());
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);
    dialog.setMinimumWidth(410);
    if (dialog.exec() != QDialog::Accepted)
        return;
    auto changed = owner;
    changed.title = name->text().trimmed();
    changed.notes = description->toPlainText();
    changed.emoji = useEmoji->isChecked() ? emoji->text().trimmed() : QString();
    changed.color = selectedColor;
    changed.markdown = markdown->currentIndex();
    changed.deadline = date->date() == noDeadline ? QDate() : date->date();
    updateProject(changed);
}

void Window::updateProjectNote(const Project &owner)
{
    auto text = owner.notes;
    const auto deadline = owner.deadline.isValid()
        ? QStringLiteral("Крайний срок · %1").arg(QLocale().toString(owner.deadline, QLocale::ShortFormat))
        : QString();
    if (owner.markdown == 1 && !text.isEmpty()) {
        QTextDocument document;
        document.setMarkdown(text, QTextDocument::MarkdownNoHTML);
        projectNote->setTextFormat(Qt::RichText);
        projectNote->setText(document.toHtml() + (deadline.isEmpty() ? QString() : "<p>" + deadline.toHtmlEscaped() + "</p>"));
    } else {
        projectNote->setTextFormat(Qt::PlainText);
        projectNote->setText(text + (text.isEmpty() || deadline.isEmpty() ? QString() : QStringLiteral("\n")) + deadline);
    }
    projectNote->setVisible(!text.isEmpty() || !deadline.isEmpty());
}

void Window::addProjectSection(const QString &projectId)
{
    bool ok = false;
    const auto name = QInputDialog::getText(this, QStringLiteral("Новый раздел"), QStringLiteral("Название"),
                                           QLineEdit::Normal, {}, &ok);
    if (!ok)
        return;
    try {
        store.addSection(projectId, name);
        if (editor->isVisible() && project->currentData().toString() == projectId) {
            const auto selection = section->currentData().toString();
            loading = true;
            reloadSections(projectId, selection);
            loading = false;
        }
        refresh();
    } catch (const std::exception &error) {
        message(QString::fromUtf8(error.what()));
    }
}

void Window::manageProjectSections(const Project &owner)
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Управление разделами"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *rows = new QListWidget(&dialog);
    layout->addWidget(rows);
    const auto reload = [&](const QString &id) {
        rows->clear();
        for (const auto &[key, name] : store.sections(owner.id)) {
            auto *item = new QListWidgetItem(name, rows);
            item->setData(Qt::UserRole, key);
            if (key == id)
                rows->setCurrentItem(item);
        }
    };
    reload({});
    auto *tools = new QHBoxLayout;
    auto *rename = new QPushButton(QStringLiteral("Переименовать"), &dialog);
    auto *up = new QPushButton(QStringLiteral("Выше"), &dialog);
    auto *down = new QPushButton(QStringLiteral("Ниже"), &dialog);
    for (auto *button : {rename, up, down})
        tools->addWidget(button);
    layout->addLayout(tools);
    const auto updateButtons = [=](int row) {
        rename->setEnabled(row >= 0);
        up->setEnabled(row > 0);
        down->setEnabled(row >= 0 && row + 1 < rows->count());
    };
    connect(rows, &QListWidget::currentRowChanged, &dialog, updateButtons);
    updateButtons(rows->currentRow());
    const auto move = [&](int offset) {
        const auto id = rows->currentItem()->data(Qt::UserRole).toString();
        try {
            store.moveSection(owner.id, id, offset);
            reload(id);
            refresh();
        } catch (const std::exception &error) {
            QMessageBox::warning(&dialog, dialog.windowTitle(), QString::fromUtf8(error.what()));
        }
    };
    connect(up, &QPushButton::clicked, &dialog, [&] { move(-1); });
    connect(down, &QPushButton::clicked, &dialog, [&] { move(1); });
    connect(rename, &QPushButton::clicked, &dialog, [&] {
        const auto *row = rows->currentItem();
        bool ok = false;
        const auto name = QInputDialog::getText(&dialog, QStringLiteral("Переименовать раздел"),
            QStringLiteral("Название"), QLineEdit::Normal, row->text(), &ok);
        if (!ok)
            return;
        try {
            const auto id = row->data(Qt::UserRole).toString();
            store.renameSection(owner.id, id, name);
            reload(id);
            refresh();
        } catch (const std::exception &error) {
            QMessageBox::warning(&dialog, dialog.windowTitle(), QString::fromUtf8(error.what()));
        }
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.resize(375, 240);
    dialog.exec();
}

QString Window::projectText(const Project &owner)
{
    QString result = owner.title + '\n';
    if (!owner.notes.isEmpty())
        result += owner.notes + QStringLiteral("\n\n");
    Filter selection;
    selection.page = Page::Project;
    selection.project = owner.id;
    selection.showDone = true;
    auto tasks = store.query(selection);
    while (tasks.next())
        result += (tasks.value(2).toBool() ? QStringLiteral("✓ ") : QStringLiteral("• ")) +
            tasks.value(1).toString() + '\n';
    return result;
}

void Window::exportProjectPdf(const Project &owner)
{
    auto path = QFileDialog::getSaveFileName(this, QStringLiteral("Экспорт проекта"), {}, QStringLiteral("PDF (*.pdf)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(".pdf", Qt::CaseInsensitive))
        path += ".pdf";
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        message(QStringLiteral("Не удалось открыть файл PDF"));
        return;
    }
    {
        QPdfWriter pdf(&file);
        pdf.setTitle(owner.title);
        QTextDocument document;
        document.setPlainText(projectText(owner));
        document.print(&pdf);
    }
    message(file.commit() ? QStringLiteral("Проект экспортирован в PDF") : QStringLiteral("Не удалось сохранить PDF"));
}

void Window::chooseArchivedProject()
{
    QList<Project> archived;
    QStringList labels;
    for (const auto &entry : store.projects()) {
        if (entry.archived) {
            archived.append(entry);
            labels.append(QStringLiteral("%1 · %2").arg(labels.size() + 1).arg(entry.title));
        }
    }
    if (archived.isEmpty()) {
        message(QStringLiteral("В архиве нет проектов"));
        return;
    }
    bool ok = false;
    const auto choice = QInputDialog::getItem(this, QStringLiteral("Архив проектов"),
        QStringLiteral("Проект для восстановления"), labels, 0, false, &ok);
    if (!ok)
        return;
    auto restored = archived.at(labels.indexOf(choice));
    restored.archived = false;
    updateProject(restored);
    selectPage(Page::Project, restored.id);
}

void Window::showProjectMenu(const QString &id, const QPoint &position, bool header)
{
    auto owner = store.project(id);
    auto *menu = new QMenu(this);
    menu->setObjectName("projectActions");
    menu->setAttribute(Qt::WA_DeleteOnClose);
    if (owner.id.isEmpty()) {
        menu->addAction(QStringLiteral("Закреплённые"), this, [this] { selectPage(Page::Pins); });
        menu->addAction(QStringLiteral("Завершённые"), this, [this] { selectPage(Page::Completed); });
        menu->addAction(QStringLiteral("Параметры"), this, &Window::showPreferences);
        menu->popup(position);
        return;
    }
    const auto action = [this, menu](Mark mark, const QString &label, auto handler) {
        return menu->addAction(markIcon(mark, palette().color(QPalette::WindowText)), label, this, handler);
    };
    action(Mark::Today, owner.favorite ? QStringLiteral("Убрать из избранного") : QStringLiteral("В избранное"),
        [this, owner]() mutable { owner.favorite = !owner.favorite; updateProject(owner); });
    action(Mark::Gear, QStringLiteral("Изменить проект…"), [this, owner] { editProject(owner); });
    if (header)
        action(Mark::Clock, QStringLiteral("Крайний срок проекта…"), [this, owner] { editProject(owner, true); });
    action(Mark::Quick, owner.remote() ? QStringLiteral("Дублировать на компьютер") : QStringLiteral("Дублировать"),
        [this, owner] {
            if (!mayDiscard())
                return;
            try {
                const auto id = store.duplicateProject(owner.id);
                refreshNavigation();
                selectPage(Page::Project, id);
            } catch (const std::exception &error) {
                message(QString::fromUtf8(error.what()));
            }
        });
    if (header && !owner.remote()) {
        menu->addSeparator();
        action(Mark::Plus, QStringLiteral("Новый раздел…"), [this, owner] { addProjectSection(owner.id); });
        action(Mark::Sort, QStringLiteral("Управление разделами…"), [this, owner] { manageProjectSections(owner); });
    }
    if (header) {
        if (selectingTasks) {
            action(Mark::Quick, QStringLiteral("Скопировать выбранные задачи"), [this] { copySelectedTasks(); });
            action(Mark::Close, QStringLiteral("Завершить выбор"), [this] { setTaskSelection(false); });
        } else {
            action(Mark::Menu, QStringLiteral("Выбрать задачи"), [this] {
                if (!mayDiscard())
                    return;
                closeCard();
                setTaskSelection(true);
                message(QStringLiteral("Выбери строки задач. Ctrl добавляет отдельные строки, Shift выделяет диапазон."));
            });
        }
        action(Mark::Quick, QStringLiteral("Вставить задачу из буфера"), [this, owner] {
            const auto text = QApplication::clipboard()->text().trimmed();
            if (text.isEmpty()) {
                message(QStringLiteral("В буфере обмена нет текста"));
                return;
            }
            if (!mayDiscard())
                return;
            Task draft;
            draft.project = owner.id;
            draft.priority = qBound(0, QSettings().value("tasks/defaultPriority", 0).toInt(), 3);
            const auto newline = text.indexOf('\n');
            draft.title = text.section('\n', 0, 0).trimmed();
            if (newline >= 0)
                draft.notes = text.mid(newline + 1);
            editTask(draft);
            title->setFocus();
        });
    }
    if (owner.remote()) {
        menu->addSeparator();
        action(Mark::Sync, QStringLiteral("Обновить из Baïkal"), [this] { showSync(); });
    }
    menu->addSeparator();
    action(Mark::Attachment, QStringLiteral("Скопировать проект для отправки"), [this, owner] {
        QApplication::clipboard()->setText(projectText(owner));
        message(QStringLiteral("Проект скопирован"));
    });
    action(Mark::Quick, QStringLiteral("Отправить по почте…"), [this, owner] {
        auto email = QUrl(QStringLiteral("mailto:"));
        QUrlQuery query;
        query.addQueryItem("subject", owner.title);
        query.addQueryItem("body", projectText(owner));
        email.setQuery(query);
        if (!QDesktopServices::openUrl(email))
            message(QStringLiteral("Не удалось открыть почтовое приложение"));
    });
    action(Mark::Backup, QStringLiteral("Экспортировать как PDF…"), [this, owner] { exportProjectPdf(owner); });
    menu->addSeparator();
    action(Mark::Inbox, owner.archived ? QStringLiteral("Вернуть из архива") : QStringLiteral("Архивировать"),
        [this, owner]() mutable {
            if (!mayDiscard())
                return;
            owner.archived = !owner.archived;
            if (owner.archived && filter.project == owner.id)
                selectPage(Page::Inbox);
            updateProject(owner);
        });
    if (!owner.remote()) {
        action(Mark::Close, QStringLiteral("Удалить проект…"), [this, owner] {
            if (!mayDiscard() || QMessageBox::question(this, QStringLiteral("Удалить проект и его задачи?"),
                                                       owner.title, QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
                return;
            try {
                store.removeProject(owner.id);
                if (filter.project == owner.id)
                    selectPage(Page::Inbox);
                refreshNavigation();
                refresh();
            } catch (const std::exception &error) {
                message(QString::fromUtf8(error.what()));
            }
        });
    }
    menu->popup(position);
}
