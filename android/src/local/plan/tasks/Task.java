package local.plan.tasks;

import java.time.LocalDate;

final class Task {
    String id;
    String project = "inbox";
    String parent = "";
    String title = "";
    String notes = "";
    String tags = "";
    LocalDate due;
    LocalDate deadline;
    int priority;
    boolean done;
    boolean pinned;
    boolean remote;
    String repeat = "";
    String raw = "";
}
