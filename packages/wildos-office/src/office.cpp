#include "office.h"

#include "clockformat.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <algorithm>

#include <unistd.h>

Office::App Office::s_app = Office::Write;

namespace
{
const QStringList writeTypes = {
    QStringLiteral("odt"), QStringLiteral("ott"), QStringLiteral("fodt"), QStringLiteral("docx"), QStringLiteral("doc"),
    QStringLiteral("docm"), QStringLiteral("dotx"), QStringLiteral("dot"), QStringLiteral("rtf"), QStringLiteral("txt"),
    QStringLiteral("wpd"), QStringLiteral("pages"), QStringLiteral("abw"), QStringLiteral("wps"),
};
const QStringList tablesTypes = {
    QStringLiteral("ods"), QStringLiteral("ots"), QStringLiteral("fods"), QStringLiteral("xlsx"), QStringLiteral("xls"),
    QStringLiteral("xlsm"), QStringLiteral("xltx"), QStringLiteral("xlt"), QStringLiteral("csv"), QStringLiteral("tsv"),
    QStringLiteral("numbers"), QStringLiteral("gnumeric"),
};
const QStringList slidesTypes = {
    QStringLiteral("odp"), QStringLiteral("otp"), QStringLiteral("fodp"), QStringLiteral("pptx"), QStringLiteral("ppt"),
    QStringLiteral("pptm"), QStringLiteral("ppsx"), QStringLiteral("pps"), QStringLiteral("potx"), QStringLiteral("pot"),
    QStringLiteral("key"),
};

// The one of three that belongs to the app this is.
template<typename T>
T pick(const T &write, const T &tables, const T &slides)
{
    switch (Office::app()) {
    case Office::Tables:
        return tables;
    case Office::Slides:
        return slides;
    default:
        return write;
    }
}

const QString pickListPath = QStringLiteral("/org.openoffice.Office.Histories/Histories/org.openoffice.Office.Histories:HistoryInfo['PickList']");

QString registryFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/libreoffice/4/user/registrymodifications.xcu");
}

QString toPath(const QString &pathOrUrl)
{
    if (pathOrUrl.startsWith(QLatin1String("file:"))) {
        return QUrl(pathOrUrl).toLocalFile();
    }
    return pathOrUrl;
}

// LibreOffice keeps its settings in memory and writes them all back when it
// quits, so its pick list is only edited while it is not running.
bool libreOfficeRunning()
{
    const uint uid = getuid();
    QDirIterator it(QStringLiteral("/proc"), QDir::Dirs | QDir::NoDotAndDotDot);
    while (it.hasNext()) {
        const QString dir = it.next();
        QFile comm(dir + QStringLiteral("/comm"));
        if (!comm.open(QIODevice::ReadOnly) || comm.readAll().trimmed() != "soffice.bin") {
            continue;
        }
        if (QFileInfo(dir).ownerId() == uid) {
            return true;
        }
    }
    return false;
}
}

void Office::setApp(App app)
{
    s_app = app;
}

Office::App Office::app()
{
    return s_app;
}

QString Office::moduleSwitch()
{
    return pick(QStringLiteral("--writer"), QStringLiteral("--calc"), QStringLiteral("--impress"));
}

bool Office::running()
{
    return libreOfficeRunning();
}

