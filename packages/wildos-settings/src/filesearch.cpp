#include "filesearch.h"

#include <KConfig>
#include <KConfigGroup>
#include <KFormat>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

namespace
{
const QString configName = QStringLiteral("baloofilerc");

QString balooctl()
{
    return QStandardPaths::findExecutable(QStringLiteral("balooctl6"));
}

// The kcm and baloo compare folders with a trailing slash
// (filteredfoldermodel.cpp addTrailingSlashes), so they are stored that way.
QString normalise(const QString &folder)
{
    QString path = folder.startsWith(QLatin1String("file:")) ? QUrl(folder).toLocalFile() : folder;
    if (path.startsWith(QLatin1String("~/")) || path == QLatin1String("~")) {
        path.replace(0, 1, QDir::homePath());
    }
    path = QDir::cleanPath(path);
    if (!path.endsWith(QLatin1Char('/'))) {
        path += QLatin1Char('/');
    }
    return path;
}

QStringList normalised(const QStringList &list)
{
    QStringList out;
    for (const QString &f : list) {
        const QString n = normalise(f);
        if (!out.contains(n)) {
            out << n;
        }
    }
    return out;
}

QString indexPath()
{
    // baloo's own fileIndexDbPath(): BALOO_DB_PATH when set.
    const QString moved = QString::fromLocal8Bit(qgetenv("BALOO_DB_PATH"));
    if (!moved.isEmpty()) {
        return moved + QStringLiteral("/index");
    }
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/baloo/index");
}
}

FileSearch::FileSearch(QObject *parent)
    : QObject(parent)
{
}

bool FileSearch::available() const
{
    return !balooctl().isEmpty();
}

bool FileSearch::enabled() const
{
    const KConfig config(configName, KConfig::NoGlobals);
    return available() && config.group(QStringLiteral("Basic Settings")).readEntry("Indexing-Enabled", true);
}

bool FileSearch::contents() const
{
    const KConfig config(configName, KConfig::NoGlobals);
    return !config.group(QStringLiteral("General")).readEntry("only basic indexing", false);
}

bool FileSearch::hidden() const
{
    const KConfig config(configName, KConfig::NoGlobals);
    return config.group(QStringLiteral("General")).readEntry("index hidden folders", false);
}

QStringList FileSearch::folders() const
{
    const KConfig config(configName, KConfig::NoGlobals);
    return normalised(config.group(QStringLiteral("General")).readPathEntry("folders", QStringList{QDir::homePath()}));
}

QStringList FileSearch::excluded() const
{
    const KConfig config(configName, KConfig::NoGlobals);
    return normalised(config.group(QStringLiteral("General")).readPathEntry("exclude folders", QStringList()));
}

QString FileSearch::indexSize() const
{
    const QFileInfo index(indexPath());
    return index.exists() ? KFormat().formatByteSize(double(index.size())) : QString();
}

QString FileSearch::display(const QString &folder) const
{
    QString path = normalise(folder);
    path.chop(1);
    const QString home = QDir::homePath();
    if (path == home) {
        return tr("Your home folder");
    }
    if (path.startsWith(home + QLatin1Char('/'))) {
        return QLatin1Char('~') + path.mid(home.length());
    }
    return path.isEmpty() ? QStringLiteral("/") : path;
}

void FileSearch::setEnabled(bool on)
{
    if (!available()) {
        return;
    }
    KConfig config(configName, KConfig::NoGlobals);
    config.group(QStringLiteral("Basic Settings")).writeEntry("Indexing-Enabled", on);
    config.sync();
    // KDE's module turns the search runner off with indexing, so nothing
    // offers to search an index that is not kept.
    KConfig runner(QStringLiteral("krunnerrc"));
    runner.group(QStringLiteral("Plugins")).writeEntry("baloosearchEnabled", on, KConfig::Notify);
    runner.sync();
    apply(true);
}

void FileSearch::setContents(bool on)
{
    KConfig config(configName, KConfig::NoGlobals);
    config.group(QStringLiteral("General")).writeEntry("only basic indexing", !on);
    config.sync();
    apply(false);
}

void FileSearch::setHidden(bool on)
{
    KConfig config(configName, KConfig::NoGlobals);
    config.group(QStringLiteral("General")).writeEntry("index hidden folders", on);
    config.sync();
    apply(false);
}

void FileSearch::writeList(const char *key, const QStringList &list)
{
    KConfig config(configName, KConfig::NoGlobals);
    QStringList sorted = list;
    sorted.sort();
    config.group(QStringLiteral("General")).writePathEntry(key, sorted);
    config.sync();
    apply(false);
}

void FileSearch::addFolder(const QString &folder)
{
    const QString path = normalise(folder);
    QStringList list = folders();
    QStringList out = excluded();
    if (out.removeAll(path) > 0) {
        writeList("exclude folders", out);
    }
    if (!list.contains(path)) {
        list << path;
        writeList("folders", list);
    }
}

void FileSearch::removeFolder(const QString &folder)
{
    QStringList list = folders();
    if (list.removeAll(normalise(folder)) > 0) {
        writeList("folders", list);
    }
}

void FileSearch::exclude(const QString &folder)
{
    const QString path = normalise(folder);
    QStringList list = excluded();
    QStringList in = folders();
    if (in.removeAll(path) > 0) {
        writeList("folders", in);
    }
    if (!list.contains(path)) {
        list << path;
        writeList("exclude folders", list);
    }
}

void FileSearch::include(const QString &folder)
{
    QStringList list = excluded();
    if (list.removeAll(normalise(folder)) > 0) {
        writeList("exclude folders", list);
    }
}

void FileSearch::apply(bool startOrStop)
{
    if (startOrStop) {
        // "balooctl6 enable" starts baloo_file from where the build put it
        // (KDE_INSTALL_FULL_LIBEXECDIR_KF), which is not on PATH.
        QProcess::startDetached(balooctl(), {enabled() ? QStringLiteral("enable") : QStringLiteral("disable")});
    } else if (enabled()) {
        // Goes nowhere when the indexer is not running, which is fine: it
        // reads the file when it starts.
        auto message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.baloo"), QStringLiteral("/"), QStringLiteral("org.kde.baloo.main"),
                                                      QStringLiteral("updateConfig"));
        QDBusConnection::sessionBus().send(message);
    }
    Q_EMIT changed();
}

void FileSearch::rebuild()
{
    if (!available() || m_rebuilding) {
        return;
    }
    m_rebuilding = true;
    Q_EMIT rebuildingChanged();
    auto process = new QProcess(this);
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
        const QString output = QString::fromUtf8(process->readAll()).trimmed();
        process->deleteLater();
        m_rebuilding = false;
        Q_EMIT rebuildingChanged();
        if (status != QProcess::NormalExit || code != 0) {
            Q_EMIT failed(output.section(QLatin1Char('\n'), -1));
        }
        Q_EMIT changed();
    });
    // purge stops the indexer, deletes the index and starts it again.
    process->start(balooctl(), {QStringLiteral("purge")});
}

void FileSearch::reload()
{
    Q_EMIT changed();
}
