#include "redactor.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace
{
bool isWordChar(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_');
}

// A value that starts or ends with a letter or digit must stand as a word
// there, so the user name "sam" leaves "sample" alone.
bool standsAlone(const QString &text, qsizetype start, qsizetype length)
{
    if (start > 0 && isWordChar(text.at(start)) && isWordChar(text.at(start - 1))) {
        return false;
    }
    const qsizetype end = start + length;
    if (end < text.size() && isWordChar(text.at(end - 1)) && isWordChar(text.at(end))) {
        return false;
    }
    return true;
}

// Folders every home has, which say nothing about whose it is. Everything
// else under a home folder is named by the person, so it goes.
const QSet<QString> &standardFolders()
{
    static const QSet<QString> names{
        QStringLiteral("Desktop"),   QStringLiteral("Documents"), QStringLiteral("Downloads"),   QStringLiteral("Music"),
        QStringLiteral("Pictures"),  QStringLiteral("Videos"),    QStringLiteral("Public"),      QStringLiteral("Templates"),
        QStringLiteral("Notes"),     QStringLiteral("Screenshots"), QStringLiteral("Screen recordings"), QStringLiteral("Recordings"),
        QStringLiteral("DCIM"),      QStringLiteral("Camera"),
    };
    return names;
}

// Host names that come with a system rather than being chosen, and appear
// in program names too ("wildos-files"); removing them would garble the log
// and protect nothing.
bool genericHost(const QString &host)
{
    static const QSet<QString> names{QStringLiteral("localhost"), QStringLiteral("wildos"), QStringLiteral("debian"),
                                     QStringLiteral("kicksecure"), QStringLiteral("live"), QStringLiteral("host")};
    return host.size() < 2 || names.contains(host.toLower());
}

bool privateIpv4Prefix(int a, int b, int *keep)
{
    if (a == 10) {
        *keep = 1;
        return true;
    }
    if ((a == 192 && b == 168) || (a == 172 && b >= 16 && b <= 31) || (a == 169 && b == 254)) {
        *keep = 2;
        return true;
    }
    return false;
}
}

void Redactor::setKnown(const Known &known)
{
    m_known = known;
}

void Redactor::setCustomWords(const QStringList &words)
{
    m_custom.clear();
    for (const QString &word : words) {
        const QString trimmed = word.trimmed();
        if (!trimmed.isEmpty() && !m_custom.contains(trimmed, Qt::CaseInsensitive)) {
            m_custom << trimmed;
        }
    }
}

void Redactor::reset()
{
    m_tokens.clear();
    std::fill(std::begin(m_counts), std::end(m_counts), 0);
}

int Redactor::total() const
{
    int sum = 0;
    for (int n : m_counts) {
        sum += n;
    }
    return sum;
}

int Redactor::count(Category category) const
{
    return category >= 0 && category < CategoryCount ? m_counts[category] : 0;
}

QString Redactor::categoryLabel(Category category)
{
    switch (category) {
    case Names:
        return QCoreApplication::translate("Redactor", "Your name and username");
    case PcName:
        return QCoreApplication::translate("Redactor", "PC name");
    case Wifi:
        return QCoreApplication::translate("Redactor", "Wi-Fi names");
    case Addresses:
        return QCoreApplication::translate("Redactor", "IP and MAC addresses");
    case Serials:
        return QCoreApplication::translate("Redactor", "Serial numbers");
    case Emails:
        return QCoreApplication::translate("Redactor", "Email addresses");
    case Files:
        return QCoreApplication::translate("Redactor", "File and folder names");
    case Custom:
        return QCoreApplication::translate("Redactor", "Words you added");
    case CategoryCount:
        break;
    }
    return {};
}

QString Redactor::token(const QString &kind, const QString &value, Category category, bool numbered)
{
    Q_UNUSED(category)
    if (!numbered) {
        return QLatin1Char('[') + kind + QLatin1Char(']');
    }
    // File names and network names are told apart by case; the rest are not.
    const bool exact = kind == QLatin1String("file") || kind == QLatin1String("folder") || kind == QLatin1String("wifi");
    const QString key = exact ? value : value.toCaseFolded();
    auto &tokens = m_tokens[kind];
    auto it = tokens.constFind(key);
    if (it != tokens.constEnd()) {
        return it.value();
    }
    const QString made = QStringLiteral("[%1-%2]").arg(kind).arg(tokens.size() + 1);
    tokens.insert(key, made);
    return made;
}

