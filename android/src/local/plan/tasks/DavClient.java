package local.plan.tasks;

import org.w3c.dom.Document;
import org.w3c.dom.Element;
import org.w3c.dom.Node;
import org.xml.sax.InputSource;

import javax.xml.parsers.DocumentBuilderFactory;
import java.io.IOException;
import java.io.StringReader;
import java.net.URI;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.time.DateTimeException;
import java.time.LocalDate;
import java.time.format.DateTimeFormatter;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Base64;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

final class DavClient {
    private static final String DAV = "DAV:";
    private static final String CAL = "urn:ietf:params:xml:ns:caldav";
    static final int MAX_RESPONSE = 12 * 1024 * 1024;
    private static final Pattern AUTH_PARAM =
            Pattern.compile("([a-zA-Z0-9_-]+)\\s*=\\s*(?:\"((?:[^\"\\\\]|\\\\.)*)\"|([^,\\s]+))");

    interface Transport {
        Response send(String method, URI uri, String depth, String body, String authorization) throws IOException;
    }

    static final class Response {
        final int status;
        final String body;
        final List<String> challenges;
        Response(int status, String body, List<String> challenges) {
            this.status = status;
            this.body = body;
            this.challenges = challenges;
        }
    }

    static final class Collection {
        final String id;
        final String title;
        final String account;
        final List<Task> tasks = new ArrayList<>();
        int skipped;
        Collection(String id, String title, String account) {
            this.id = id;
            this.title = title;
            this.account = account;
        }
    }

    private final URI address;
    private final String user;
    private final String password;
    private final Transport transport;
    private List<String> auth;
    private String cnonce;
    private int nonceCount;

    DavClient(String address, String user, String password, Transport transport) {
        this.address = URI.create(address);
        if (!"https".equalsIgnoreCase(this.address.getScheme()) || this.address.getHost() == null
                || this.address.getUserInfo() != null || this.address.getQuery() != null
                || this.address.getFragment() != null || user.isEmpty() || password.isEmpty())
            throw new IllegalArgumentException("Укажи HTTPS-адрес Baïkal, логин и пароль.");
        this.user = user;
        this.password = password;
        this.transport = transport;
    }

    List<Collection> fetch() throws IOException {
        URI principal = resolve(address, requiredHref(request("PROPFIND", address, "0", prop("<d:current-user-principal/>")),
                DAV, "current-user-principal"));
        URI home = resolve(principal, requiredHref(request("PROPFIND", principal, "0", prop("<c:calendar-home-set/>")),
                CAL, "calendar-home-set"));
        Element calendars = request("PROPFIND", home, "1",
                prop("<d:resourcetype/><d:displayname/><c:supported-calendar-component-set/>"));
        List<Collection> result = new ArrayList<>();
        Set<URI> seen = new HashSet<>();
        int count = 0;
        long rawChars = 0;
        for (Element entry : children(calendars, DAV, "response")) {
            Element type = property(entry, DAV, "resourcetype");
            if (type == null || child(type, CAL, "calendar") == null) continue;
            Element components = property(entry, CAL, "supported-calendar-component-set");
            if (components != null && children(components, CAL, "comp").stream()
                    .noneMatch(part -> "VTODO".equals(part.getAttribute("name")))) continue;
            URI url = resolve(home, content(child(entry, DAV, "href")));
            if (!seen.add(url)) continue;
            if (seen.size() > 100) throw new IOException("Слишком много списков CalDAV.");
            String name = content(property(entry, DAV, "displayname"));
            if (name.isEmpty()) {
                String path = url.getPath().replaceFirst("/$", "");
                name = path.substring(path.lastIndexOf('/') + 1);
            }
            Collection list = new Collection(hash("SHA-256", user + "\0" + url), name, user + "@" + address.getHost());
            readTasks(list, url);
            count += list.tasks.size();
            for (Task task : list.tasks) rawChars += task.raw.length();
            if (count > 25000 || rawChars > 16 * 1024 * 1024)
                throw new IOException("Ответ Baïkal слишком большой. Предыдущие задачи сохранены.");
            result.add(list);
        }
        if (result.isEmpty()) throw new IOException("В этой учётной записи нет списков задач.");
        return result;
    }

