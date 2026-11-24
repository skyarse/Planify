package local.plan.tasks;

import java.io.IOException;
import java.net.URI;
import java.time.LocalDate;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;

public final class DavClientTest {
    private static final String BASE = "https://dav.example.test/dav.php";
    private static final String CAL = "urn:ietf:params:xml:ns:caldav";
    private static final String PAYLOAD = "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VTODO\r\nUID:task\r\n"
            + "SUMMARY:Купить\r\n  билеты\r\nDESCRIPTION;ALTREP=\"cid:example\":Строка\\nВторая\r\n"
            + "DUE;VALUE=DATE:20261203\r\nDTSTART:invalid\r\nSTATUS:NEEDS-ACTION\r\nX-PINNED:TRUE\r\n"
            + "RELATED-TO;RELTYPE=PARENT:parent\r\nRRULE:FREQ=WEEKLY\r\nEND:VTODO\r\n"
            + "BEGIN:VTODO\r\nUID:task\r\nRECURRENCE-ID;VALUE=DATE:20261210\r\nSUMMARY:Другой день\r\nEND:VTODO\r\n"
            + "END:VCALENDAR\r\n";

    public static void main(String[] args) throws Exception {
        Task task = DavClient.parseTask(PAYLOAD);
        check(task != null && "Купить билеты".equals(task.title), "folded summary");
        check(task.notes.equals("Строка\nВторая") && task.due.equals(LocalDate.of(2026, 12, 3)), "readable properties");
        check(task.pinned && task.parent.equals("parent") && task.repeat.equals("remote") && !task.done, "recurrence and parent");
        check(task.raw.equals(PAYLOAD) && DavClient.parseTask("corrupt") == null, "preserved original and rejected invalid");
        check(DavClient.parseTask(PAYLOAD.replace("END:VTODO", "END:VEVENT")) == null, "rejects broken components");

        String digest = DavClient.authorize(Arrays.asList("Basic realm=\"other\"",
                "Digest realm=\"testrealm@host.com\", qop=\"auth,auth-int\","
                + " nonce=\"dcd98b7102dd2f0e8b11d0f600bfb0c093\", opaque=\"5ccc069c403ebaf9f0171e9517f40e41\""),
                "GET", URI.create("https://example.test/dir/index.html"), "Mufasa", "Circle Of Life", "0a4f113b", 1);
        check(digest.contains("response=\"6629fae49393a05397450978507c4ef1\""), "RFC Digest hash");
        check(DavClient.authorize(Collections.singletonList("Basic realm=\"test\""), "REPORT",
                URI.create(BASE), "test", "pass", "nonce", 4).equals("Basic dGVzdDpwYXNz"), "Basic auth");

        Fixture good = new Fixture(false);
        List<DavClient.Collection> lists = new DavClient(BASE, "test", "pass", good).fetch();
        check(good.calls == 5 && lists.size() == 1 && lists.get(0).skipped == 1, "import with invalid sibling task");
        check(lists.get(0).tasks.get(0).raw.contains("RECURRENCE-ID"), "recurrence retained after import");
        try {
            new DavClient(BASE, "test", "pass", new Fixture(true)).fetch();
            throw new AssertionError("cross-origin href accepted");
        } catch (IOException expected) {
            check(expected.getMessage().contains("недопустимый адрес"), "wrong rejection reason");
        }
        System.out.println("CalDAV tests passed.");
    }

    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    private static final class Fixture implements DavClient.Transport {
        int calls;
        final boolean external;
        Fixture(boolean external) { this.external = external; }
        @Override public DavClient.Response send(String method, URI uri, String depth, String body, String authorization) {
            calls++;
            check("dav.example.test".equals(uri.getHost()), "credentials sent off origin");
            check(method.equals("PROPFIND") || method.equals("REPORT"), "server write attempted");
            if (authorization == null) return new DavClient.Response(401, "", Collections.singletonList("Basic realm=\"test\""));
            check(authorization.equals("Basic dGVzdDpwYXNz"), "credentials not passed");
            String path = uri.getPath();
            String data;
            switch (path) {
                case "/dav.php":
                    data = entry(path, "<d:current-user-principal><d:href>" +
                        (external ? "https://other.example.test/" : "/dav.php/principals/demo/") + "</d:href></d:current-user-principal>");
                    break;
                case "/dav.php/principals/demo/":
                    data = entry(path, "<c:calendar-home-set><d:href>/dav.php/calendars/demo/</d:href></c:calendar-home-set>");
                    break;
                case "/dav.php/calendars/demo/":
                    data = entry(path + "tasks/", "<d:resourcetype><c:calendar/></d:resourcetype><d:displayname>Работа</d:displayname>");
                    break;
                case "/dav.php/calendars/demo/tasks/":
                    check(method.equals("REPORT") && body.contains("VTODO"), "wrong report");
                    data = entry(path + "task.ics", "<c:calendar-data><![CDATA[" + PAYLOAD + "]]></c:calendar-data>")
                            + entry(path + "invalid.ics", "<c:calendar-data>broken</c:calendar-data>");
                    break;
                default: throw new AssertionError(path);
            }
            return new DavClient.Response(207, "<d:multistatus xmlns:d='DAV:' xmlns:c='" + CAL + "'>" + data + "</d:multistatus>",
                    Collections.emptyList());
        }
    }

    private static String entry(String href, String properties) {
        return "<d:response><d:href>" + href + "</d:href><d:propstat><d:prop>" + properties
                + "</d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response>";
    }
}