// Settings > Typing keeps its spelling language as Sonnet's default, where
// KDE apps read it. LibreOffice takes the language of new documents from
// the display language instead, so without this Write kept checking British
// English after someone chose American (#21). A language picked in
// LibreOffice's own options since Wild OS last set one is left alone.
void Office::followSpellingLanguage()
{
    const QSettings sonnet(QStringLiteral("KDE"), QStringLiteral("Sonnet"));
    static const QRegularExpression code(QStringLiteral("^([a-z]{2,3})(?:_([A-Z]{2}))?"));
    const QRegularExpressionMatch match = code.match(sonnet.value(QStringLiteral("defaultLanguage")).toString());
    if (!match.hasMatch() || libreOfficeRunning()) {
        return;
    }
    // Sonnet names dictionaries like en_GB or de_DE_frami; LibreOffice wants en-GB.
    const QString chosen = match.captured(2).isEmpty() ? match.captured(1) : match.captured(1) + QLatin1Char('-') + match.captured(2);

    const QString stateDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/wildos-office");
    const QString stateFile = stateDir + QStringLiteral("/spelling-language");
    QString lastSet;
    QFile state(stateFile);
    if (state.open(QIODevice::ReadOnly)) {
        lastSet = QString::fromUtf8(state.readAll()).trimmed();
        state.close();
    }
    auto remember = [&] {
        QDir().mkpath(stateDir);
        QSaveFile out(stateFile);
        if (out.open(QIODevice::WriteOnly)) {
            out.write(chosen.toUtf8());
            out.commit();
        }
    };

    // A new profile has no registry yet; LibreOffice makes one on its first
    // start, and the next start from Write or Tables sets the language.
    QFile file(registryFile());
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    file.close();

    const QString path = QStringLiteral("oor:path=\"/org.openoffice.Office.Linguistic/General\"");
    const QString prop = QStringLiteral("oor:name=\"DefaultLocale\"");
    const QString line = QStringLiteral("<item %1><prop %2 oor:op=\"fuse\"><value>%3</value></prop></item>").arg(path, prop, chosen);
    static const QRegularExpression valueText(QStringLiteral("<value>([^<]*)</value>"));
    int at = -1;
    QString current;
    for (int i = 0; i < lines.size(); ++i) {
        if (lines.at(i).contains(path) && lines.at(i).contains(prop)) {
            at = i;
            current = valueText.match(lines.at(i)).captured(1).trimmed();
            break;
        }
    }
    if (current == chosen) {
        remember();
        return;
    }
    if (!current.isEmpty() && current != lastSet) {
        return;
    }
    if (at >= 0) {
        lines[at] = line;
    } else {
        const int end = int(lines.indexOf(QStringLiteral("</oor:items>")));
        if (end < 0) {
            return;
        }
        lines.insert(end, line);
    }
    QSaveFile save(registryFile());
    if (save.open(QIODevice::WriteOnly)) {
        save.write(lines.join(QLatin1Char('\n')).toUtf8());
        if (save.commit()) {
            remember();
        }
    }
}

QString Office::soffice()
{
    const QString custom = qEnvironmentVariable("WILDOS_OFFICE_SOFFICE");
    return custom.isEmpty() ? QStringLiteral("/usr/lib/libreoffice/program/soffice") : custom;
}

Office::Office(QObject *parent)
    : QObject(parent)
{
    loadTemplates();
    reload();
}

QString Office::appName() const
{
    return pick(tr("Write"), tr("Tables"), tr("Slides"));
}

QString Office::iconName() const
{
    return pick(QStringLiteral("org.wildos.write"), QStringLiteral("org.wildos.tables"), QStringLiteral("org.wildos.slides"));
}

QString Office::formatName() const
{
    return pick(tr("Works with Word files"), tr("Works with Excel files"), tr("Works with PowerPoint files"));
}

QString Office::formatNote() const
{
    return pick(tr("Opens .docx and saves as .docx by default, so files still open in Word."),
                tr("Opens .xlsx and saves as .xlsx by default, so files still open in Excel."),
                tr("Opens .pptx and saves as .pptx by default, so files still open in PowerPoint."));
}

QStringList Office::nameFilters() const
{
    QStringList patterns;
    for (const QString &type : pick(writeTypes, tablesTypes, slidesTypes)) {
        patterns << QStringLiteral("*.") + type;
    }
    return {pick(tr("Documents"), tr("Spreadsheets"), tr("Presentations")) + QStringLiteral(" (") + patterns.join(QLatin1Char(' ')) + QLatin1Char(')'),
            tr("All files") + QStringLiteral(" (*)")};
}

QString Office::documentsFolder() const
{
    return QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toString();
}

QString Office::dataDir() const
{
    const QString custom = qEnvironmentVariable("WILDOS_OFFICE_DATA");
    return custom.isEmpty() ? QStringLiteral(WILDOS_OFFICE_DATA) : custom;
}