void Redactor::claim(QList<Span> &spans, QList<bool> &taken, qsizetype start, qsizetype length, const QString &kind, const QString &value,
                     Category category, bool numbered)
{
    if (length <= 0) {
        return;
    }
    for (qsizetype i = start; i < start + length; ++i) {
        if (taken.at(i)) {
            return;
        }
    }
    for (qsizetype i = start; i < start + length; ++i) {
        taken[i] = true;
    }
    spans.append({start, length, kind, value, category, numbered});
}

void Redactor::literal(const QString &text, QList<Span> &spans, QList<bool> &taken, const QString &value, const QString &kind, Category category,
                       Qt::CaseSensitivity cs, bool numbered)
{
    if (value.isEmpty()) {
        return;
    }
    qsizetype from = 0;
    while (true) {
        const qsizetype at = text.indexOf(value, from, cs);
        if (at < 0) {
            break;
        }
        from = at + value.size();
        if (!standsAlone(text, at, value.size())) {
            continue;
        }
        claim(spans, taken, at, value.size(), kind, value, category, numbered);
    }
}

// One path under a home folder or a mounted drive, from `start` for
// `length` characters: the account's name, the drive's name and every name
// the person chose go; the standard folders and programs' own hidden
// folders stay, so the path still says where it was.
void Redactor::pathSpans(const QString &text, qsizetype start, qsizetype length, QList<Span> &spans, QList<bool> &taken)
{
    const QString path = text.mid(start, length);
    struct Part {
        qsizetype at;
        QString name;
    };
    QList<Part> parts;
    qsizetype i = 0;
    while (i < path.size()) {
        const qsizetype slash = path.indexOf(QLatin1Char('/'), i);
        const qsizetype end = slash < 0 ? path.size() : slash;
        if (end > i) {
            parts.append({i, path.mid(i, end - i)});
        }
        i = end + 1;
    }
    if (parts.isEmpty()) {
        return;
    }

    int first = 0;
    bool drive = false;
    if (parts.first().name == QLatin1String("~")) {
        first = 1;
    } else if (parts.first().name == QLatin1String("home")) {
        first = 1;
    } else if (parts.first().name == QLatin1String("media")) {
        first = 1;
        drive = true;
    } else if (parts.first().name == QLatin1String("run") && parts.size() > 1 && parts.at(1).name == QLatin1String("media")) {
        first = 2;
        drive = true;
    } else {
        return;
    }

    int next = first;
    if (parts.first().name != QLatin1String("~") && next < parts.size()) {
        const Part &owner = parts.at(next);
        const QString name = owner.name;
        if (!m_known.userName.isEmpty() && name.compare(m_known.userName, Qt::CaseInsensitive) == 0) {
            claim(spans, taken, start + owner.at, name.size(), QStringLiteral("user"), name, Names, false);
        } else {
            claim(spans, taken, start + owner.at, name.size(), QStringLiteral("account"), name, Names);
        }
        ++next;
    }
    if (drive && next < parts.size()) {
        const Part &label = parts.at(next);
        claim(spans, taken, start + label.at, label.name.size(), QStringLiteral("usb"), label.name, Files);
        ++next;
    }

    static const QRegularExpression extension(QStringLiteral("^.+\\.[A-Za-z0-9]{1,6}$"));
    bool hidden = false;
    bool trash = false;
    for (int n = next; n < parts.size(); ++n) {
        const Part &part = parts.at(n);
        const QString &name = part.name;
        if (name == QLatin1String(".") || name == QLatin1String("..")) {
            continue;
        }
        if (!trash && name.startsWith(QLatin1Char('.'))) {
            hidden = true;
            continue;
        }
        if (hidden && !trash) {
            // Inside a program's hidden folder the names are the program's,
            // except for the bin, where they are the files that were binned.
            if (name == QLatin1String("Trash")) {
                if (n + 1 < parts.size()) {
                    ++n;
                }
                trash = true;
            }
            continue;
        }
        if (!trash && standardFolders().contains(name)) {
            continue;
        }
        const bool file = n == parts.size() - 1 && extension.match(name).hasMatch();
        const QString kind = file ? QStringLiteral("file") : QStringLiteral("folder");
        claim(spans, taken, start + part.at, name.size(), kind, name, Files);
    }
}

void Redactor::paths(const QString &text, QList<Span> &spans, QList<bool> &taken)
{
    // Quoted paths may hold spaces; take them whole first.
    static const QRegularExpression quoted(QStringLiteral("(['\"])((?:~|/home|/media|/run/media)/[^'\"\\n]+)\\1"));
    auto it = quoted.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        pathSpans(text, m.capturedStart(2), m.capturedLength(2), spans, taken);
    }
    static const QRegularExpression bare(QStringLiteral("(?<![\\w~.-])(?:~|/home|/media|/run/media)(?:/[^\\s/'\"<>|;:,()\\[\\]{}=*?]+)+"));
    it = bare.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        qsizetype length = m.capturedLength();
        // A path at the end of a sentence keeps its full stop.
        while (length > 0 && text.at(m.capturedStart() + length - 1) == QLatin1Char('.')) {
            --length;
        }
        pathSpans(text, m.capturedStart(), length, spans, taken);
    }
}