    private void readTasks(Collection list, URI url) throws IOException {
        Element report = request("REPORT", url, "1",
                "<c:calendar-query xmlns:d='DAV:' xmlns:c='" + CAL + "'>"
                + "<d:prop><c:calendar-data/></d:prop><c:filter><c:comp-filter name='VCALENDAR'>"
                + "<c:comp-filter name='VTODO'/></c:comp-filter></c:filter></c:calendar-query>");
        Map<String, Task> parsed = new LinkedHashMap<>();
        Set<String> duplicates = new HashSet<>();
        Set<URI> seen = new HashSet<>();
        String base = url.getPath().endsWith("/") ? url.getPath() : url.getPath() + "/";
        for (Element entry : children(report, DAV, "response")) {
            URI href = resolve(url, content(child(entry, DAV, "href")));
            if (!href.getPath().startsWith(base) || href.getPath().equals(base) || !seen.add(href))
                throw new IOException("CalDAV вернул неоднозначный список задач.");
            Element data = property(entry, CAL, "calendar-data");
            Task task = data == null ? null : parseTask(data.getTextContent());
            if (task == null || duplicates.contains(task.id)) {
                list.skipped++;
            } else if (parsed.containsKey(task.id)) {
                parsed.remove(task.id);
                duplicates.add(task.id);
                list.skipped += 2;
            } else {
                parsed.put(task.id, task);
            }
        }
        for (Task task : parsed.values()) {
            task.id = hash("SHA-256", list.id + "\0" + task.id);
            task.project = list.id;
            if (!task.parent.isEmpty()) task.parent = hash("SHA-256", list.id + "\0" + task.parent);
            list.tasks.add(task);
        }
    }