void Office::loadTemplates()
{
    struct Builtin {
        const char *id;
        QString name;
        QString description;
    };
    // Slides has no templates of its own yet: its start screen offers the
    // blank presentation and whatever the person saved as a template.
    const QList<Builtin> builtins = s_app == Slides ? QList<Builtin>{} : s_app == Tables
        ? QList<Builtin>{
              {"budget", tr("Budget"), tr("A month's income and spending, planned against actual.")},
              {"invoice", tr("Invoice"), tr("Bill a customer, with VAT and the total worked out for you.")},
              {"planner", tr("Planner"), tr("Your week hour by hour, with a to-do list and a shopping list.")},
              {"timesheet", tr("Timesheet"), tr("Hours worked each day, with breaks taken off and the week's pay.")},
              {"expenses", tr("Expenses"), tr("Everything you spend, with totals for each category.")},
          }
        : QList<Builtin>{
              {"letter", tr("Letter"), tr("A formal letter with your address, the date and a reference line.")},
              {"cv", tr("CV"), tr("Profile, experience, education and skills on one clean page.")},
              {"report", tr("Report"), tr("A cover band, summary, findings with a table, and recommendations.")},
              {"newsletter", tr("Newsletter"), tr("A masthead and two columns of news for a club or group.")},
              {"meeting-notes", tr("Meeting notes"), tr("Who came, the agenda, what was said and who does what next.")},
          };
    const QString folder = dataDir() + pick(QStringLiteral("/templates/write/"), QStringLiteral("/templates/tables/"), QStringLiteral("/templates/slides/"));
    const QString ext = pick(QStringLiteral(".ott"), QStringLiteral(".ots"), QStringLiteral(".otp"));
    for (const Builtin &b : builtins) {
        const QString file = folder + QLatin1String(b.id) + ext;
        if (!QFileInfo::exists(file)) {
            continue;
        }
        const QString thumb = folder + QLatin1String(b.id) + QStringLiteral(".png");
        m_templates.append(QVariantMap{
            {QStringLiteral("id"), QLatin1String(b.id)},
            {QStringLiteral("name"), b.name},
            {QStringLiteral("description"), b.description},
            {QStringLiteral("file"), file},
            {QStringLiteral("thumbnail"), QFileInfo::exists(thumb) ? QUrl::fromLocalFile(thumb).toString() : QString()},
        });
    }
}

bool Office::ownsFile(const QString &path) const
{
    return pick(writeTypes, tablesTypes, slidesTypes).contains(QFileInfo(path).suffix().toLower());
}

QString Office::whenText(const QDateTime &time)
{
    const QDateTime local = time.toLocalTime();
    const QDate day = local.date();
    const QDate today = QDate::currentDate();
    if (day == today) {
        return tr("Today, %1").arg(ClockFormat::time(local.time()));
    }
    if (day == today.addDays(-1)) {
        return tr("Yesterday");
    }
    // A weekday only while it can't be mistaken for one a week earlier.
    if (day < today && day > today.addDays(-5)) {
        return QLocale().dayName(day.dayOfWeek(), QLocale::ShortFormat);
    }
    return QLocale().toString(day, QStringLiteral("dd/MM/yyyy"));
}

QString Office::folderText(const QString &directory)
{
    const QString home = QDir::homePath();
    const QString dir = QDir::cleanPath(directory);
    if (dir == home) {
        return tr("Home");
    }
    QString rest;
    if (dir.startsWith(home + QLatin1Char('/'))) {
        rest = dir.mid(home.length() + 1);
    } else {
        // A USB stick or another disk: from its own name down.
        static const QRegularExpression media(QStringLiteral("^/(?:run/)?media/[^/]+/"));
        rest = dir;
        rest.remove(media);
    }
    return rest.split(QLatin1Char('/'), Qt::SkipEmptyParts).join(QStringLiteral(" > "));
}