QString Redactor::redact(const QString &text)
{
    if (text.isEmpty()) {
        return text;
    }
    QList<Span> spans;
    QList<bool> taken(text.size(), false);

    // The order is the precedence: something matched earlier is never
    // matched again by a later, looser rule.
    QStringList custom = m_custom;
    std::sort(custom.begin(), custom.end(), [](const QString &a, const QString &b) { return a.size() > b.size(); });
    for (const QString &word : custom) {
        literal(text, spans, taken, word, QStringLiteral("word"), Custom, Qt::CaseInsensitive, true);
    }

    static const QRegularExpression email(QStringLiteral("[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(?:\\.[A-Za-z0-9-]+)*\\.[A-Za-z]{2,}"));
    for (auto it = email.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        // systemd's template units read like addresses
        // (systemd-coredump@0-123-0.service, user@1000.service).
        static const QSet<QString> units{QStringLiteral("service"), QStringLiteral("socket"), QStringLiteral("timer"), QStringLiteral("path"),
                                         QStringLiteral("mount"),   QStringLiteral("scope"),  QStringLiteral("slice"), QStringLiteral("target"),
                                         QStringLiteral("device"),  QStringLiteral("swap"),   QStringLiteral("automount")};
        if (units.contains(m.captured().section(QLatin1Char('.'), -1))) {
            continue;
        }
        claim(spans, taken, m.capturedStart(), m.capturedLength(), QStringLiteral("email"), m.captured(), Emails);
    }

    paths(text, spans, taken);

    static const QRegularExpression mac(QStringLiteral("(?<![0-9A-Fa-f:-])(?:[0-9A-Fa-f]{2}([:-]))(?:[0-9A-Fa-f]{2}\\1){4}[0-9A-Fa-f]{2}(?![0-9A-Fa-f:-])"));
    for (auto it = mac.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        const QString value = m.captured().toLower().replace(QLatin1Char('-'), QLatin1Char(':'));
        if (value == QLatin1String("00:00:00:00:00:00") || value == QLatin1String("ff:ff:ff:ff:ff:ff")) {
            continue;
        }
        claim(spans, taken, m.capturedStart(), m.capturedLength(), QStringLiteral("mac"), value, Addresses);
    }

    for (const QString &id : m_known.machineIds) {
        literal(text, spans, taken, id, QStringLiteral("id"), Serials, Qt::CaseInsensitive, true);
    }
    QStringList serials = m_known.serials;
    std::sort(serials.begin(), serials.end(), [](const QString &a, const QString &b) { return a.size() > b.size(); });
    for (const QString &serial : serials) {
        if (serial.size() >= 4) {
            literal(text, spans, taken, serial, QStringLiteral("serial"), Serials, Qt::CaseInsensitive, true);
        }
    }
    static const QRegularExpression serialField(QStringLiteral(
        "(?i)(?<![\\w])(?:serial(?:[ _]?(?:number|no\\.?))?|s/n|sn|iserialnumber|serialnumber|id_serial(?:_short)?|(?:product|board|chassis)_serial)"
        "\\s*[:=]\\s*\"?([A-Za-z0-9][A-Za-z0-9_.-]{3,})"));
    for (auto it = serialField.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        claim(spans, taken, m.capturedStart(1), m.capturedLength(1), QStringLiteral("serial"), m.captured(1), Serials);
    }
    static const QRegularExpression iSerial(QStringLiteral("(?<![\\w])iSerial\\s+\\d+\\s+([A-Za-z0-9][A-Za-z0-9_.-]{3,})"));
    for (auto it = iSerial.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        claim(spans, taken, m.capturedStart(1), m.capturedLength(1), QStringLiteral("serial"), m.captured(1), Serials);
    }
    // Machine, boot and disk IDs: any UUID, and any bare 32 digit hex
    // number, the form /etc/machine-id and the boot ID take in logs.
    static const QRegularExpression uuid(QStringLiteral("(?<![0-9A-Fa-f-])[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}(?![0-9A-Fa-f-])"));
    for (auto it = uuid.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        const QString bare = m.captured().remove(QLatin1Char('-'));
        claim(spans, taken, m.capturedStart(), m.capturedLength(), QStringLiteral("id"), bare, Serials);
    }
    static const QRegularExpression hex32(QStringLiteral("(?<![0-9A-Za-z])[0-9a-f]{32}(?![0-9A-Za-z])"));
    for (auto it = hex32.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        claim(spans, taken, m.capturedStart(), m.capturedLength(), QStringLiteral("id"), m.captured(), Serials);
    }

    static const QRegularExpression ipv6(QStringLiteral("(?<![\\w:.])[0-9A-Fa-f]{0,4}(?::[0-9A-Fa-f]{0,4}){2,7}(?![\\w:])"));
    for (auto it = ipv6.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        const QString value = m.captured();
        const QStringList groups = value.split(QLatin1Char(':'));
        const bool compressed = value.contains(QLatin1String("::"));
        int filled = 0;
        bool ok = true;
        for (const QString &g : groups) {
            if (!g.isEmpty()) {
                ++filled;
            }
        }
        if (compressed) {
            // At most one "::", and enough to be an address rather than
            // C++'s "a::b".
            ok = value.count(QLatin1String("::")) == 1 && (filled >= 3 || (filled >= 2 && groups.first().size() >= 3));
        } else {
            ok = groups.size() == 8 && filled == 8;
        }
        if (!ok || value == QLatin1String("::1")) {
            continue;
        }
        claim(spans, taken, m.capturedStart(), m.capturedLength(), QStringLiteral("ip"), value.toLower(), Addresses);
    }

    static const QRegularExpression ipv4(QStringLiteral("(?<![\\w.])(\\d{1,3})\\.(\\d{1,3})\\.(\\d{1,3})\\.(\\d{1,3})(?![\\w+~-]|\\.\\d)"));
    for (auto it = ipv4.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        int octets[4];
        bool ok = true;
        for (int n = 0; n < 4; ++n) {
            octets[n] = m.captured(n + 1).toInt();
            ok = ok && octets[n] <= 255;
        }
        if (!ok || octets[0] == 127 || octets[0] == 0 || octets[0] == 255) {
            continue;
        }
        // Version numbers have four parts too (a kernel's 6.6.87.2-...),
        // which the lookahead skips by their suffix. A private address keeps
        // its network part (192.168.[ip-1]): it
        // says the problem is on a home network, and nothing about whose.
        int keep = 0;
        privateIpv4Prefix(octets[0], octets[1], &keep);
        const qsizetype from = keep == 0 ? m.capturedStart() : m.capturedEnd(keep) + 1;
        claim(spans, taken, from, m.capturedEnd() - from, QStringLiteral("ip"), m.captured(), Addresses);
    }

    QStringList wifi = m_known.wifi;
    std::sort(wifi.begin(), wifi.end(), [](const QString &a, const QString &b) { return a.size() > b.size(); });
    for (const QString &name : wifi) {
        if (name.trimmed().size() >= 3) {
            literal(text, spans, taken, name, QStringLiteral("wifi"), Wifi, Qt::CaseSensitive, true);
        }
    }

    if (!genericHost(m_known.hostName)) {
        literal(text, spans, taken, m_known.hostName, QStringLiteral("pc"), PcName, Qt::CaseInsensitive, false);
    }

    const QString realName = m_known.realName.trimmed();
    if (!realName.isEmpty()) {
        literal(text, spans, taken, realName, QStringLiteral("name"), Names, Qt::CaseInsensitive, false);
        const QStringList parts = realName.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (parts.size() > 1) {
            for (const QString &part : parts) {
                if (part.size() >= 3) {
                    literal(text, spans, taken, part, QStringLiteral("name"), Names, Qt::CaseSensitive, false);
                }
            }
        }
    }
    for (const QString &name : m_known.otherNames) {
        if (name.trimmed().size() >= 3) {
            literal(text, spans, taken, name.trimmed(), QStringLiteral("account"), Names, Qt::CaseInsensitive, true);
        }
    }
    if (m_known.userName.size() >= 2) {
        literal(text, spans, taken, m_known.userName, QStringLiteral("user"), Names, Qt::CaseInsensitive, false);
    }
    for (const QString &name : m_known.otherUsers) {
        if (name.size() >= 2) {
            literal(text, spans, taken, name, QStringLiteral("account"), Names, Qt::CaseInsensitive, true);
        }
    }

    if (spans.isEmpty()) {
        return text;
    }
    // Tokens are numbered in the order their values first appear.
    std::sort(spans.begin(), spans.end(), [](const Span &a, const Span &b) { return a.start < b.start; });
    QString out;
    out.reserve(text.size());
    qsizetype at = 0;
    for (const Span &span : std::as_const(spans)) {
        out += QStringView(text).mid(at, span.start - at);
        out += token(span.kind, span.value, span.category, span.numbered);
        at = span.start + span.length;
        ++m_counts[span.category];
    }
    out += QStringView(text).mid(at);
    return out;
}
