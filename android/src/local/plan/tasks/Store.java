package local.plan.tasks;

import android.content.ContentValues;
import android.content.Context;
import android.database.Cursor;
import android.database.sqlite.SQLiteDatabase;
import android.database.sqlite.SQLiteOpenHelper;

import java.time.LocalDate;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;

final class Store extends SQLiteOpenHelper {
    private static final String COLUMNS = "id,project,parent,title,notes,tags,due,deadline,priority,done,pinned,remote,repeat_rule";

    static final class Project {
        final String id;
        final String title;
        final String account;
        Project(String id, String title, String account) {
            this.id = id;
            this.title = title;
            this.account = account;
        }
        boolean remote() { return !account.isEmpty(); }
        @Override public String toString() { return title; }
    }

    Store(Context context) {
        super(context.getApplicationContext(), "plan-tasks.db", null, 1);
        setWriteAheadLoggingEnabled(true);
    }

    @Override public void onConfigure(SQLiteDatabase db) { db.setForeignKeyConstraintsEnabled(true); }

    @Override public void onCreate(SQLiteDatabase db) {
        db.execSQL("CREATE TABLE projects(id TEXT PRIMARY KEY, title TEXT NOT NULL, account TEXT NOT NULL)");
        db.execSQL("CREATE TABLE tasks(id TEXT PRIMARY KEY, project TEXT NOT NULL REFERENCES projects(id), parent TEXT NOT NULL,"
                + " title TEXT NOT NULL, notes TEXT NOT NULL, tags TEXT NOT NULL, due TEXT, deadline TEXT,"
                + " priority INTEGER NOT NULL, done INTEGER NOT NULL, pinned INTEGER NOT NULL, remote INTEGER NOT NULL,"
                + " repeat_rule TEXT NOT NULL, raw TEXT NOT NULL)");
        db.execSQL("CREATE INDEX tasks_project ON tasks(project, done)");
        db.execSQL("CREATE INDEX tasks_parent ON tasks(parent)");
        db.execSQL("INSERT INTO projects VALUES('inbox','Входящие','')");
    }

    @Override public void onUpgrade(SQLiteDatabase db, int before, int after) {
        throw new IllegalStateException("Несовместимая версия базы задач.");
    }

    List<Project> projects() {
        List<Project> result = new ArrayList<>();
        try (Cursor row = getReadableDatabase().rawQuery(
                "SELECT id,title,account FROM projects ORDER BY id='inbox' DESC, account, title", null)) {
            while (row.moveToNext()) result.add(new Project(row.getString(0), row.getString(1), row.getString(2)));
        }
        return result;
    }

    Project project(String id) {
        try (Cursor row = getReadableDatabase().rawQuery("SELECT id,title,account FROM projects WHERE id=?", new String[]{id})) {
            return row.moveToFirst() ? new Project(row.getString(0), row.getString(1), row.getString(2)) : null;
        }
    }

    String addProject(String title) {
        String id = UUID.randomUUID().toString();
        writeProject(getWritableDatabase(), id, title, "");
        return id;
    }

    private static void writeProject(SQLiteDatabase db, String id, String title, String account) {
        ContentValues v = new ContentValues();
        v.put("id", id);
        v.put("title", title);
        v.put("account", account);
        if (db.update("projects", v, "id=?", new String[]{id}) == 0) db.insertOrThrow("projects", null, v);
    }

    List<Task> list(String view, String project, LocalDate today) {
        String day = today.toString();
        String filter;
        String[] args = new String[0];
        switch (view) {
            case "today":
                filter = "done=0 AND (due<=? OR deadline<=?)";
                args = new String[]{day, day};
                break;
            case "upcoming": filter = "done=0 AND (due IS NOT NULL OR deadline IS NOT NULL)"; break;
            case "pinned": filter = "done=0 AND pinned=1"; break;
            case "completed": filter = "done=1"; break;
            case "labels": filter = "done=0 AND tags<>''"; break;
            case "search": filter = "1=1"; break;
            default:
                filter = "done=0 AND project=?";
                args = new String[]{project};
        }
        return readTasks(filter, args);
    }

    List<Task> children(String id) { return readTasks("parent=?", new String[]{id}); }