QVariantMap Office::describe(const QString &path) const
{
    const QFileInfo info(path);
    return {
        {QStringLiteral("path"), path},
        {QStringLiteral("name"), info.fileName()},
        {QStringLiteral("folder"), folderText(info.absolutePath())},
        {QStringLiteral("when"), whenText(info.lastModified())},
        {QStringLiteral("pinned"), m_pins.contains(path)},
    };
}

// LibreOffice's recent documents, newest first. The pick list is an
// OrderList of numbered nodes, each naming a URL in its ItemList.
QStringList Office::pickList() const
{
    QFile file(registryFile());
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QMap<int, QString> order;
    QXmlStreamReader xml(&file);
    const QString orderPath = pickListPath + QStringLiteral("/OrderList");
    bool inOrder = false;
    int index = -1;
    bool inRef = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const auto name = xml.name();
            if (name == QLatin1String("item")) {
                inOrder = xml.attributes().value(QLatin1String("oor:path")) == orderPath;
            } else if (inOrder && name == QLatin1String("node")) {
                bool ok = false;
                index = xml.attributes().value(QLatin1String("oor:name")).toInt(&ok);
                if (!ok) {
                    index = -1;
                }
            } else if (inOrder && name == QLatin1String("prop")) {
                inRef = xml.attributes().value(QLatin1String("oor:name")) == QLatin1String("HistoryItemRef");
            } else if (inOrder && inRef && index >= 0 && name == QLatin1String("value")) {
                order.insert(index, xml.readElementText());
            }
        } else if (xml.isEndElement() && xml.name() == QLatin1String("item")) {
            inOrder = false;
            inRef = false;
            index = -1;
        }
    }
    return order.values();
}

void Office::loadStore()
{
    m_pins.clear();
    m_hidden.clear();
    QFile file(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/wildos-office/documents.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    for (const auto &value : root.value(QLatin1String("pinned")).toArray()) {
        m_pins << value.toString();
    }
    const QJsonObject removed = root.value(QLatin1String("removed")).toObject();
    for (auto it = removed.begin(); it != removed.end(); ++it) {
        m_hidden.insert(it.key(), qint64(it.value().toDouble()));
    }
}

void Office::saveStore() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/wildos-office");
    QDir().mkpath(dir);
    QJsonObject removed;
    for (auto it = m_hidden.begin(); it != m_hidden.end(); ++it) {
        removed.insert(it.key(), double(it.value()));
    }
    QSaveFile file(dir + QStringLiteral("/documents.json"));
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }
    file.write(QJsonDocument(QJsonObject{{QStringLiteral("pinned"), QJsonArray::fromStringList(m_pins)}, {QStringLiteral("removed"), removed}})
                   .toJson(QJsonDocument::Compact));
    file.commit();
}

void Office::reload()
{
    loadStore();
    QVariantList recent;
    QSet<QString> seen;
    for (const QString &url : pickList()) {
        const QString path = toPath(url);
        if (path.isEmpty() || seen.contains(path) || !ownsFile(path)) {
            continue;
        }
        const QFileInfo info(path);
        if (!info.exists()) {
            continue;
        }
        // Removed from the list while LibreOffice was running, so its own
        // list still has it: hidden until the document is saved again.
        const auto hidden = m_hidden.constFind(path);
        if (hidden != m_hidden.constEnd() && info.lastModified().toMSecsSinceEpoch() <= hidden.value()) {
            continue;
        }
        seen.insert(path);
        recent.append(describe(path));
    }
    QVariantList pinned;
    for (const QString &path : std::as_const(m_pins)) {
        if (ownsFile(path) && QFileInfo::exists(path)) {
            pinned.append(describe(path));
        }
    }

    QVariantList user;
    const QStringList folders = {
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/libreoffice/4/user/template"),
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + QStringLiteral("/Templates"),
    };
    const QStringList templateTypes = pick(QStringList{QStringLiteral("*.ott"), QStringLiteral("*.dotx"), QStringLiteral("*.dot")},
                                           QStringList{QStringLiteral("*.ots"), QStringLiteral("*.xltx"), QStringLiteral("*.xlt")},
                                           QStringList{QStringLiteral("*.otp"), QStringLiteral("*.potx"), QStringLiteral("*.pot")});
    for (const QString &folder : folders) {
        QDirIterator it(folder, templateTypes, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QFileInfo info(it.next());
            user.append(QVariantMap{
                {QStringLiteral("name"), info.completeBaseName()},
                {QStringLiteral("file"), info.absoluteFilePath()},
                {QStringLiteral("description"), folderText(info.absolutePath())},
            });
        }
    }

    m_recent = recent;
    m_pinned = pinned;
    m_userTemplates = user;
    Q_EMIT changed();
}

