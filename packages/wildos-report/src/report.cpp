#include "report.h"
#include "issuewatch.h"
#include "parts.h"
#include "sysinfo.h"

#include <KWaylandExtras>
#include <KWindowSystem>
#include <KZip>

#include <QBuffer>
#include <QClipboard>
#include <QColorSpace>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDesktopServices>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <QtConcurrentRun>

#include <unistd.h>

struct PickerRule {
    uint type = 0;
    QString pattern;
};
struct PickerFilter {
    QString name;
    QList<PickerRule> rules;
};
Q_DECLARE_METATYPE(PickerRule)
Q_DECLARE_METATYPE(PickerFilter)

QDBusArgument &operator<<(QDBusArgument &arg, const PickerRule &rule)
{
    arg.beginStructure();
    arg << rule.type << rule.pattern;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, PickerRule &rule)
{
    arg.beginStructure();
    arg >> rule.type >> rule.pattern;
    arg.endStructure();
    return arg;
}

QDBusArgument &operator<<(QDBusArgument &arg, const PickerFilter &filter)
{
    arg.beginStructure();
    arg << filter.name << filter.rules;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, PickerFilter &filter)
{
    arg.beginStructure();
    arg >> filter.name >> filter.rules;
    arg.endStructure();
    return arg;
}

namespace
{
const QString issuesUrl = QStringLiteral("https://github.com/wildorigins/wildos/issues");
// GitHub turns away longer addresses; under 8 KB is safe in every browser.
constexpr int urlBudget = 8000;

QString sizeName(qint64 bytes)
{
    if (bytes < 1024) {
        return QStringLiteral("%1 bytes").arg(bytes);
    }
    if (bytes < 1024 * 1024) {
        return QStringLiteral("%1 KB").arg(qMax<qint64>(1, qRound64(bytes / 1024.0)));
    }
    return QStringLiteral("%1 MB").arg(QLocale(QLocale::English, QLocale::UnitedKingdom).toString(bytes / (1024.0 * 1024.0), 'f', 1));
}

// A log line as it reads in the saved file: "13:09:58  files: message".
QString fileLines(const QString &lines)
{
    QString out;
    out.reserve(lines.size() + lines.size() / 8);
    for (const QString &line : lines.split(QLatin1Char('\n'))) {
        const QStringList cols = line.split(QLatin1Char('\t'));
        out += cols.size() >= 3 ? cols.at(0) + QStringLiteral("  ") + cols.at(1) + QStringLiteral(": ") + cols.mid(2).join(QLatin1Char(' ')) : line;
        out += QLatin1Char('\n');
    }
    return out;
}

struct Redacted {
    QStringList texts;
    QList<QList<int>> counts;
};

Redacted redactTexts(const Redactor::Known &known, const QStringList &custom, const QStringList &texts)
{
    Redactor redactor;
    redactor.setKnown(known);
    redactor.setCustomWords(custom);
    redactor.reset();
    Redacted out;
    for (const QString &text : texts) {
        int before[Redactor::CategoryCount];
        for (int c = 0; c < Redactor::CategoryCount; ++c) {
            before[c] = redactor.count(Redactor::Category(c));
        }
        out.texts << redactor.redact(text);
        QList<int> counts;
        for (int c = 0; c < Redactor::CategoryCount; ++c) {
            counts << redactor.count(Redactor::Category(c)) - before[c];
        }
        out.counts << counts;
    }
    return out;
}
}

void PreviewModel::setLines(const QStringList &lines, bool log)
{
    beginResetModel();
    m_lines = lines;
    m_log = log;
    endResetModel();
    Q_EMIT countChanged();
}

int PreviewModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_lines.size());
}

QVariant PreviewModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_lines.size()) {
        return {};
    }
    const QString &line = m_lines.at(index.row());
    if (!m_log) {
        return role == TextRole ? QVariant(line) : role == LogRole ? QVariant(false) : QVariant(QString());
    }
    const qsizetype first = line.indexOf(QLatin1Char('\t'));
    const qsizetype second = first < 0 ? -1 : line.indexOf(QLatin1Char('\t'), first + 1);
    switch (role) {
    case TimeRole:
        return second < 0 ? QString() : line.left(first);
    case SourceRole:
        return second < 0 ? QString() : line.mid(first + 1, second - first - 1);
    case TextRole:
        return second < 0 ? line : line.mid(second + 1);
    case LogRole:
        return second >= 0;
    }
    return {};
}

QHash<int, QByteArray> PreviewModel::roleNames() const
{
    return {{TimeRole, "time"}, {SourceRole, "source"}, {TextRole, "text"}, {LogRole, "log"}};
}

Report::Report(QObject *parent)
    : QObject(parent)
    
{
    m_pickTime = QTime::currentTime().addSecs(-3600).toString(QStringLiteral("HH:mm"));
    const struct {
        const char *id;
        const char *file;
        bool included;
    } sections[] = {
        {"description", "report.md", true}, {"event", "history-event.txt", true},     {"about", "about-this-pc.txt", true},
        {"crash", "crash.txt", true},
        {"logs", "logs.txt", true},         {"updates", "recent-updates.txt", true}, {"fulllog", "full-system-log.txt", false},
    };
    for (const auto &s : sections) {
        Section section;
        section.id = QString::fromLatin1(s.id);
        section.file = QString::fromLatin1(s.file);
        section.included = s.included;
        m_sections << section;
    }
    connect(&m_collector, &Collector::finished, this, &Report::collected);
    m_watchTimer.setSingleShot(true);
    connect(&m_watchTimer, &QTimer::timeout, this, &Report::watchOnce);
    m_capturePoll.setInterval(250);
    connect(&m_capturePoll, &QTimer::timeout, this, [this]() {
        const bool done = QFile::exists(m_capturePath);
        const bool cancelled = QFile::exists(m_capturePath + QStringLiteral(".cancelled"));
        // Capture can be closed without either, so give up after a while.
        const bool lost = m_captureStarted.secsTo(QDateTime::currentDateTime()) > 600;
        if (!done && !cancelled && !lost) {
            return;
        }
        m_capturePoll.stop();
        QFile::remove(m_capturePath + QStringLiteral(".cancelled"));
        if (done) {
            m_shots << m_capturePath;
            Q_EMIT screenshotsChanged();
            if (m_redactSerial > 0) {
                redactAll();
            }
        }
        m_capturing = false;
        Q_EMIT capturingChanged();
        Q_EMIT captureDone();
    });
}