    static Task parseTask(String data) {
        if (data.isEmpty() || data.indexOf('\0') >= 0) return null;
        String unfolded = data.replaceAll("\\r?\\n[ \\t]", "");
        String[] lines = unfolded.split("\\r?\\n");
        if (lines.length < 4 || !"BEGIN:VCALENDAR".equalsIgnoreCase(lines[0].trim())
                || !"END:VCALENDAR".equalsIgnoreCase(lines[lines.length-1].trim())) return null;
        List<Map<String, String>> components = new ArrayList<>();
        Map<String, String> part = null;
        ArrayDeque<String> componentsOpen = new ArrayDeque<>();
        for (String line : lines) {
            int colon = valueSeparator(line);
            if (colon < 1) continue;
            String name = line.substring(0, colon);
            String key = name.split(";", 2)[0].toUpperCase(Locale.ROOT);
            String value = line.substring(colon + 1);
            if ("BEGIN".equals(key)) {
                componentsOpen.addLast(value.toUpperCase(Locale.ROOT));
                if (componentsOpen.size() == 2 && "VTODO".equalsIgnoreCase(value)) part = new HashMap<>();
            } else if ("END".equals(key)) {
                if (componentsOpen.size() == 2 && part != null) {
                    if (!"VTODO".equalsIgnoreCase(value)) return null;
                    components.add(part);
                    part = null;
                }
                if (!value.equalsIgnoreCase(componentsOpen.pollLast())) return null;
            } else if (componentsOpen.size() == 2 && part != null && (!"RELATED-TO".equals(key)
                    || !name.toUpperCase(Locale.ROOT).contains("RELTYPE=")
                    || name.toUpperCase(Locale.ROOT).matches(".*;RELTYPE=\"?PARENT\"?(;.*)?"))) {
                if ("CATEGORIES".equals(key)) part.merge(key, value, (old, next) -> old + "," + next);
                else part.putIfAbsent(key, value);
            }
        }
        if (!componentsOpen.isEmpty() || components.isEmpty()) return null;
        Map<String, String> master = null;
        String uid = value(components.get(0), "UID");
        if (uid.trim().isEmpty()) return null;
        for (Map<String, String> entry : components) {
            if (!uid.equals(value(entry, "UID"))) return null;
            if (!entry.containsKey("RECURRENCE-ID")) {
                if (master != null) return null;
                master = entry;
            }
        }
        if (master == null && components.size() != 1) return null;
        Map<String, String> fields = master == null ? components.get(0) : master;
        Task task = new Task();
        task.id = uid;
        task.parent = value(fields, "RELATED-TO");
        if (task.parent.equals(uid)) task.parent = "";
        task.title = value(fields, "SUMMARY");
        if (task.title.isEmpty()) task.title = "Без названия";
        task.notes = value(fields, "DESCRIPTION");
        task.tags = value(fields, "CATEGORIES");
        task.due = date(value(fields, "DUE"));
        LocalDate start = date(value(fields, "DTSTART"));
        if (start != null) {
            task.deadline = task.due;
            task.due = start;
        }
        int priority = number(value(fields, "PRIORITY"));
        task.priority = priority <= 0 ? 0 : priority <= 4 ? 1 : priority == 5 ? 2 : 3;
        task.done = "COMPLETED".equalsIgnoreCase(value(fields, "STATUS"))
                || number(value(fields, "PERCENT-COMPLETE")) == 100;
        String pinned = value(fields, "X-PINNED");
        task.pinned = "TRUE".equalsIgnoreCase(pinned) || "1".equals(pinned);
        if (components.size() > 1 || fields.containsKey("RRULE")) task.repeat = "remote";
        task.remote = true;
        task.raw = data;
        return task;
    }

    private static int valueSeparator(String line) {
        boolean quoted = false;
        for (int i = 0; i < line.length(); i++) {
            char ch = line.charAt(i);
            if (ch == '"') quoted = !quoted;
            if (ch == ':' && !quoted) return i;
        }
        return -1;
    }

    private static String value(Map<String, String> fields, String key) {
        String raw = fields.getOrDefault(key, "");
        StringBuilder value = new StringBuilder(raw.length());
        for (int i = 0; i < raw.length(); i++) {
            char ch = raw.charAt(i);
            if (ch == '\\' && i + 1 < raw.length()) {
                char next = raw.charAt(++i);
                if (next == 'n' || next == 'N') ch = '\n';
                else if (next == ',' || next == ';' || next == '\\') ch = next;
                else { value.append(ch); ch = next; }
            }
            value.append(ch);
        }
        return value.toString();
    }

    private static int number(String value) {
        try { return Integer.parseInt(value); } catch (NumberFormatException ignored) { return 0; }
    }

    private static LocalDate date(String value) {
        if (value.length() < 8) return null;
        try {
            LocalDate result = LocalDate.parse(value.substring(0, 8), DateTimeFormatter.BASIC_ISO_DATE);
            return result.getYear() < 1 ? null : result;
        } catch (DateTimeException ignored) {
            return null;
        }
    }