bool Office::launch(const QStringList &arguments)
{
    followSpellingLanguage();
    if (!QProcess::startDetached(soffice(), QStringList{QStringLiteral("--nologo")} + arguments)) {
        Q_EMIT failed(tr("%1 couldn't start. Try again in a moment.").arg(appName()));
        return false;
    }
    return true;
}

bool Office::isLibreOfficeRunning() const
{
    return running();
}

bool Office::open(const QString &pathOrUrl)
{
    const QString path = toPath(pathOrUrl);
    if (!QFileInfo::exists(path)) {
        Q_EMIT failed(tr("%1 has been moved or deleted.").arg(QFileInfo(path).fileName()));
        reload();
        return false;
    }
    return launch({moduleSwitch(), path});
}

bool Office::openTemplate(const QString &path)
{
    // -n opens a copy with no name, so the template itself is never saved over.
    return launch({QStringLiteral("-n"), toPath(path)});
}

bool Office::newBlank()
{
    return launch({moduleSwitch()});
}

void Office::showInFolder(const QString &path)
{
    // Files answers FileManager1 and opens the folder with the file selected.
    auto message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.FileManager1"), QStringLiteral("/org/freedesktop/FileManager1"),
                                                  QStringLiteral("org.freedesktop.FileManager1"), QStringLiteral("ShowItems"));
    message << QStringList{QUrl::fromLocalFile(path).toString()} << QString();
    const QDBusMessage reply = QDBusConnection::sessionBus().call(message, QDBus::Block, 5000);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
    }
}

void Office::pin(const QString &path)
{
    loadStore();
    if (!m_pins.contains(path)) {
        m_pins.prepend(path);
        saveStore();
    }
    reload();
}

void Office::unpin(const QString &path)
{
    loadStore();
    m_pins.removeAll(path);
    saveStore();
    reload();
}

void Office::forget(const QString &path)
{
    loadStore();
    m_pins.removeAll(path);
    const QString url = QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
    if (libreOfficeRunning()) {
        m_hidden.insert(path, QFileInfo(path).lastModified().toMSecsSinceEpoch());
    } else {
        removeFromPickList(url);
        removeFromPickList(QUrl::fromLocalFile(path).toString());
        m_hidden.remove(path);
    }
    saveStore();
    reload();
}

// The registry keeps one <item> per line. The document's ItemList entries
// go, and the OrderList closes up behind it so its numbers stay 0, 1, 2.
void Office::removeFromPickList(const QString &url) const
{
    QFile file(registryFile());
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QByteArray original = file.readAll();
    file.close();

    QString escaped;
    {
        QXmlStreamWriter writer(&escaped);
        writer.writeCharacters(url);
    }
    const QString itemPath = pickListPath + QStringLiteral("/ItemList");
    const QString orderPath = pickListPath + QStringLiteral("/OrderList");
    static const QRegularExpression orderNode(QStringLiteral("<node oor:name=\"(\\d+)\""));

    QStringList lines = QString::fromUtf8(original).split(QLatin1Char('\n'));
    int removedIndex = -1;
    for (const QString &line : std::as_const(lines)) {
        if (line.contains(orderPath) && line.contains(QStringLiteral("<value>") + escaped + QStringLiteral("</value>"))) {
            removedIndex = orderNode.match(line).captured(1).toInt();
        }
    }
    QStringList out;
    bool changedAny = false;
    for (QString line : std::as_const(lines)) {
        const bool item = line.contains(itemPath)
            && (line.contains(QStringLiteral("oor:name=\"") + escaped + QLatin1Char('"')) || line.contains(QStringLiteral("['") + escaped + QStringLiteral("']")));
        const bool order = line.contains(orderPath) && line.contains(QStringLiteral("<value>") + escaped + QStringLiteral("</value>"));
        if (item || order) {
            changedAny = true;
            continue;
        }
        if (removedIndex >= 0 && line.contains(orderPath)) {
            const auto match = orderNode.match(line);
            const int n = match.captured(1).toInt();
            if (match.hasMatch() && n > removedIndex) {
                line.replace(match.capturedStart(1), match.capturedLength(1), QString::number(n - 1));
            }
        }
        out << line;
    }
    if (!changedAny) {
        return;
    }
    QSaveFile save(registryFile());
    if (save.open(QIODevice::WriteOnly)) {
        save.write(out.join(QLatin1Char('\n')).toUtf8());
        save.commit();
    }
}