Report *Report::instance()
{
    static Report *report = new Report;
    QQmlEngine::setObjectOwnership(report, QQmlEngine::CppOwnership);
    return report;
}

void Report::handle(const QStringList &arguments)
{
    QString part;
    for (int i = 1; i < arguments.size(); ++i) {
        const QString &a = arguments.at(i);
        if (a.startsWith(QLatin1String("wildos-report:"))) {
            part = a.mid(14).section(QLatin1Char('?'), 0, 0).remove(QLatin1Char('/'));
        } else if (a == QLatin1String("--part") && i + 1 < arguments.size()) {
            part = arguments.at(++i);
        } else if (a == QLatin1String("--crash") && i + 1 < arguments.size()) {
            m_crashTime = QDateTime::fromMSecsSinceEpoch(arguments.at(++i).toLongLong() / 1000);
            Q_EMIT crashChanged();
        } else if (a == QLatin1String("--app") && i + 1 < arguments.size()) {
            m_crashApp = arguments.at(++i);
            Q_EMIT crashChanged();
        } else if (a == QLatin1String("--event") && i + 1 < arguments.size()) {
            const QString about = readEvent(arguments.at(++i));
            if (part.isEmpty()) {
                part = about;
            }
        }
    }
    // A report already under way keeps its answers.
    if (!part.isEmpty() && Parts::known(part) && m_description.trimmed().isEmpty()) {
        setPart(part);
    }
    Q_EMIT raiseRequested();
}