    Task get(String id) {
        List<Task> result = readTasks("id=?", new String[]{id});
        return result.isEmpty() ? null : result.get(0);
    }

    private List<Task> readTasks(String where, String[] args) {
        List<Task> result = new ArrayList<>();
        String sql = "SELECT " + COLUMNS + " FROM tasks WHERE " + where
                + " ORDER BY pinned DESC, coalesce(due,deadline,'9999-12-31'), rowid";
        try (Cursor row = getReadableDatabase().rawQuery(sql, args)) {
            while (row.moveToNext()) {
                Task task = new Task();
                task.id = row.getString(0);
                task.project = row.getString(1);
                task.parent = row.getString(2);
                task.title = row.getString(3);
                task.notes = row.getString(4);
                task.tags = row.getString(5);
                if (!row.isNull(6)) task.due = LocalDate.parse(row.getString(6));
                if (!row.isNull(7)) task.deadline = LocalDate.parse(row.getString(7));
                task.priority = row.getInt(8);
                task.done = row.getInt(9) != 0;
                task.pinned = row.getInt(10) != 0;
                task.remote = row.getInt(11) != 0;
                task.repeat = row.getString(12);
                result.add(task);
            }
        }
        return result;
    }

    void saveLocal(Task task) {
        if (task.remote) throw new IllegalArgumentException("Задача Baïkal доступна для просмотра.");
        Project project = project(task.project);
        if (project == null || project.remote()) throw new IllegalArgumentException("Выбери локальный проект.");
        if (task.id == null) task.id = UUID.randomUUID().toString();
        writeTask(getWritableDatabase(), task);
    }

    void toggle(Task task) {
        if (!task.done && task.due != null && !task.repeat.isEmpty()) {
            switch (task.repeat) {
                case "daily": task.due = task.due.plusDays(1); break;
                case "weekly": task.due = task.due.plusWeeks(1); break;
                case "monthly": task.due = task.due.plusMonths(1); break;
                default: task.done = true;
            }
        } else task.done = !task.done;
        saveLocal(task);
    }

    void remove(Task task) {
        if (task.remote) throw new IllegalArgumentException("Задача Baïkal доступна для просмотра.");
        SQLiteDatabase db = getWritableDatabase();
        db.beginTransaction();
        try {
            db.execSQL("UPDATE tasks SET parent='' WHERE parent=? AND remote=0", new String[]{task.id});
            db.delete("tasks", "id=? AND remote=0", new String[]{task.id});
            db.setTransactionSuccessful();
        } finally { db.endTransaction(); }
    }

    void importLists(List<DavClient.Collection> lists) {
        SQLiteDatabase db = getWritableDatabase();
        db.beginTransaction();
        try {
            for (DavClient.Collection list : lists) {
                writeProject(db, list.id, list.title, list.account);
                Set<String> seen = new HashSet<>();
                for (Task task : list.tasks) {
                    writeTask(db, task);
                    seen.add(task.id);
                }
                if (list.skipped > 0) continue;
                List<String> missing = new ArrayList<>();
                try (Cursor row = db.rawQuery("SELECT id FROM tasks WHERE project=? AND remote=1", new String[]{list.id})) {
                    while (row.moveToNext()) if (!seen.contains(row.getString(0))) missing.add(row.getString(0));
                }
                for (String id : missing) db.delete("tasks", "id=? AND remote=1", new String[]{id});
            }
            db.setTransactionSuccessful();
        } finally { db.endTransaction(); }
    }

    private static void writeTask(SQLiteDatabase db, Task task) {
        ContentValues v = new ContentValues();
        v.put("id", task.id);
        v.put("project", task.project);
        v.put("parent", task.parent);
        v.put("title", task.title);
        v.put("notes", task.notes);
        v.put("tags", task.tags);
        v.put("due", task.due == null ? null : task.due.toString());
        v.put("deadline", task.deadline == null ? null : task.deadline.toString());
        v.put("priority", task.priority);
        v.put("done", task.done);
        v.put("pinned", task.pinned);
        v.put("remote", task.remote);
        v.put("repeat_rule", task.repeat);
        v.put("raw", task.raw);
        if (db.update("tasks", v, "id=?", new String[]{task.id}) == 0) db.insertOrThrow("tasks", null, v);
    }
}