    private Element request(String method, URI uri, String depth, String body) throws IOException {
        String authorization = auth == null ? null : authorize(auth, method, uri, user, password, cnonce, ++nonceCount);
        Response response = transport.send(method, uri, depth, body, authorization);
        if (response.status == 401) {
            auth = response.challenges;
            cnonce = UUID.randomUUID().toString().replace("-", "");
            nonceCount = 1;
            response = transport.send(method, uri, depth, body, authorize(auth, method, uri, user, password, cnonce, nonceCount));
        }
        if (response.status == 401 || response.status == 403)
            throw new IOException("Baïkal отклонил вход. Проверь логин и пароль.");
        if (response.status >= 300 && response.status < 400)
            throw new IOException("Baïkal перенаправил запрос. Укажи конечный адрес dav.php.");
        if (response.status != 207) throw new IOException("Baïkal ответил с кодом " + response.status + ".");
        try {
            String xml = response.body;
            if (xml.length() > MAX_RESPONSE || xml.contains("<!DOCTYPE")) throw new IOException();
            DocumentBuilderFactory factory = DocumentBuilderFactory.newInstance();
            factory.setNamespaceAware(true);
            factory.setExpandEntityReferences(false);
            javax.xml.parsers.DocumentBuilder parser = factory.newDocumentBuilder();
            parser.setEntityResolver((publicId, systemId) -> { throw new org.xml.sax.SAXException("External entity"); });
            parser.setErrorHandler(new org.xml.sax.helpers.DefaultHandler() {
                @Override public void fatalError(org.xml.sax.SAXParseException e) throws org.xml.sax.SAXException { throw e; }
            });
            Document doc = parser.parse(new InputSource(new StringReader(xml)));
            Element root = doc.getDocumentElement();
            if (!DAV.equals(root.getNamespaceURI()) || !"multistatus".equals(root.getLocalName())) throw new IOException();
            for (Node node = root.getFirstChild(); node != null; node = node.getNextSibling()) {
                if (node instanceof Element && (!DAV.equals(node.getNamespaceURI())
                        || !("response".equals(node.getLocalName()) || "responsedescription".equals(node.getLocalName()))))
                    throw new IOException();
            }
            return root;
        } catch (Exception e) {
            throw new IOException("Ответ CalDAV повреждён. Предыдущие задачи сохранены.", e);
        }
    }

    static String authorize(List<String> challenges, String method, URI uri, String user, String secret,
                            String cnonce, int nonceCount) throws IOException {
        for (String challenge : challenges) {
            if (!challenge.regionMatches(true, 0, "Digest ", 0, 7)) continue;
            Map<String, String> params = new HashMap<>();
            Matcher matcher = AUTH_PARAM.matcher(challenge.substring(7));
            while (matcher.find()) params.put(matcher.group(1).toLowerCase(Locale.ROOT),
                    matcher.group(2) != null ? matcher.group(2).replaceAll("\\\\(.)", "$1") : matcher.group(3));
            String algorithm = params.getOrDefault("algorithm", "MD5").toUpperCase(Locale.ROOT);
            boolean session = algorithm.endsWith("-SESS");
            String digest = session ? algorithm.substring(0, algorithm.length() - 5) : algorithm;
            if (!"MD5".equals(digest) && !"SHA-256".equals(digest) && !"SHA-512-256".equals(digest)) continue;
            if ("SHA-512-256".equals(digest)) digest = "SHA-512/256";
            String realm = params.get("realm"), nonce = params.get("nonce"), qop = params.get("qop");
            if (realm == null || nonce == null || (qop != null && !Pattern.compile("(^|,\\s*)auth(\\s*,|$)").matcher(qop.trim()).find()))
                continue;
            String path = uri.getRawPath();
            if (path == null || path.isEmpty()) path = "/";
            String nc = String.format(Locale.ROOT, "%08x", nonceCount);
            String a1 = hash(digest, user + ":" + realm + ":" + secret);
            if (session) a1 = hash(digest, a1 + ":" + nonce + ":" + cnonce);
            String a2 = hash(digest, method + ":" + path);
            String response = hash(digest, a1 + ":" + nonce + ":"
                    + (qop == null ? "" : nc + ":" + cnonce + ":auth:") + a2);
            String header = "Digest username=" + quote(user) + ", realm=" + quote(realm) + ", nonce=" + quote(nonce)
                    + ", uri=" + quote(path) + ", algorithm=" + algorithm + ", response=" + quote(response);
            if (qop != null) header += ", qop=auth, nc=" + nc + ", cnonce=" + quote(cnonce);
            else if (session) header += ", cnonce=" + quote(cnonce);
            if (params.containsKey("opaque")) header += ", opaque=" + quote(params.get("opaque"));
            return header;
        }
        for (String challenge : challenges) if (challenge.regionMatches(true, 0, "Basic", 0, 5))
            return "Basic " + Base64.getEncoder().encodeToString((user + ":" + secret).getBytes(StandardCharsets.UTF_8));
        throw new IOException("Baïkal предложил неподдерживаемый способ входа.");
    }