QString Report::readEvent(const QString &path)
{
    // Only from this account's own runtime folder, where Settings leaves it
    // and nobody else can write.
    const QFileInfo info(path);
    const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (runtime.isEmpty() || !info.canonicalFilePath().startsWith(QFileInfo(runtime).canonicalFilePath() + QLatin1Char('/')) || !info.isFile()
        || info.isSymLink() || info.ownerId() != getuid() || info.size() > 256 * 1024) {
        return {};
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    file.remove();
    const QString title = o.value(QStringLiteral("title")).toString().left(200);
    const QString text = o.value(QStringLiteral("text")).toString();
    if (title.isEmpty() || text.isEmpty()) {
        return {};
    }
    m_eventTitle = title;
    m_eventTime = QDateTime::fromSecsSinceEpoch(o.value(QStringLiteral("when")).toInteger());
    section(QStringLiteral("event"))->raw = QStringLiteral("# From Settings > System > History\n\n") + text;
    m_fixedSince = {};
    if (o.value(QStringLiteral("crash")).toBool()) {
        m_crashTime = m_eventTime;
        m_crashApp = o.value(QStringLiteral("app")).toString();
    }
    Q_EMIT crashChanged();
    Q_EMIT whenChanged();
    Q_EMIT itemsChanged();

    // The part of Wild OS the event is about, so the logs gathered are its.
    const QString kind = o.value(QStringLiteral("kind")).toString();
    const QString program = o.value(QStringLiteral("program")).toString();
    if (!program.isEmpty()) {
        return Parts::byProgram(program).id;
    }
    if (kind.startsWith(QLatin1String("update")) || kind == QLatin1String("restore-point") || kind == QLatin1String("programs")) {
        return QStringLiteral("updates");
    }
    if (kind.startsWith(QLatin1String("app")) || kind == QLatin1String("install-failed")) {
        return QStringLiteral("store");
    }
    if (kind.startsWith(QLatin1String("drive"))) {
        return QStringLiteral("files");
    }
    if (kind == QLatin1String("signin") || kind == QLatin1String("signout") || kind == QLatin1String("password")) {
        return QStringLiteral("signin");
    }
    if (kind == QLatin1String("firewall")) {
        return QStringLiteral("network");
    }
    return QStringLiteral("other");
}

QVariantList Report::parts() const
{
    return Parts::choices();
}

void Report::setPart(const QString &part)
{
    if (part == m_part || !Parts::known(part)) {
        return;
    }
    m_fixedSince = {};
    m_part = part;
    Q_EMIT partChanged();
}

QString Report::partName() const
{
    return Parts::byId(m_part).name;
}

void Report::setWhen(const QString &when)
{
    if (when != m_when) {
        m_fixedSince = {};
        m_when = when;
        Q_EMIT whenChanged();
    }
}

void Report::setPickDay(int day)
{
    if (day != m_pickDay) {
        m_fixedSince = {};
        m_pickDay = day;
        Q_EMIT whenChanged();
    }
}

void Report::setPickTime(const QString &time)
{
    if (time != m_pickTime && QTime::fromString(time, QStringLiteral("H:mm")).isValid()) {
        m_fixedSince = {};
        m_pickTime = time;
        Q_EMIT whenChanged();
    }
}

QPair<QDateTime, QDateTime> Report::window() const
{
    if (m_fixedSince.isValid()) {
        return {m_fixedSince, m_fixedUntil};
    }
    const QDateTime now = QDateTime::currentDateTime();
    QDateTime since;
    QDateTime until = now;
    if (m_when == QLatin1String("today")) {
        since = QDateTime(now.date(), QTime(0, 0));
    } else if (m_when == QLatin1String("pick")) {
        const QDateTime at(now.date().addDays(-m_pickDay), QTime::fromString(m_pickTime, QStringLiteral("H:mm")));
        since = at.addSecs(-15 * 60);
        until = qMin(now, at.addSecs(15 * 60));
        if (since >= until) {
            since = until.addSecs(-30 * 60);
        }
    } else {
        // Ten minutes before the crash or now, so what led up to it is in.
        // An event from History is looked at however long ago it was.
        const bool recentCrash = m_crashTime.isValid() && m_crashTime.secsTo(now) < 6 * 3600;
        const bool anchored = recentCrash || m_eventTime.isValid();
        const QDateTime anchor = recentCrash ? m_crashTime : m_eventTime.isValid() ? m_eventTime : now;
        since = anchor.addSecs(-10 * 60);
        since.setTime(QTime(since.time().hour(), since.time().minute()));
        until = anchored ? qMin(now, anchor.addSecs(5 * 60)) : now;
    }
    return {since, until};
}

QString Report::windowText() const
{
    const auto [since, until] = window();
    QString text = QStringLiteral("%1 to %2").arg(since.toString(QStringLiteral("HH:mm")), until.toString(QStringLiteral("HH:mm")));
    if (until.date() != QDate::currentDate()) {
        text += QStringLiteral(" yesterday");
    }
    return text;
}

void Report::setDescription(const QString &text)
{
    if (text != m_description) {
        m_description = text;
        Q_EMIT descriptionChanged();
    }
}

void Report::setRepeat(const QString &repeat)
{
    if (repeat != m_repeat) {
        m_repeat = repeat;
        Q_EMIT repeatChanged();
    }
}

QString Report::repeatText() const
{
    return m_repeat == QLatin1String("always")      ? QStringLiteral("Every time")
        : m_repeat == QLatin1String("sometimes") ? QStringLiteral("Sometimes")
        : m_repeat == QLatin1String("once")      ? QStringLiteral("Only once")
                                                 : QString();
}

QVariantList Report::screenshots() const
{
    QVariantList list;
    for (const QString &path : m_shots) {
        const QFileInfo info(path);
        const QString name = m_shotNames.value(path);
        list.append(QVariantMap{
            {QStringLiteral("url"), QUrl::fromLocalFile(path)},
            {QStringLiteral("label"), name.isEmpty() ? QStringLiteral("Screenshot, %1").arg(info.lastModified().toString(QStringLiteral("HH:mm"))) : name},
            {QStringLiteral("size"), sizeName(info.size())},
        });
    }
    return list;
}

QString Report::crashNote() const
{
    if (!m_eventTitle.isEmpty()) {
        const QString at = m_eventTime.date() == QDate::currentDate() ? m_eventTime.toString(QStringLiteral("HH:mm"))
                                                                      : m_eventTime.toString(QStringLiteral("d MMMM, HH:mm"));
        return QStringLiteral("From History: %1, %2. Its details are added for you.").arg(m_eventTitle, at);
    }
    if (!m_crashTime.isValid()) {
        return {};
    }
    const QString app = m_crashApp.isEmpty() ? partName() : m_crashApp;
    return QStringLiteral("%1 closed unexpectedly at %2. Its details are added for you.").arg(app, m_crashTime.toString(QStringLiteral("HH:mm")));
}

void Report::takeScreenshot()
{
    if (m_capturing || !m_temp.isValid()) {
        return;
    }
    m_capturePath = m_temp.filePath(QStringLiteral("screenshot-%1.png").arg(QDateTime::currentMSecsSinceEpoch()));
    m_capturing = true;
    Q_EMIT capturingChanged();
    Q_EMIT hideForCapture();
    // Capture freezes the screen as it starts, so the window must be gone
    // by then, minimise animation and all.
    QTimer::singleShot(450, this, [this]() {
        m_captureStarted = QDateTime::currentDateTime();
        if (!QProcess::startDetached(QStringLiteral("wildos-capture"), {QStringLiteral("--to"), m_capturePath})) {
            QFile cancelled(m_capturePath + QStringLiteral(".cancelled"));
            if (cancelled.open(QIODevice::WriteOnly)) {
                cancelled.close();
            }
            Q_EMIT message(QStringLiteral("Capture couldn't be started"));
        }
        m_capturePoll.start();
    });
}

void Report::removeScreenshot(int index)
{
    if (index < 0 || index >= m_shots.size()) {
        return;
    }
    const QString path = m_shots.takeAt(index);
    m_shotNames.remove(path);
    QFile::remove(path);
    Q_EMIT screenshotsChanged();
    if (m_redactSerial > 0) {
        redactAll();
    }
}

Report::Section *Report::section(const QString &id)
{
    for (Section &s : m_sections) {
        if (s.id == id) {
            return &s;
        }
    }
    return nullptr;
}

const Report::Section *Report::section(const QString &id) const
{
    for (const Section &s : m_sections) {
        if (s.id == id) {
            return &s;
        }
    }
    return nullptr;
}

// The person's own words and answers, as the report's first file.
QString Report::describe() const
{
    const auto [since, until] = window();
    QStringList lines;
    lines << QStringLiteral("# Problem report") << QString();
    lines << QStringLiteral("Part of Wild OS: %1").arg(partName());
    if (m_crashTime.isValid()) {
        lines << QStringLiteral("Closed unexpectedly at: %1").arg(m_crashTime.toString(QStringLiteral("d MMMM yyyy, HH:mm:ss")));
    }
    if (!m_eventTitle.isEmpty()) {
        lines << QStringLiteral("From History: %1, %2").arg(m_eventTitle, m_eventTime.toString(QStringLiteral("d MMMM yyyy, HH:mm")));
    }
    lines << QStringLiteral("When: %1 (%2 to %3)")
                 .arg(m_when == QLatin1String("today")  ? QStringLiteral("Earlier today")
                          : m_when == QLatin1String("pick") ? QStringLiteral("At a time picked")
                                                            : QStringLiteral("Just now"),
                      since.toString(QStringLiteral("d MMMM yyyy, HH:mm")), until.toString(QStringLiteral("HH:mm")));
    if (!repeatText().isEmpty()) {
        lines << QStringLiteral("Can it be made to happen again: %1").arg(repeatText());
    }
    lines << QString() << QStringLiteral("## What went wrong") << QString() << m_description.trimmed();
    if (!m_shots.isEmpty()) {
        lines << QString() << QStringLiteral("Screenshots: %1, in the screenshots folder.").arg(m_shots.size());
    }
    return lines.join(QLatin1Char('\n'));
}

void Report::prepare()
{
    m_fixedSince = {};
    m_fixedUntil = {};
    const auto [since, until] = window();
    // "Just now" moves on with the clock; the same part and choice of time
    // within two minutes is the same report.
    const QString key = QStringLiteral("%1|%2|%3|%4").arg(m_part, m_when, m_pickTime).arg(m_pickDay) + QStringLiteral("|")
        + QString::number(since.toSecsSinceEpoch() / 120);
    if (key == m_collectedKey && !m_collecting) {
        m_fixedSince = m_raw.since;
        m_fixedUntil = m_raw.until;
        redactAll();
        return;
    }
    m_collectedKey = key;
    m_fixedSince = since;
    m_fixedUntil = until;
    Q_EMIT whenChanged();
    // The preview's title names the window too.
    Q_EMIT viewingChanged();
    m_collecting = true;
    Q_EMIT busyChanged();
    m_collector.start(since, until, Parts::byId(m_part), true);
}

void Report::collected(const RawReport &raw)
{
    m_raw = raw;
    m_collecting = false;
    // The serial numbers only root can read come with the logs.
    m_known = SysInfo::known(raw.serials);
    if (raw.crashTime.isValid() && !m_crashTime.isValid()) {
        m_crashTime = raw.crashTime;
        Q_EMIT crashChanged();
    }
    section(QStringLiteral("about"))->raw = raw.about;
    section(QStringLiteral("crash"))->raw = raw.crash;
    section(QStringLiteral("logs"))->raw = raw.logs.join(QLatin1Char('\n'));
    section(QStringLiteral("updates"))->raw = raw.updates;
    section(QStringLiteral("fulllog"))->raw = raw.boot.join(QLatin1Char('\n'));
    Q_EMIT busyChanged();
    Q_EMIT itemsChanged();
    redactAll();
}

void Report::redactAll()
{
    section(QStringLiteral("description"))->raw = describe();
    QStringList texts;
    for (const Section &s : std::as_const(m_sections)) {
        texts << s.raw;
    }
    const int serial = ++m_redactSerial;
    m_redacting = true;
    Q_EMIT busyChanged();
    auto *watcher = new QFutureWatcher<Redacted>(this);
    connect(watcher, &QFutureWatcher<Redacted>::finished, this, [this, watcher, serial]() {
        watcher->deleteLater();
        if (serial != m_redactSerial) {
            return;
        }
        const Redacted result = watcher->result();
        for (int i = 0; i < m_sections.size() && i < result.texts.size(); ++i) {
            m_sections[i].redacted = result.texts.at(i);
            for (int c = 0; c < Redactor::CategoryCount; ++c) {
                m_sections[i].counts[c] = result.counts.at(i).at(c);
            }
        }
        m_redacting = false;
        Q_EMIT busyChanged();
        Q_EMIT redactedChanged();
        Q_EMIT itemsChanged();
        showPreview();
        measure();
    });
    watcher->setFuture(QtConcurrent::run(redactTexts, m_known, m_custom, texts));
}

QVariantList Report::items() const
{
    QVariantList list;
    const auto add = [&list, this](const QString &id, const QString &title, const QString &subtitle, const QString &icon) {
        const Section *s = section(id);
        list.append(QVariantMap{{QStringLiteral("id"), id},
                                {QStringLiteral("title"), title},
                                {QStringLiteral("subtitle"), subtitle},
                                {QStringLiteral("icon"), icon},
                                {QStringLiteral("included"), s && s->included}});
    };
    const bool waiting = m_collecting;
    qint64 shotBytes = 0;
    for (const QString &path : m_shots) {
        shotBytes += QFileInfo(path).size();
    }
    const QString shots = m_shots.isEmpty() ? QStringLiteral("No screenshot")
        : m_shots.size() == 1                ? QStringLiteral("1 screenshot, %1").arg(sizeName(shotBytes))
                                             : QStringLiteral("%1 screenshots, %2").arg(m_shots.size()).arg(sizeName(shotBytes));
    add(QStringLiteral("description"), m_shots.size() > 1 ? QStringLiteral("Your description and screenshots")
                                       : m_shots.size() == 1 ? QStringLiteral("Your description and screenshot")
                                                             : QStringLiteral("Your description"),
        shots, QStringLiteral("file-text"));
    if (!m_eventTitle.isEmpty()) {
        add(QStringLiteral("event"), QStringLiteral("This event from History"), m_eventTitle, QStringLiteral("history"));
    }
    const QString v = SysInfo::version();
    add(QStringLiteral("about"), QStringLiteral("About this PC"),
        v.isEmpty() ? QStringLiteral("Wild OS version, the PC's model, graphics and memory")
                    : QStringLiteral("Wild OS %1, the PC's model, graphics and memory").arg(v),
        QStringLiteral("cpu"));
    if (!waiting && !m_raw.crash.isEmpty()) {
        const QString app = m_crashApp.isEmpty() ? partName() : m_crashApp;
        const QString at = m_raw.crashTime.isValid() ? m_raw.crashTime.toString(QStringLiteral("HH:mm")) : QString();
        add(QStringLiteral("crash"), QStringLiteral("Crash details for %1").arg(app),
            (m_raw.crashTrace ? QStringLiteral("Where it stopped, %1") : QStringLiteral("How it stopped, %1")).arg(at), QStringLiteral("zap"));
    }
    const Part &part = Parts::byId(m_part);
    QString logs;
    if (waiting) {
        logs = QStringLiteral("Gathering");
    } else {
        const int count = int(m_raw.logs.size());
        const QString lines = count == 1 ? QStringLiteral("1 line") : QStringLiteral("%1 lines").arg(count);
        if (!m_raw.systemLogs) {
            logs = part.programs.isEmpty() ? QStringLiteral("Your own apps only, %1").arg(lines) : QStringLiteral("%1 only, %2").arg(part.name, lines);
        } else {
            logs = part.programs.isEmpty() ? QStringLiteral("The system, %1").arg(lines) : QStringLiteral("%1 and the system, %2").arg(part.name, lines);
        }
    }
    add(QStringLiteral("logs"), QStringLiteral("Logs from %1").arg(windowText()), logs, QStringLiteral("logs"));
    add(QStringLiteral("updates"), QStringLiteral("Recent updates"),
        waiting || m_raw.updateDays > 0 ? QStringLiteral("What changed in the last 7 days") : QStringLiteral("Nothing changed in the last 7 days"),
        QStringLiteral("history"));
    const Section *full = section(QStringLiteral("fulllog"));
    add(QStringLiteral("fulllog"), QStringLiteral("Full system log"),
        waiting ? QStringLiteral("For problems that are hard to pin down")
                : QStringLiteral("For problems that are hard to pin down · %1").arg(sizeName(full->raw.toUtf8().size())),
        QStringLiteral("database"));
    return list;
}

void Report::setIncluded(const QString &id, bool on)
{
    Section *s = section(id);
    if (!s || s->included == on) {
        return;
    }
    s->included = on;
    Q_EMIT itemsChanged();
    Q_EMIT redactedChanged();
    measure();
}

void Report::view(const QString &id)
{
    if (id != m_viewing && section(id)) {
        m_viewing = id;
        Q_EMIT viewingChanged();
        showPreview();
    }
}

QString Report::previewTitle() const
{
    if (m_viewing == QLatin1String("description")) {
        return QStringLiteral("Preview: your description");
    }
    if (m_viewing == QLatin1String("about")) {
        return QStringLiteral("Preview: about this PC");
    }
    if (m_viewing == QLatin1String("crash")) {
        return QStringLiteral("Preview: crash details");
    }
    if (m_viewing == QLatin1String("event")) {
        return QStringLiteral("Preview: the event from History");
    }
    if (m_viewing == QLatin1String("updates")) {
        return QStringLiteral("Preview: recent updates");
    }
    if (m_viewing == QLatin1String("fulllog")) {
        return QStringLiteral("Preview: full system log");
    }
    return QStringLiteral("Preview: Logs from %1").arg(windowText());
}

void Report::showPreview()
{
    const Section *s = section(m_viewing);
    if (!s) {
        return;
    }
    const bool log = m_viewing == QLatin1String("logs") || m_viewing == QLatin1String("fulllog");
    QStringList lines = s->redacted.isEmpty() ? QStringList() : s->redacted.split(QLatin1Char('\n'));
    if (m_viewing == QLatin1String("description")) {
        for (int i = 0; i < m_shots.size(); ++i) {
            lines << QStringLiteral("screenshots/screenshot-%1.png, %2").arg(i + 1).arg(sizeName(QFileInfo(m_shots.at(i)).size()));
        }
    }
    if (lines.isEmpty()) {
        lines << (log ? QStringLiteral("Nothing was logged in this time.") : QStringLiteral("Nothing to show."));
    }
    m_preview.setLines(lines, log);
}

int Report::removed() const
{
    int total = 0;
    for (const Section &s : m_sections) {
        if (s.included) {
            for (int c = 0; c < Redactor::CategoryCount; ++c) {
                total += s.counts[c];
            }
        }
    }
    return total;
}

QVariantList Report::removedKinds() const
{
    QVariantList list;
    for (int c = 0; c < Redactor::CategoryCount; ++c) {
        int count = 0;
        for (const Section &s : m_sections) {
            if (s.included) {
                count += s.counts[c];
            }
        }
        if (count > 0) {
            list.append(QVariantMap{{QStringLiteral("label"), Redactor::categoryLabel(Redactor::Category(c))}, {QStringLiteral("count"), count}});
        }
    }
    return list;
}

void Report::addWord(const QString &word)
{
    const QString w = word.trimmed();
    if (w.size() < 2 || m_custom.contains(w, Qt::CaseInsensitive)) {
        return;
    }
    m_custom << w;
    Q_EMIT redactedChanged();
    redactAll();
}

void Report::removeWord(const QString &word)
{
    if (m_custom.removeAll(word) > 0) {
        Q_EMIT redactedChanged();
        redactAll();
    }
}

// The zip, built for real so the size shown is the size saved.
void Report::measure()
{
    const int serial = ++m_sizeSerial;
    auto *watcher = new QFutureWatcher<qint64>(this);
    connect(watcher, &QFutureWatcher<qint64>::finished, this, [this, watcher, serial]() {
        watcher->deleteLater();
        if (serial == m_sizeSerial) {
            m_size = watcher->result();
            Q_EMIT sizeChanged();
        }
    });
    const QList<Section> sections = m_sections;
    const QStringList shots = m_shots;
    watcher->setFuture(QtConcurrent::run([sections, shots]() { return qint64(zip(sections, shots, false).size()); }));
}

QString Report::sizeText() const
{
    return m_size < 0 || busy() ? QString() : QStringLiteral("Report size %1").arg(sizeName(m_size));
}

QByteArray Report::zip(const QList<Section> &sections, const QStringList &shots, bool all)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    KZip archive(&buffer);
    if (!archive.open(QIODevice::WriteOnly)) {
        return {};
    }
    const QDateTime now = QDateTime::currentDateTime();
    for (const Section &s : sections) {
        if ((!s.included && !all) || s.redacted.isEmpty()) {
            continue;
        }
        QString text = s.redacted;
        if (s.id == QLatin1String("logs") || s.id == QLatin1String("fulllog")) {
            text = fileLines(text);
        }
        if (s.id == QLatin1String("description")) {
            int total = 0;
            for (const Section &other : sections) {
                if (other.included || all) {
                    for (int c = 0; c < Redactor::CategoryCount; ++c) {
                        total += other.counts[c];
                    }
                }
            }
            QStringList files;
            for (const Section &other : sections) {
                if ((other.included || all) && !other.redacted.isEmpty() && other.id != QLatin1String("description")) {
                    files << QStringLiteral("- ") + other.file;
                }
            }
            text += QStringLiteral("\n\n## In this report\n\n%1\n\nWritten with Report a problem in Wild OS. Personal details were replaced "
                                   "with placeholders such as [user] and [wifi-1] before anyone saw the report: %2 in all.\n")
                        .arg(files.join(QLatin1Char('\n')))
                        .arg(total);
        } else if (!text.endsWith(QLatin1Char('\n'))) {
            text += QLatin1Char('\n');
        }
        archive.writeFile(s.file, text.toUtf8(), 0100644, QString(), QString(), now, now, now);
    }
    const bool description = std::any_of(sections.begin(), sections.end(), [](const Section &s) {
        return s.id == QLatin1String("description") && s.included;
    });
    if (all || description) {
        for (int i = 0; i < shots.size(); ++i) {
            QFile shot(shots.at(i));
            if (shot.open(QIODevice::ReadOnly)) {
                archive.writeFile(QStringLiteral("screenshots/screenshot-%1.png").arg(i + 1), shot.readAll(), 0100644, QString(), QString(), now, now, now);
            }
        }
    }
    archive.close();
    return bytes;
}