// Search documents: the recent and pinned lists, then the person's own
// folders, for file names with every word typed in them.
void Office::search(const QString &text)
{
    m_query = text.trimmed();
    const int generation = ++m_searchGeneration;
    if (m_query.isEmpty()) {
        m_results.clear();
        m_searching = false;
        Q_EMIT resultsChanged();
        return;
    }
    const QStringList words = m_query.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    auto matches = [words](const QString &name) {
        return std::all_of(words.begin(), words.end(), [&name](const QString &w) {
            return name.contains(w, Qt::CaseInsensitive);
        });
    };
    QStringList known;
    for (const QVariantList &list : {m_pinned, m_recent}) {
        for (const QVariant &entry : list) {
            const QString path = entry.toMap().value(QStringLiteral("path")).toString();
            if (!known.contains(path) && matches(QFileInfo(path).fileName())) {
                known << path;
            }
        }
    }
    m_results.clear();
    for (const QString &path : std::as_const(known)) {
        m_results.append(describe(path));
    }
    m_searching = true;
    Q_EMIT resultsChanged();

    const QStringList types = pick(writeTypes, tablesTypes, slidesTypes);
    QPointer<Office> self(this);
    QThread *thread = QThread::create([self, generation, words, types, known, matches]() {
        QStringList found;
        const QString home = QDir::homePath();
        QStringList roots;
        for (const auto location : {QStandardPaths::DocumentsLocation, QStandardPaths::DesktopLocation, QStandardPaths::DownloadLocation}) {
            roots << QStandardPaths::writableLocation(location);
        }
        roots << home;
        QSet<QString> visited;
        int scanned = 0;
        for (const QString &root : std::as_const(roots)) {
            // Home itself is only searched at the top; its usual folders
            // are searched in full above, and hidden folders never.
            const bool shallow = root == home;
            QDirIterator it(root, QDir::Files | QDir::NoDotAndDotDot, shallow ? QDirIterator::NoIteratorFlags : QDirIterator::Subdirectories);
            while (it.hasNext() && scanned < 40000 && found.size() < 60) {
                const QString path = it.next();
                ++scanned;
                const QFileInfo info = it.fileInfo();
                if (visited.contains(path) || known.contains(path) || !types.contains(info.suffix().toLower()) || !matches(info.fileName())
                    || info.fileName().startsWith(QLatin1String(".~lock"))) {
                    continue;
                }
                visited.insert(path);
                found << path;
            }
        }
        QMetaObject::invokeMethod(qApp, [self, generation, found]() {
            if (self) {
                self->finishSearch(generation, found);
            }
        });
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void Office::finishSearch(int generation, const QStringList &found)
{
    if (generation != m_searchGeneration) {
        return;
    }
    QStringList sorted = found;
    std::sort(sorted.begin(), sorted.end(), [](const QString &a, const QString &b) {
        return QFileInfo(a).lastModified() > QFileInfo(b).lastModified();
    });
    for (const QString &path : std::as_const(sorted)) {
        m_results.append(describe(path));
    }
    m_searching = false;
    Q_EMIT resultsChanged();
}
