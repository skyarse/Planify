package local.plan.tasks;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.DatePickerDialog;
import android.content.SharedPreferences;
import android.content.res.ColorStateList;
import android.content.res.Configuration;
import android.graphics.Color;
import android.graphics.Insets;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.Editable;
import android.text.InputType;
import android.text.TextUtils;
import android.text.TextWatcher;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.widget.ArrayAdapter;
import android.widget.BaseAdapter;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

import java.io.IOException;
import java.time.LocalDate;
import java.time.format.DateTimeFormatter;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.TreeSet;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class MainActivity extends Activity {
    private static final Locale RUSSIAN = Locale.forLanguageTag("ru-RU");
    private static final String[] PRIORITIES = {"Обычный", "Высокий", "Средний", "Низкий"};
    private static final String[] REPEATS = {"", "daily", "weekly", "monthly"};
    private static final DateTimeFormatter DATE = DateTimeFormatter.ofPattern("d MMMM", RUSSIAN);
    private static final int BLUE = 0xff2968b4;
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Runnable refreshSearch = () -> refresh();
    private Store store;
    private SharedPreferences prefs;
    private TextView heading;
    private TextView detail;
    private TextView syncStatus;
    private EditText search;
    private LinearLayout tabs;
    private LinearLayout empty;
    private TextView emptyTitle;
    private TextView emptyHint;
    private ListView list;
    private TextView add;
    private RowAdapter rows;
    private boolean dark;
    private boolean importing;
    private int canvas;
    private int card;
    private int ink;
    private int muted;
    private int outline;
    private int accentSoft;
    private int accentInk;
    private String page = "today";
    private String project = "inbox";
    private String tag = "";
    private String report = "";
    private final Map<String, String> projectNames = new HashMap<>();

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        store = new Store(this);
        prefs = getSharedPreferences("settings", MODE_PRIVATE);
        page = state == null ? prefs.getString("home", "today") : state.getString("page", "today");
        project = state == null ? "inbox" : state.getString("project", "inbox");
        tag = state == null ? "" : state.getString("tag", "");
        if (state == null) {
            String lastImport = prefs.getString("lastImport", "");
            if (!lastImport.isEmpty()) report = lastImport;
        }
        buildScreen();
    }

    @Override
    protected void onSaveInstanceState(Bundle out) {
        out.putString("page", page);
        out.putString("project", project);
        out.putString("tag", tag);
        super.onSaveInstanceState(out);
    }

    @Override
    public void onConfigurationChanged(Configuration configuration) {
        super.onConfigurationChanged(configuration);
        boolean nowDark = darkTheme();
        if (nowDark != dark) buildScreen();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (rows != null) refresh();
    }

    @Override
    protected void onDestroy() {
        handler.removeCallbacks(refreshSearch);
        worker.execute(() -> store.close());
        worker.shutdown();
        super.onDestroy();
    }

    private boolean darkTheme() {
        String theme = prefs.getString("theme", "system");
        return "dark".equals(theme) || ("system".equals(theme)
                && (getResources().getConfiguration().uiMode & Configuration.UI_MODE_NIGHT_MASK) == Configuration.UI_MODE_NIGHT_YES);
    }

    private void buildScreen() {
        dark = darkTheme();
        canvas = dark ? 0xff101722 : 0xfff5f7fc;
        card = dark ? 0xff1b2635 : Color.WHITE;
        ink = dark ? 0xffebf0fa : 0xff202c3d;
        muted = dark ? 0xffa6b6ce : 0xff52627a;
        outline = dark ? 0xff314258 : 0xffe0e6f2;
        accentSoft = dark ? 0xff294d72 : 0xffdceaff;
        accentInk = dark ? 0xffbfdcff : 0xff194c89;
        getWindow().setDecorFitsSystemWindows(false);


        LinearLayout root = vertical();
        root.setBackgroundColor(canvas);
        root.setOnApplyWindowInsetsListener((v, window) -> {
            Insets bars = window.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout()
                    | WindowInsets.Type.ime());
            v.setPadding(bars.left, bars.top, bars.right, bars.bottom);
            return window;
        });
        setContentView(root);
        root.requestApplyInsets();
        int appearance = WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS
                | WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS;
        root.post(() -> {
            WindowInsetsController controller = root.getWindowInsetsController();
            if (controller != null) controller.setSystemBarsAppearance(dark ? 0 : appearance, appearance);
        });

        LinearLayout top = horizontal();
        top.setGravity(Gravity.CENTER_VERTICAL);
        top.setPadding(dp(20), dp(9), dp(12), 0);
        TextView app = text("План задач", 16, accentInk, true);
        top.addView(app, new LinearLayout.LayoutParams(0, dp(48), 1));
        TextView projectButton = pill("Проекты", false);
        projectButton.setOnClickListener(v -> showProjects());
        top.addView(projectButton);
        TextView menu = pill("⋮", false);
        menu.setTextSize(24);
        menu.setContentDescription("Меню");
        menu.setOnClickListener(v -> showMenu());
        top.addView(menu);
        root.addView(top);

        LinearLayout header = vertical();
        header.setPadding(dp(20), dp(10), dp(20), dp(12));
        heading = text("", 34, ink, true);
        heading.setMaxLines(2);
        heading.setEllipsize(TextUtils.TruncateAt.END);
        detail = text("", 14, muted, false);
        detail.setPadding(0, dp(4), 0, 0);
        header.addView(heading);
        header.addView(detail);
        root.addView(header);

        tabs = horizontal();
        tabs.setPadding(dp(16), 0, dp(16), dp(9));
        root.addView(tabs);
        search = field("Поиск по задачам", "", InputType.TYPE_CLASS_TEXT);
        search.setSingleLine(true);
        search.setPadding(dp(14), 0, dp(14), 0);
        search.setBackground(surface(card, outline, 12));
        LinearLayout.LayoutParams searchSize = new LinearLayout.LayoutParams(-1, dp(48));
        searchSize.setMargins(dp(16), 0, dp(16), dp(7));
        root.addView(search, searchSize);
        search.setVisibility(View.GONE);
        search.addTextChangedListener(new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int start, int count, int after) { }
            @Override public void onTextChanged(CharSequence s, int start, int before, int count) {
                handler.removeCallbacks(refreshSearch);
                handler.postDelayed(refreshSearch, 160);
            }
            @Override public void afterTextChanged(Editable s) { }
        });

        FrameLayout body = new FrameLayout(this);
        root.addView(body, new LinearLayout.LayoutParams(-1, 0, 1));
        list = new ListView(this);
        list.setDivider(null);
        list.setClipToPadding(false);
        list.setPadding(dp(15), dp(2), dp(15), dp(82));
        rows = new RowAdapter();
        list.setAdapter(rows);
        body.addView(list, new FrameLayout.LayoutParams(-1, -1));
        list.setOnItemClickListener((parent, view, position, id) -> openTask(rows.getItem(position)));

        empty = vertical();
        empty.setGravity(Gravity.CENTER);
        empty.setPadding(dp(26), 0, dp(26), dp(50));
        body.addView(empty, new FrameLayout.LayoutParams(-1, -1));
        TextView check = text("✓", 46, accentInk, true);
        check.setGravity(Gravity.CENTER);
        check.setBackground(surface(accentSoft, 0, 46));
        empty.addView(check, new LinearLayout.LayoutParams(dp(88), dp(88)));
        emptyTitle = text("", 21, ink, true);
        emptyTitle.setGravity(Gravity.CENTER);
        emptyTitle.setPadding(0, dp(18), 0, dp(6));
        emptyHint = text("", 15, muted, false);
        emptyHint.setGravity(Gravity.CENTER);
        empty.addView(emptyTitle);
        empty.addView(emptyHint);

        add = text("+", 33, Color.WHITE, false);
        add.setGravity(Gravity.CENTER);
        add.setContentDescription("Новая задача");
        add.setBackground(touch(BLUE, 0, 19));
        add.setElevation(dp(3));
        FrameLayout.LayoutParams addSize = new FrameLayout.LayoutParams(dp(58), dp(58), Gravity.BOTTOM | Gravity.END);
        addSize.setMargins(dp(18), dp(14), dp(19), dp(15));
        body.addView(add, addSize);
        add.setOnClickListener(v -> createTask(null));

        syncStatus = text("", 13, muted, false);
        syncStatus.setPadding(dp(20), dp(8), dp(20), dp(10));
        root.addView(syncStatus);
        updateStatus();
        renderTabs();
        refresh();
    }

    private void renderTabs() {
        tabs.removeAllViews();
        String[] names = {"Входящие", "Сегодня", "Предстоящие"};
        String[] keys = {"inbox", "today", "upcoming"};
        for (int i = 0; i < keys.length; i++) {
            String key = keys[i];
            TextView tab = pill(names[i], key.equals(page));
            tab.setMaxLines(1);
            tab.setEllipsize(TextUtils.TruncateAt.END);
            LinearLayout.LayoutParams size = new LinearLayout.LayoutParams(0, dp(46), 1);
            size.setMargins(dp(3), 0, dp(3), 0);
            tabs.addView(tab, size);
            tab.setOnClickListener(v -> navigate(key, "inbox", ""));
        }
    }

    private void navigate(String target, String selectedProject, String label) {
        page = target;
        project = selectedProject;
        tag = label;
        search.setText("");
        search.setVisibility(View.GONE);
        renderTabs();
        refresh();
        list.setSelection(0);
    }

    private void refresh() {
        projectNames.clear();
        for (Store.Project entry : store.projects()) projectNames.put(entry.id, entry.title);
        String term = search.getText().toString().trim().toLowerCase(RUSSIAN);
        LocalDate now = LocalDate.now();
        List<Task> tasks = store.list(!term.isEmpty() ? "search" : page, project, now);
        if (!term.isEmpty()) {
            tasks.removeIf(task -> !(task.title + "\n" + task.notes + "\n" + task.tags).toLowerCase(RUSSIAN).contains(term));
        } else if (!tag.isEmpty()) {
            tasks.removeIf(task -> !hasTag(task.tags, tag));
        }
        rows.items = tasks;
        rows.notifyDataSetChanged();
        heading.setText(!term.isEmpty() ? "Поиск" : titleForPage());
        detail.setText("today".equals(page) && term.isEmpty()
                ? now.format(DateTimeFormatter.ofPattern("EEEE, d MMMM", RUSSIAN)) + "  ·  " + countLabel(tasks.size())
                : countLabel(tasks.size()));
        list.setVisibility(tasks.isEmpty() ? View.GONE : View.VISIBLE);
        empty.setVisibility(tasks.isEmpty() ? View.VISIBLE : View.GONE);
        emptyTitle.setText(term.isEmpty() ? "Здесь пока пусто" : "Ничего не найдено");
        emptyHint.setText(!term.isEmpty() ? "Попробуй другой запрос."
                : isRemoteProject() ? "Обнови импорт в меню Baïkal." : "Нажми «+», чтобы добавить задачу.");
        add.setVisibility(View.VISIBLE);
    }

    private static boolean hasTag(String input, String tag) {
        for (String item : input.split(",")) if (item.trim().equalsIgnoreCase(tag)) return true;
        return false;
    }

    private String titleForPage() {
        switch (page) {
            case "today": return "Сегодня";
            case "upcoming": return "Предстоящие";
            case "pinned": return "Закреплённые";
            case "completed": return "Завершённые";
            case "labels": return tag.isEmpty() ? "Метки" : tag;
            case "project": return projectNames.getOrDefault(project, "Проект");
            default: return "Входящие";
        }
    }

    private static String countLabel(int count) {
        int ones = count % 10, tens = count % 100;
        return count + (tens >= 11 && tens <= 14 ? " задач"
                : ones == 1 ? " задача" : ones >= 2 && ones <= 4 ? " задачи" : " задач");
    }

    private boolean isRemoteProject() {
        Store.Project entry = "project".equals(page) ? store.project(project) : null;
        return entry != null && entry.remote();
    }

    private final class RowAdapter extends BaseAdapter {
        List<Task> items = new ArrayList<>();
        @Override public int getCount() { return items.size(); }
        @Override public Task getItem(int position) { return items.get(position); }
        @Override public long getItemId(int position) { return position; }
        @Override public View getView(int position, View previous, ViewGroup parent) {
            Task task = getItem(position);
            Row row = previous == null ? new Row() : (Row) previous;
            row.bind(task);
            return row;
        }
    }

    private final class Row extends LinearLayout {
        final TextView done;
        final TextView title;
        final TextView meta;
        final View shell;
        Row() {
            super(MainActivity.this);
            setPadding(0, dp(3), 0, dp(4));
            LinearLayout panel = horizontal();
            panel.setGravity(Gravity.CENTER_VERTICAL);
            panel.setPadding(dp(8), dp(5), dp(10), dp(5));
            panel.setBackground(surface(card, outline, 16));
            addView(panel, new LinearLayout.LayoutParams(-1, -2));
            shell = panel;
            done = text("○", 25, accentInk, false);
            done.setGravity(Gravity.CENTER);
            panel.addView(done, new LinearLayout.LayoutParams(dp(46), dp(48)));
            LinearLayout column = vertical();
            column.setPadding(dp(4), dp(5), 0, dp(5));
            panel.addView(column, new LinearLayout.LayoutParams(0, -2, 1));
            title = text("", 17, ink, true);
            title.setMaxLines(2);
            title.setEllipsize(TextUtils.TruncateAt.END);
            meta = text("", 13, muted, false);
            meta.setPadding(0, dp(4), 0, 0);
            meta.setMaxLines(2);
            meta.setEllipsize(TextUtils.TruncateAt.END);
            column.addView(title);
            column.addView(meta);
        }
        void bind(Task task) {
            done.setText(task.done ? "✓" : "○");
            String action = task.done ? "Вернуть в активные" : "Отметить выполненной";
            done.setContentDescription(task.remote ? "Просмотреть задачу " + task.title : action + " · " + task.title);
            done.setOnClickListener(v -> {
                if (task.remote) openTask(task);
                else { store.toggle(task); refresh(); }
            });
            title.setText((task.pinned ? "◆  " : "") + task.title);
            title.setPaintFlags(task.done ? title.getPaintFlags() | Paint.STRIKE_THRU_TEXT_FLAG
                    : title.getPaintFlags() & ~Paint.STRIKE_THRU_TEXT_FLAG);
            List<String> bits = new ArrayList<>();
            bits.add(projectNames.getOrDefault(task.project, "Задача"));
            if (task.due != null) bits.add(task.due.format(DATE));
            if (task.deadline != null) bits.add("Срок " + task.deadline.format(DATE));
            if (task.priority > 0) bits.add(PRIORITIES[task.priority]);
            if (task.remote) bits.add("Baïkal");
            meta.setText(TextUtils.join("  ·  ", bits));
            shell.setOnClickListener(v -> openTask(task));
        }
    }

    private void showMenu() {
        boolean searching = search.getVisibility() == View.VISIBLE;
        String[] options = {searching ? "Закрыть поиск" : "Поиск", "Метки", "Закреплённые", "Завершённые", "Импорт из Baïkal", "Параметры"};
        dialog().setTitle("Меню").setItems(options, (d, item) -> {
            switch (item) {
                case 0:
                    WindowInsetsController controller = search.getWindowInsetsController();
                    if (searching) {
                        search.setText("");
                        search.setVisibility(View.GONE);
                        if (controller != null) controller.hide(WindowInsets.Type.ime());
                        refresh();
                    } else {
                        search.setVisibility(View.VISIBLE);
                        search.requestFocus();
                        if (controller != null) controller.show(WindowInsets.Type.ime());
                    }
                    break;
                case 1: showLabels(); break;
                case 2: navigate("pinned", "inbox", ""); break;
                case 3: navigate("completed", "inbox", ""); break;
                case 4: showImport(); break;
                default: showSettings();
            }
        }).show();
    }

    private void showLabels() {
        Set<String> names = new TreeSet<>(String.CASE_INSENSITIVE_ORDER);
        for (Task task : store.list("labels", "inbox", LocalDate.now()))
            for (String value : task.tags.split(",")) if (!value.trim().isEmpty()) names.add(value.trim());
        List<String> labels = new ArrayList<>();
        labels.add("Все метки");
        labels.addAll(names);
        dialog().setTitle("Метки").setItems(labels.toArray(new String[0]), (d, i) ->
                navigate("labels", "inbox", i == 0 ? "" : labels.get(i))).show();
    }

    private void showProjects() {
        List<Store.Project> projects = store.projects();
        String[] names = projects.stream().map(p -> p.title + (p.remote() ? "  ·  Baïkal" : "")).toArray(String[]::new);
        dialog().setTitle("Проекты").setItems(names, (d, i) -> {
            String id = projects.get(i).id;
            navigate("inbox".equals(id) ? "inbox" : "project", id, "");
        }).setPositiveButton("Новый проект", (d, w) -> {
            LinearLayout form = form();
            EditText title = field("Название проекта", "", InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_CAP_SENTENCES);
            form.addView(title);
            AlertDialog input = dialog().setTitle("Новый проект").setView(form)
                    .setNegativeButton("Отмена", null).setPositiveButton("Создать", null).create();
            input.show();
            input.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v -> {
                String name = title.getText().toString().trim();
                if (name.isEmpty()) { title.setError("Введи название"); return; }
                String id = store.addProject(name);
                input.dismiss();
                navigate("project", id, "");
            });
        }).setNeutralButton("Baïkal…", (d, w) -> showImport()).show();
    }

    private void showImport() {
        if (importing) { notifyUser("Импорт уже выполняется"); return; }
        LinearLayout form = form();
        form.addView(text("Загружает копию задач для просмотра офлайн.", 15, dialogText(), false));
        EditText address = field("https://сервер/dav.php", prefs.getString("davAddress", ""),
                InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI);
        EditText login = field("Логин", prefs.getString("davLogin", ""), InputType.TYPE_CLASS_TEXT);
        EditText password = field("Пароль", "", InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
        form.addView(address);
        form.addView(login);
        form.addView(password);
        AlertDialog input = dialog().setTitle("Импорт из Baïkal").setView(scroll(form, false))
                .setNegativeButton("Отмена", null).setPositiveButton("Загрузить", null).create();
        input.show();
        input.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v -> {
            String url = address.getText().toString().trim();
            String user = login.getText().toString().trim();
            String secret = password.getText().toString();
            if (url.isEmpty()) { address.setError("Введи адрес Baïkal"); return; }
            if (user.isEmpty()) { login.setError("Введи логин"); return; }
            if (secret.isEmpty()) { password.setError("Введи пароль"); return; }
            password.setText("");
            input.dismiss();
            importTasks(url, user, secret);
        });
    }

    private void importTasks(String url, String user, String password) {
        importing = true;
        updateStatus();
        worker.execute(() -> {
            try (AndroidTransport transport = new AndroidTransport(this)) {
                List<DavClient.Collection> remote = new DavClient(url, user, password, transport).fetch();
                store.importLists(remote);
                int count = 0, skipped = 0;
                for (DavClient.Collection collection : remote) {
                    count += collection.tasks.size();
                    skipped += collection.skipped;
                }
                String message = "Baïkal · загружено задач: " + count;
                if (skipped > 0) message += " · пропущено: " + skipped + ". Предыдущие записи сохранены.";
                final String summary = message;
                prefs.edit().putString("davAddress", url).putString("davLogin", user).putString("lastImport", summary).apply();
                runOnUiThread(() -> {
                    if (isDestroyed() || isFinishing()) return;
                    importing = false;
                    report = summary;
                    if ("today".equals(page) && !remote.isEmpty())
                        navigate("project", remote.get(0).id, "");
                    else refresh();
                    updateStatus();
                });
            } catch (IOException | IllegalArgumentException error) {
                finishImport(error.getMessage() == null ? "Проверь адрес Baïkal." : error.getMessage());
            } catch (RuntimeException error) {
                finishImport("Не удалось сохранить импорт. Предыдущие задачи сохранены.");
            }
        });
    }

    private void finishImport(String message) {
        runOnUiThread(() -> {
            if (isDestroyed() || isFinishing()) return;
            importing = false;
            report = message;
            updateStatus();
        });
    }

    private void updateStatus() {
        String status = importing ? "Baïkal · загрузка задач…" : report;
        syncStatus.setText(status);
        syncStatus.setVisibility(status.isEmpty() ? View.GONE : View.VISIBLE);
    }

    private void openTask(Task task) {
        if (!task.remote) { editTask(task); return; }
        LinearLayout fields = form();
        if (!task.notes.isEmpty()) fields.addView(text(task.notes, 16, dialogText(), false));
        if (task.due != null) fields.addView(text("Дата: " + task.due.format(DATE), 15, dialogText(), false));
        if (task.deadline != null) fields.addView(text("Срок: " + task.deadline.format(DATE), 15, dialogText(), false));
        if (!task.tags.isEmpty()) fields.addView(text("Метки: " + task.tags, 15, dialogText(), false));
        if (!task.repeat.isEmpty()) fields.addView(text("Повторяющаяся задача", 15, dialogText(), false));
        fields.addView(text(task.done ? "Завершена · Baïkal" : "Для просмотра · Baïkal", 15, dialogText(), false));
        AlertDialog view = dialog().setTitle(task.title).setView(scroll(fields, false))
                .setPositiveButton("Закрыть", null).create();
        addChildren(fields, task, view);
        view.show();
    }

    private void createTask(Task parent) {
        Task task = new Task();
        if (parent != null) {
            task.parent = parent.id;
            task.project = parent.project;
        } else {
            if ("project".equals(page) && !isRemoteProject()) task.project = project;
            if ("today".equals(page)) task.due = LocalDate.now();
        }
        editTask(task);
    }

    private void editTask(Task task) {
        LinearLayout form = form();
        EditText title = field("Название", task.title, InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_CAP_SENTENCES);
        EditText notes = field("Описание", task.notes,
                InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_MULTI_LINE | InputType.TYPE_TEXT_FLAG_CAP_SENTENCES);
        notes.setMinLines(2);
        EditText tags = field("Метки через запятую", task.tags, InputType.TYPE_CLASS_TEXT);
        form.addView(title);
        form.addView(notes);
        form.addView(tags);
        List<Store.Project> projects = store.projects();
        projects.removeIf(Store.Project::remote);
        int currentProject = 0;
        for (int i = 0; i < projects.size(); i++) if (task.project.equals(projects.get(i).id)) currentProject = i;
        Spinner target = spinner(projects.stream().map(p -> p.title).toArray(String[]::new), currentProject);
        if (!task.parent.isEmpty() || (task.id != null && !store.children(task.id).isEmpty())) target.setEnabled(false);
        addField(form, "Проект", target);
        Spinner priority = spinner(PRIORITIES, task.priority);
        addField(form, "Приоритет", priority);
        LocalDate[] dates = {task.due, task.deadline};
        addDate(form, "Дата", dates, 0);
        addDate(form, "Срок", dates, 1);
        int rule = 0;
        for (int i = 0; i < REPEATS.length; i++) if (REPEATS[i].equals(task.repeat)) rule = i;
        Spinner repeat = spinner(new String[]{"Без повтора", "Каждый день", "Каждую неделю", "Каждый месяц"}, rule);
        addField(form, "Повтор", repeat);
        CheckBox pinned = check("Закрепить", task.pinned);
        CheckBox completed = check("Выполнена", task.done);
        form.addView(pinned);
        form.addView(completed);
        AlertDialog.Builder builder = dialog().setTitle(task.id == null ? "Новая задача" : "Задача")
                .setView(scroll(form, false)).setNegativeButton("Отмена", null).setPositiveButton("Сохранить", null);
        if (task.id != null) builder.setNeutralButton("Удалить", null);
        AlertDialog edit = builder.create();
        if (task.id != null) {
            addChildren(form, task, edit);
            TextView child = pill("Добавить подзадачу", true);
            form.addView(child);
            child.setOnClickListener(v -> {
                edit.dismiss();
                createTask(task);
            });
        }
        edit.show();
        edit.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v -> {
            String name = title.getText().toString().trim();
            if (name.isEmpty()) { title.setError("Введи название"); return; }
            if (repeat.getSelectedItemPosition() != 0 && dates[0] == null) {
                notifyUser("Для повторения выбери дату");
                return;
            }
            task.title = name;
            task.notes = notes.getText().toString();
            task.tags = tags.getText().toString().trim();
            task.project = projects.get(target.getSelectedItemPosition()).id;
            task.priority = priority.getSelectedItemPosition();
            task.due = dates[0];
            task.deadline = dates[1];
            task.repeat = REPEATS[repeat.getSelectedItemPosition()];
            task.pinned = pinned.isChecked();
            task.done = completed.isChecked();
            store.saveLocal(task);
            edit.dismiss();
            refresh();
        });
        if (task.id != null) edit.getButton(AlertDialog.BUTTON_NEUTRAL).setOnClickListener(v ->
            dialog().setTitle("Удалить задачу?").setMessage(task.title)
                .setNegativeButton("Отмена", null).setPositiveButton("Удалить", (d, w) -> {
                    store.remove(task);
                    edit.dismiss();
                    refresh();
                }).show());
    }

    private void addChildren(LinearLayout form, Task task, AlertDialog parent) {
        List<Task> children = store.children(task.id);
        if (children.isEmpty()) return;
        form.addView(text("Подзадачи", 15, dialogText(), true));
        for (Task child : children) {
            TextView item = pill((child.done ? "✓  " : "") + child.title, false);
            item.setTextColor(dialogText());
            item.setBackground(touch(dark ? 0xff29364b : 0xffe9eef7, 0, 10));
            form.addView(item);
            item.setOnClickListener(v -> { parent.dismiss(); openTask(child); });
        }
    }

    private void addDate(LinearLayout form, String label, LocalDate[] dates, int index) {
        LinearLayout row = horizontal();
        TextView choose = pill(dateLabel(label, dates[index]), true);
        row.addView(choose, new LinearLayout.LayoutParams(0, -2, 1));
        TextView clear = pill("×", false);
        clear.setTextColor(dialogText());
        clear.setContentDescription("Очистить " + label.toLowerCase(RUSSIAN));
        row.addView(clear);
        clear.setOnClickListener(v -> { dates[index] = null; choose.setText(dateLabel(label, null)); });
        choose.setOnClickListener(v -> {
            LocalDate date = dates[index] == null ? LocalDate.now() : dates[index];
            DatePickerDialog picker = new DatePickerDialog(this,
                    dark ? android.R.style.Theme_Material_Dialog_Alert : android.R.style.Theme_Material_Light_Dialog_Alert,
                    (d, year, month, day) -> {
                        dates[index] = LocalDate.of(year, month + 1, day);
                        choose.setText(dateLabel(label, dates[index]));
                    }, date.getYear(), date.getMonthValue() - 1, date.getDayOfMonth());
            picker.show();
        });
        form.addView(row);
    }

    private static String dateLabel(String label, LocalDate date) {
        return label + ": " + (date == null ? "не выбрана" : date.format(DateTimeFormatter.ofPattern("dd.MM.yyyy")));
    }

    private void showSettings() {
        dialog().setTitle("Параметры").setItems(new String[]{"Домашняя страница", "Оформление", "Учётная запись Baïkal"},
                (d, i) -> {
                    if (i == 0) chooseSetting("Домашняя страница", "home",
                            new String[]{"Входящие", "Сегодня", "Предстоящие"}, new String[]{"inbox","today","upcoming"}, "today");
                    else if (i == 1) chooseSetting("Оформление", "theme",
                            new String[]{"Системное", "Светлое", "Тёмное"}, new String[]{"system","light","dark"}, "system");
                    else showImport();
                }).show();
    }

    private void chooseSetting(String title, String key, String[] names, String[] values, String fallback) {
        int selected = java.util.Arrays.asList(values).indexOf(prefs.getString(key, fallback));
        dialog().setTitle(title).setSingleChoiceItems(names, selected, (d, i) -> {
            prefs.edit().putString(key, values[i]).apply();
            d.dismiss();
            if ("theme".equals(key)) buildScreen();
            else navigate(values[i], "inbox", "");
        }).setNegativeButton("Отмена", null).show();
    }

    private AlertDialog.Builder dialog() {
        return new AlertDialog.Builder(this,
                dark ? android.R.style.Theme_Material_Dialog_Alert : android.R.style.Theme_Material_Light_Dialog_Alert);
    }

    private Spinner spinner(String[] options, int selected) {
        Spinner spin = new Spinner(this, Spinner.MODE_DROPDOWN);
        ArrayAdapter<String> adapter = new ArrayAdapter<String>(this, android.R.layout.simple_spinner_item, options) {
            @Override public View getView(int position, View convert, ViewGroup parent) {
                TextView value = (TextView) super.getView(position, convert, parent);
                value.setTextColor(dialogText());
                return value;
            }
            @Override public View getDropDownView(int position, View convert, ViewGroup parent) {
                TextView value = (TextView) super.getDropDownView(position, convert, parent);
                value.setTextColor(dialogText());
                value.setBackgroundColor(dark ? 0xff29364b : Color.WHITE);
                return value;
            }
        };
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        spin.setAdapter(adapter);
        spin.setSelection(selected);
        spin.setMinimumHeight(dp(48));
        return spin;
    }

    private void addField(LinearLayout form, String title, View input) {
        form.addView(text(title, 14, dialogText(), true));
        form.addView(input, new LinearLayout.LayoutParams(-1, -2));
    }

    private CheckBox check(String value, boolean selected) {
        CheckBox box = new CheckBox(this);
        box.setText(value);
        box.setTextColor(dialogText());
        box.setChecked(selected);
        return box;
    }

    private LinearLayout form() {
        LinearLayout form = vertical();
        form.setPadding(dp(20), dp(10), dp(20), dp(14));
        return form;
    }

    private ScrollView scroll(View content, boolean fill) {
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(fill);
        scroll.addView(content);
        return scroll;
    }

    private EditText field(String hint, String value, int input) {
        EditText field = new EditText(this);
        field.setHint(hint);
        field.setText(value);
        field.setTextSize(16);
        field.setInputType(input);
        field.setTextColor(dialogText());
        field.setHintTextColor(dark ? 0xffa6b6ce : 0xff54627a);
        field.setMinimumHeight(dp(48));
        return field;
    }

    private TextView pill(String title, boolean selected) {
        TextView button = text(title, 14, selected ? accentInk : ink, selected);
        button.setGravity(Gravity.CENTER);
        button.setMinHeight(dp(46));
        button.setPadding(dp(9), 0, dp(9), 0);
        button.setBackground(touch(selected ? accentSoft : Color.TRANSPARENT, 0, 12));
        button.setFocusable(true);
        return button;
    }

    private TextView text(String value, int size, int color, boolean bold) {
        TextView label = new TextView(this);
        label.setText(value);
        label.setTextSize(size);
        label.setTextColor(color);
        label.setGravity(Gravity.CENTER_VERTICAL);
        if (bold) label.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        return label;
    }

    private LinearLayout vertical() {
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        return layout;
    }

    private LinearLayout horizontal() { return new LinearLayout(this); }
    private int dp(int pixel) { return Math.round(getResources().getDisplayMetrics().density * pixel); }
    private int dialogText() { return dark ? 0xffebf0fa : 0xff202c3d; }

    private GradientDrawable surface(int fill, int border, int radius) {
        GradientDrawable shape = new GradientDrawable();
        shape.setColor(fill);
        shape.setCornerRadius(dp(radius));
        if (border != 0) shape.setStroke(dp(1), border);
        return shape;
    }

    private RippleDrawable touch(int fill, int border, int radius) {
        return new RippleDrawable(ColorStateList.valueOf(0x25558bce), surface(fill, border, radius),
                surface(Color.WHITE, 0, radius));
    }

    private void notifyUser(String message) { Toast.makeText(this, message, Toast.LENGTH_SHORT).show(); }
}