QString Report::suggestedName() const
{
    return QStringLiteral("wildos-report-%1.zip").arg(QTime::currentTime().toString(QStringLiteral("HHmm")));
}

// The portal's own call rather than Qt's file dialog: Qt only passes the
// folder to start in when saving, and Open should start in Screenshots.
void Report::chooseScreenshotFiles(QWindow *window)
{
    if (!m_pickerRequest.isEmpty()) {
        return;
    }
    m_pickerRequest = QStringLiteral("pending");
    const auto ask = [this](const QString &parent) {
        static const bool registered = [] {
            qDBusRegisterMetaType<PickerRule>();
            qDBusRegisterMetaType<QList<PickerRule>>();
            qDBusRegisterMetaType<PickerFilter>();
            qDBusRegisterMetaType<QList<PickerFilter>>();
            return true;
        }();
        Q_UNUSED(registered)
        static int serial = 0;
        const QString token = QStringLiteral("wildos_report_%1").arg(++serial);
        QString sender = QDBusConnection::sessionBus().baseService().mid(1);
        sender.replace(QLatin1Char('.'), QLatin1Char('_'));
        m_pickerRequest = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
        QDBusConnection::sessionBus().connect(QStringLiteral("org.freedesktop.portal.Desktop"), m_pickerRequest,
                                              QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"), this,
                                              SLOT(pickerResponse(uint, QVariantMap)));

        QList<PickerFilter> filters{{QStringLiteral("Pictures"), {}}};
        for (const char *glob : {"*.png", "*.jpg", "*.jpeg", "*.webp", "*.gif", "*.bmp"}) {
            filters.first().rules.append({0, QString::fromLatin1(glob)});
        }
        QByteArray folder = QFile::encodeName(screenshotsFolder().toLocalFile());
        folder.append('\0');
        const QVariantMap options{
            {QStringLiteral("handle_token"), token},
            {QStringLiteral("modal"), true},
            {QStringLiteral("multiple"), true},
            {QStringLiteral("filters"), QVariant::fromValue(filters)},
            {QStringLiteral("current_filter"), QVariant::fromValue(filters.first())},
            {QStringLiteral("current_folder"), folder},
        };
        QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.portal.Desktop"), QStringLiteral("/org/freedesktop/portal/desktop"),
                                                           QStringLiteral("org.freedesktop.portal.FileChooser"), QStringLiteral("OpenFile"));
        call << parent << QStringLiteral("Choose a picture") << options;
        QDBusConnection::sessionBus().callWithCallback(call, this, SLOT(pickerOpened()), SLOT(pickerFailed()));
    };
    if (!window || !KWindowSystem::isPlatformWayland()) {
        ask(QString());
        return;
    }
    // The dialog belongs to this window (in front of it, no taskbar button
    // of its own) through the handle the compositor gives it.
    auto done = std::make_shared<bool>(false);
    auto exported = std::make_shared<QMetaObject::Connection>();
    *exported = connect(KWaylandExtras::self(), &KWaylandExtras::windowExported, this, [ask, window, done, exported](QWindow *w, const QString &handle) {
        if (w != window || *done) {
            return;
        }
        *done = true;
        QObject::disconnect(*exported);
        ask(QStringLiteral("wayland:") + handle);
    });
    QTimer::singleShot(1000, this, [ask, done, exported]() {
        if (!*done) {
            *done = true;
            QObject::disconnect(*exported);
            ask(QString());
        }
    });
    KWaylandExtras::exportWindow(window);
}