    private static String quote(String value) throws IOException {
        if (value.indexOf('\r') >= 0 || value.indexOf('\n') >= 0) throw new IOException("Некорректный ответ авторизации.");
        return "\"" + value.replace("\\", "\\\\").replace("\"", "\\\"") + "\"";
    }

    private static String hash(String algorithm, String value) throws IOException {
        try {
            byte[] bytes = MessageDigest.getInstance(algorithm).digest(value.getBytes(StandardCharsets.UTF_8));
            StringBuilder result = new StringBuilder(bytes.length * 2);
            for (byte b : bytes) result.append(Character.forDigit((b >> 4) & 15, 16)).append(Character.forDigit(b & 15, 16));
            return result.toString();
        } catch (java.security.NoSuchAlgorithmException e) {
            throw new IOException("Устройство не поддерживает способ входа Baïkal.", e);
        }
    }

    private static URI resolve(URI base, String href) throws IOException {
        try {
            URI url = base.resolve(href).normalize();
            if (href.isEmpty() || !"https".equalsIgnoreCase(url.getScheme()) || url.getHost() == null
                    || !base.getHost().equalsIgnoreCase(url.getHost()) || port(base) != port(url)
                    || url.getUserInfo() != null || url.getFragment() != null || url.getQuery() != null)
                throw new IllegalArgumentException();
            return url;
        } catch (IllegalArgumentException e) {
            throw new IOException("CalDAV вернул недопустимый адрес.", e);
        }
    }

    private static int port(URI url) { return url.getPort() == -1 ? 443 : url.getPort(); }

    private static String prop(String inner) {
        return "<d:propfind xmlns:d='DAV:' xmlns:c='" + CAL + "'><d:prop>" + inner + "</d:prop></d:propfind>";
    }

    private static String requiredHref(Element root, String namespace, String name) throws IOException {
        for (Element response : children(root, DAV, "response")) {
            Element prop = property(response, namespace, name);
            String href = prop == null ? "" : content(child(prop, DAV, "href"));
            if (!href.isEmpty()) return href;
        }
        throw new IOException("CalDAV не сообщил адрес списков. Проверь адрес dav.php.");
    }

    private static Element property(Element response, String namespace, String name) {
        for (Element propstat : children(response, DAV, "propstat")) {
            if (!content(child(propstat, DAV, "status")).matches("HTTP/\\S+ 2[0-9]{2}(?:\\s.*)?")) continue;
            Element container = child(propstat, DAV, "prop");
            if (container == null) continue;
            Element hit = child(container, namespace, name);
            if (hit != null) return hit;
        }
        return null;
    }

    private static String content(Element element) { return element == null ? "" : element.getTextContent().trim(); }

    private static Element child(Element parent, String namespace, String name) {
        for (Node node = parent.getFirstChild(); node != null; node = node.getNextSibling())
            if (node instanceof Element && namespace.equals(node.getNamespaceURI()) && name.equals(node.getLocalName()))
                return (Element) node;
        return null;
    }

    private static List<Element> children(Element parent, String namespace, String name) {
        List<Element> result = new ArrayList<>();
        for (Node node = parent.getFirstChild(); node != null; node = node.getNextSibling())
            if (node instanceof Element && namespace.equals(node.getNamespaceURI()) && name.equals(node.getLocalName()))
                result.add((Element) node);
        return result;
    }
}