void Report::pickerResponse(uint response, const QVariantMap &results)
{
    QDBusConnection::sessionBus().disconnect(QStringLiteral("org.freedesktop.portal.Desktop"), m_pickerRequest,
                                             QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"), this,
                                             SLOT(pickerResponse(uint, QVariantMap)));
    m_pickerRequest.clear();
    if (response != 0) {
        return;
    }
    QList<QUrl> files;
    const QStringList uris = results.value(QStringLiteral("uris")).toStringList();
    for (const QString &uri : uris) {
        files << QUrl(uri);
    }
    addScreenshotFiles(files);
}

void Report::pickerOpened()
{
}

void Report::pickerFailed()
{
    m_pickerRequest.clear();
    Q_EMIT message(QStringLiteral("The Open dialog couldn't be shown"));
}

void Report::addScreenshotFiles(const QList<QUrl> &files)
{
    if (!m_temp.isValid()) {
        return;
    }
    int added = 0;
    QStringList unreadable;
    for (const QUrl &file : files) {
        const QString source = file.toLocalFile();
        QImageReader reader(source);
        reader.setAutoTransform(true);
        QImage image = reader.read();
        if (image.isNull()) {
            unreadable << QFileInfo(source).fileName();
            continue;
        }
        if (image.colorSpace().isValid() && image.colorSpace() != QColorSpace(QColorSpace::SRgb)) {
            image.convertToColorSpace(QColorSpace(QColorSpace::SRgb));
        }
        // Photos from a phone run to 50 megapixels; a screen's worth is
        // plenty to show a problem and keeps the report small.
        if (image.width() > 3840 || image.height() > 3840) {
            image = image.scaled(3840, 3840, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        // Drawn onto a blank picture and saved as a new PNG, the way a
        // screenshot from Capture is added: the copy carries no location,
        // camera or author details and no file name, only the pixels.
        QImage clean(image.size(), image.hasAlphaChannel() ? QImage::Format_ARGB32 : QImage::Format_RGB32);
        clean.fill(Qt::transparent);
        {
            QPainter painter(&clean);
            painter.drawImage(0, 0, image);
        }
        const QString path = m_temp.filePath(QStringLiteral("picture-%1-%2.png").arg(QDateTime::currentMSecsSinceEpoch()).arg(added));
        if (!clean.save(path, "PNG")) {
            unreadable << QFileInfo(source).fileName();
            continue;
        }
        m_shots << path;
        m_shotNames.insert(path, QFileInfo(source).fileName());
        ++added;
    }
    if (added > 0) {
        Q_EMIT screenshotsChanged();
        if (m_redactSerial > 0) {
            redactAll();
        }
    }
    if (unreadable.size() == 1) {
        Q_EMIT message(QStringLiteral("%1 couldn't be opened as a picture").arg(unreadable.first()));
    } else if (unreadable.size() > 1) {
        Q_EMIT message(QStringLiteral("%1 files couldn't be opened as pictures").arg(unreadable.size()));
    }
}

QUrl Report::screenshotsFolder() const
{
    const QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    const QString screenshots = pictures + QStringLiteral("/Screenshots");
    return QUrl::fromLocalFile(QFileInfo(screenshots).isDir() ? screenshots : pictures);
}

QUrl Report::downloadsFolder() const
{
    return QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
}

QString Report::uniquePath(const QString &folder) const
{
    const QString base = suggestedName().chopped(4);
    QString path = QDir(folder).filePath(base + QStringLiteral(".zip"));
    for (int n = 2; QFileInfo::exists(path); ++n) {
        path = QDir(folder).filePath(QStringLiteral("%1-%2.zip").arg(base).arg(n));
    }
    return path;
}

QString Report::writeZip(const QString &path, bool all)
{
    const QByteArray bytes = zip(m_sections, m_shots, all);
    QFile file(path);
    if (bytes.isEmpty() || !file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(bytes) != bytes.size()) {
        return {};
    }
    file.close();
    m_savedPath = path;
    m_savedText = QStringLiteral("Report saved as %1, %2").arg(QFileInfo(path).fileName(), sizeName(bytes.size()));
    Q_EMIT savedChanged();
    return path;
}

void Report::setTitle(const QString &title)
{
    if (title != m_title) {
        m_title = title;
        m_titleTouched = true;
        Q_EMIT titleChanged();
    }
}

// The first sentence of the description, cut at a word, as a start.
void Report::suggestTitle()
{
    if (m_titleTouched && !m_title.trimmed().isEmpty()) {
        return;
    }
    const Section *s = section(QStringLiteral("description"));
    QString text = m_description.simplified();
    if (s && !s->redacted.isEmpty()) {
        const qsizetype at = s->redacted.indexOf(QStringLiteral("## What went wrong\n\n"));
        if (at >= 0) {
            text = s->redacted.mid(at + 20).section(QStringLiteral("\n\nScreenshots:"), 0, 0).simplified();
        }
    }
    const auto sentence = QRegularExpression(QStringLiteral("^(.+?[.!?])(\\s|$)")).match(text);
    if (sentence.hasMatch()) {
        text = sentence.captured(1);
    }
    if (text.endsWith(QLatin1Char('.'))) {
        text.chop(1);
    }
    if (text.size() > 80) {
        text = text.left(80);
        text = text.left(text.lastIndexOf(QLatin1Char(' '))) + QStringLiteral("...");
    }
    m_title = text;
    Q_EMIT titleChanged();
}

QString Report::issueBody(const QString &zipName, int budget) const
{
    const Section *s = section(QStringLiteral("description"));
    QString words = m_description.trimmed();
    if (s && !s->redacted.isEmpty()) {
        const qsizetype at = s->redacted.indexOf(QStringLiteral("## What went wrong\n\n"));
        if (at >= 0) {
            words = s->redacted.mid(at + 20).section(QStringLiteral("\n\nScreenshots:"), 0, 0).trimmed();
        }
    }
    if (budget >= 0 && words.size() > budget) {
        words = words.left(qMax(0, budget)).trimmed() + QStringLiteral("...\n\n(The rest is in the report.)");
    }
    const auto [since, until] = window();
    QStringList lines{words, QString(), QStringLiteral("| | |"), QStringLiteral("|---|---|")};
    lines << QStringLiteral("| Part of Wild OS | %1 |").arg(partName());
    lines << QStringLiteral("| When | %1 |").arg(m_crashTime.isValid() ? m_crashTime.toString(QStringLiteral("d MMMM yyyy, HH:mm"))
                                                  : m_eventTime.isValid() ? m_eventTime.toString(QStringLiteral("d MMMM yyyy, HH:mm"))
                                                                        : QStringLiteral("%1 to %2").arg(since.toString(QStringLiteral("d MMMM yyyy, HH:mm")),
                                                                                                         until.toString(QStringLiteral("HH:mm"))));
    if (!repeatText().isEmpty()) {
        lines << QStringLiteral("| Happens | %1 |").arg(repeatText());
    }
    const QString v = SysInfo::version();
    lines << QStringLiteral("| Wild OS | %1, %2 |").arg(v.isEmpty() ? QStringLiteral("unknown") : v, SysInfo::channel());
    const Section *about = section(QStringLiteral("about"));
    if (about && about->included && !SysInfo::model().isEmpty()) {
        lines << QStringLiteral("| PC | %1 |").arg(SysInfo::model());
    }
    if (!zipName.isEmpty()) {
        lines << QString() << QStringLiteral("**Logs and details:** drag `%1` from your Downloads folder into this box to attach it.").arg(zipName);
    }
    return lines.join(QLatin1Char('\n'));
}

void Report::setHandoff(const QString &state)
{
    m_handoff = state;
    Q_EMIT handoffChanged();
}

void Report::openGitHub()
{
    if (m_handoff == QLatin1String("opening")) {
        return;
    }
    endHandoff();
    m_postedNumber = 0;
    m_postedUrl.clear();
    setHandoff(QStringLiteral("opening"));
    const int serial = m_handoffSerial;
    // Writing the zip holds up the window for a moment, so the dialog that
    // says what is happening gets drawn first.
    QTimer::singleShot(150, this, [this, serial]() {
        if (serial != m_handoffSerial) {
            return;
        }
        const QString folder = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        QDir().mkpath(folder);
        const QString path = writeZip(uniquePath(folder), false);
        if (path.isEmpty()) {
            setHandoff({});
            Q_EMIT message(QStringLiteral("The report couldn't be saved to Downloads"));
            return;
        }
        const QString title = m_title.trimmed().isEmpty() ? QStringLiteral("Problem with %1").arg(partName()) : m_title.trimmed();
        const auto encode = [](const QString &value) { return QString::fromLatin1(QUrl::toPercentEncoding(value)); };
        const QString zipName = QFileInfo(path).fileName();
        const auto address = [&](int budget) {
            return issuesUrl + QStringLiteral("/new?title=") + encode(title) + QStringLiteral("&body=") + encode(issueBody(zipName, budget));
        };
        // A long description is cut to fit, and the whole of it is in the zip.
        QString url = address(-1);
        for (int budget = int(m_description.size()); url.size() > urlBudget && budget > 0; budget = budget * 4 / 5) {
            url = address(budget);
        }
        if (url.size() > urlBudget) {
            url = address(0);
        }
        m_handoffUrl = url;
        m_handoffZip = zipName;
        m_handoffAt = QDateTime::currentDateTimeUtc();
        if (!QDesktopServices::openUrl(QUrl::fromEncoded(url.toLatin1()))) {
            setHandoff({});
            Q_EMIT message(QStringLiteral("The browser couldn't be opened. The report is in Downloads."));
            return;
        }
        // The browser takes a moment to appear; saying "finish in your
        // browser" before it has would be ahead of what is on screen.
        QTimer::singleShot(2500, this, [this, serial]() {
            if (serial != m_handoffSerial) {
                return;
            }
            m_watching = true;
            setHandoff(QStringLiteral("waiting"));
            watchNext(20);
        });
    });
}

void Report::reopenGitHub()
{
    if (!m_handoffUrl.isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromEncoded(m_handoffUrl.toLatin1()));
    }
}

void Report::endHandoff()
{
    ++m_handoffSerial;
    m_watchTimer.stop();
    m_watching = false;
    m_watchTag.clear();
    if (m_watchReply) {
        m_watchReply->abort();
    }
    if (!m_handoff.isEmpty()) {
        setHandoff({});
    }
}

void Report::viewPosted()
{
    if (!m_postedUrl.isEmpty()) {
        QDesktopServices::openUrl(QUrl(m_postedUrl));
    }
}

// Without signing in, GitHub answers 60 requests an hour from one address,
// but an answer that nothing has changed since last time (304) is free. So
// even if new issues kept arriving, every 20 seconds for ten minutes and
// then every minute until half an hour stays under the limit.
void Report::watchNext(int seconds)
{
    if (m_handoffAt.secsTo(QDateTime::currentDateTimeUtc()) + seconds > 30 * 60) {
        m_watching = false;
        Q_EMIT handoffChanged();
        return;
    }
    m_watchTimer.start(seconds * 1000);
}

void Report::watchOnce()
{
    if (!m_net) {
        m_net = new QNetworkAccessManager(this);
    }
    QNetworkRequest request(QUrl(QStringLiteral("https://api.github.com/repos/wildorigins/wildos/issues?state=all&sort=created&direction=desc&per_page=30")));
    request.setHeader(QNetworkRequest::UserAgentHeader, QByteArrayLiteral("Wild OS Report a problem"));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    if (!m_watchTag.isEmpty()) {
        request.setRawHeader("If-None-Match", m_watchTag);
    }
    request.setTransferTimeout(20000);
    const int serial = m_handoffSerial;
    QNetworkReply *reply = m_net->get(request);
    m_watchReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, serial, reply]() {
        reply->deleteLater();
        if (serial != m_handoffSerial) {
            return;
        }
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 200) {
            m_watchTag = reply->rawHeader("ETag");
            const IssueWatch::Posted posted = IssueWatch::find(reply->readAll(), m_handoffZip, m_handoffAt);
            if (posted.number > 0) {
                m_postedNumber = posted.number;
                m_postedUrl = posted.url;
                m_watching = false;
                setHandoff(QStringLiteral("posted"));
                return;
            }
        }
        const bool limited = status == 403 || status == 429;
        watchNext(limited ? 120 : m_handoffAt.secsTo(QDateTime::currentDateTimeUtc()) < 600 ? 20 : 60);
    });
}

bool Report::saveTo(const QUrl &file)
{
    QString path = file.toLocalFile();
    if (path.isEmpty()) {
        return false;
    }
    if (!path.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".zip");
    }
    if (writeZip(path, false).isEmpty()) {
        Q_EMIT message(QStringLiteral("The report couldn't be saved there"));
        return false;
    }
    return true;
}

void Report::copyDescription()
{
    QString text = issueBody(QString(), -1);
    QGuiApplication::clipboard()->setText(text);
    Q_EMIT message(QStringLiteral("Description copied"));
}

void Report::copyPreview()
{
    const Section *s = section(m_viewing);
    if (s) {
        const bool log = m_viewing == QLatin1String("logs") || m_viewing == QLatin1String("fulllog");
        QGuiApplication::clipboard()->setText(log ? fileLines(s->redacted) : s->redacted);
        Q_EMIT message(QStringLiteral("Copied"));
    }
}

void Report::searchIssues()
{
    const QString words = (m_title.trimmed().isEmpty() ? partName() : m_title).simplified();
    QDesktopServices::openUrl(QUrl(issuesUrl + QStringLiteral("?q=") + QString::fromLatin1(QUrl::toPercentEncoding(QStringLiteral("is:issue ") + words))));
}

void Report::showSaved()
{
    if (m_savedPath.isEmpty()) {
        return;
    }
    // The file manager's own call, which opens the folder with the file
    // selected.
    QProcess::startDetached(QStringLiteral("dbus-send"),
                            {QStringLiteral("--session"), QStringLiteral("--type=method_call"), QStringLiteral("--dest=org.freedesktop.FileManager1"),
                             QStringLiteral("/org/freedesktop/FileManager1"), QStringLiteral("org.freedesktop.FileManager1.ShowItems"),
                             QStringLiteral("array:string:") + QUrl::fromLocalFile(m_savedPath).toString(), QStringLiteral("string:")});
}

QString Report::saveHeadless(const QString &dir, const QString &part, const QDateTime &since, const QDateTime &until, int boot, const QString &description)
{
    if (Parts::known(part)) {
        m_part = part;
    }
    m_description = description.isEmpty() ? QStringLiteral("Saved from start-up recovery.") : description;
    m_fixedSince = since;
    m_fixedUntil = until;
    QEventLoop loop;
    RawReport raw;
    connect(&m_collector, &Collector::finished, &loop, [&](const RawReport &r) {
        raw = r;
        loop.quit();
    });
    disconnect(&m_collector, &Collector::finished, this, &Report::collected);
    m_collector.start(since, until, Parts::byId(m_part), false, boot);
    loop.exec();

    m_known = SysInfo::known(raw.serials);
    m_raw = raw;
    section(QStringLiteral("description"))->raw = describe().replace(QStringLiteral("When: Just now"), QStringLiteral("When: Saved from start-up recovery"));
    section(QStringLiteral("about"))->raw = raw.about;
    section(QStringLiteral("crash"))->raw = raw.crash;
    section(QStringLiteral("logs"))->raw = raw.logs.join(QLatin1Char('\n'));
    section(QStringLiteral("updates"))->raw = raw.updates;
    section(QStringLiteral("fulllog"))->raw = raw.boot.join(QLatin1Char('\n'));
    QStringList texts;
    for (const Section &s : std::as_const(m_sections)) {
        texts << s.raw;
    }
    const Redacted result = redactTexts(m_known, m_custom, texts);
    for (int i = 0; i < m_sections.size(); ++i) {
        m_sections[i].redacted = result.texts.at(i);
        for (int c = 0; c < Redactor::CategoryCount; ++c) {
            m_sections[i].counts[c] = result.counts.at(i).at(c);
        }
    }
    QDir().mkpath(dir);
    return writeZip(uniquePath(dir), true);
}
