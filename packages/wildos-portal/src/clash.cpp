#include "clash.h"

#include "dirmodel.h"
#include "format.h"
#include "windows.h"

#include <KFileUtils>
#include <KIO/Global>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QMimeDatabase>
#include <QQuickWindow>

namespace
{
QString folderName(const QUrl &folder)
{
    if (folder.isLocalFile() && QDir::cleanPath(folder.toLocalFile()) == QDir::homePath()) {
        return QCoreApplication::translate("Clash", "Home");
    }
    const QString name = folder.adjusted(QUrl::StripTrailingSlash).fileName();
    return name.isEmpty() ? QCoreApplication::translate("Clash", "this folder") : name;
}

// "Budget 2026 (2).ods", counting from 2 as Files and Windows do.
QString suggestKeepBothName(const QUrl &dest, bool isDir)
{
    const QUrl folder = KIO::upUrl(dest);
    const QString fileName = dest.fileName();
    if (!folder.isLocalFile()) {
        return KFileUtils::suggestName(folder, fileName);
    }
    static QMimeDatabase db;
    const QString suffix = isDir ? QString() : db.suffixForFileName(fileName);
    const QString ext = suffix.isEmpty() ? QString() : QLatin1Char('.') + suffix;
    const QString base = fileName.left(fileName.size() - ext.size());
    const QDir dir(folder.toLocalFile());
    for (int n = 2;; ++n) {
        const QString name = QStringLiteral("%1 (%2)%3").arg(base).arg(n).arg(ext);
        if (!dir.exists(name)) {
            return name;
        }
    }
}

QString when(qint64 ms)
{
    return ms > 0 ? QLocale::system().toString(QDateTime::fromMSecsSinceEpoch(ms).toLocalTime().date(), QLocale::ShortFormat) : QString();
}

QString facts(qint64 ms, qint64 size, bool isDir)
{
    QStringList parts;
    if (ms > 0) {
        parts.append(when(ms));
    }
    if (size >= 0 && !isDir) {
        parts.append(Format::size(size));
    }
    return parts.join(QStringLiteral(", "));
}
}

Clash::Clash(const QVariantMap &info, QObject *parent)
    : QObject(parent)
{
    const QUrl src(info.value(QStringLiteral("source")).toString());
    const QUrl dest(info.value(QStringLiteral("dest")).toString());
    m_folders = info.value(QStringLiteral("isDir")).toBool();
    m_name = Format::visibleName(dest.fileName(), m_folders, DirModel::showExtensions());
    m_whereUrl = KIO::upUrl(dest);
    m_srcSize = info.value(QStringLiteral("sourceSize"), -1).toLongLong();
    m_destSize = info.value(QStringLiteral("destSize"), -1).toLongLong();
    m_srcTime = info.value(QStringLiteral("sourceModified")).toLongLong();
    m_destTime = info.value(QStringLiteral("destModified")).toLongLong();
    m_items = info.value(QStringLiteral("items")).toLongLong();
    m_clashes = info.value(QStringLiteral("clashes")).toLongLong();
    m_multiple = info.value(QStringLiteral("multiple")).toBool();
    m_move = info.value(QStringLiteral("move")).toBool();
    const bool sameFile = info.value(QStringLiteral("sameFile")).toBool();
    m_canReplace = !sameFile && info.value(QStringLiteral("canReplace"), true).toBool();
    m_canKeepBoth = info.value(QStringLiteral("canKeepBoth"), true).toBool();
    // Skipping the only item is the same as stopping, so it is offered
    // either way and answered as a skip or a cancel by the caller.
    m_canSkip = !sameFile;
    m_keepBothName = suggestKeepBothName(dest, m_folders);

    if (m_folders) {
        m_icon = QStringLiteral("folder");
    } else {
        static QMimeDatabase db;
        const QMimeType mime = src.isLocalFile() ? db.mimeTypeForFile(src.toLocalFile()) : db.mimeTypeForFile(dest.fileName(), QMimeDatabase::MatchExtension);
        m_icon = mime.iconName();
    }
}

QString Clash::windowTitle() const
{
    return m_folders ? tr("Merge or skip folders") : tr("Replace or skip files");
}

QString Clash::heading() const
{
    // The folder is named here, not when the clash arrives: the translations
    // load with the dialog, after the first clash is made.
    return tr("\"%1\" is already in %2").arg(m_name, folderName(m_whereUrl));
}

QString Clash::summary() const
{
    // Whole sentences, so a translation can order and agree its words.
    if (m_items > 1) {
        const QString count = QLocale::system().toString(m_items);
        const QString items = m_move ? tr("Moving %1 items.").arg(count) : tr("Copying %1 items.").arg(count);
        QString clashes;
        if (m_clashes > 1) {
            clashes = tr("%1 have the same name as items already there.").arg(m_clashes);
        } else if (m_clashes == 1) {
            clashes = m_folders ? tr("1 has the same name as a folder already there.") : tr("1 has the same name as a file already there.");
        } else {
            clashes = m_folders ? tr("This one has the same name as a folder already there.") : tr("This one has the same name as a file already there.");
        }
        return items + QLatin1Char(' ') + clashes;
    }
    if (m_folders) {
        return m_move ? tr("The folder you are moving has the same name as one already there.") : tr("The folder you are copying has the same name as one already there.");
    }
    return m_move ? tr("The file you are moving has the same name as one already there.") : tr("The file you are copying has the same name as one already there.");
}

QString Clash::replaceTitle() const
{
    return m_folders ? tr("Merge the folders") : tr("Replace the file");
}

QString Clash::replaceText() const
{
    if (m_folders) {
        return tr("Put what you are copying inside it. Files with the same name are asked about one by one.");
    }
    const QString mine = facts(m_srcTime, m_srcSize, false);
    if (m_srcTime > 0 && m_destTime > 0) {
        // Within a minute counts as the same time: copies keep their dates
        // only to the second on some drives.
        const qint64 diff = m_srcTime - m_destTime;
        return diff > 60000    ? tr("The one you are copying is newer: %1").arg(mine)
            : diff < -60000    ? tr("The one you are copying is older: %1").arg(mine)
                               : tr("The one you are copying is the same age: %1").arg(mine);
    }
    return mine.isEmpty() ? tr("Put the one you are copying in its place") : tr("The one you are copying: %1").arg(mine);
}

QString Clash::skipTitle() const
{
    return m_folders ? tr("Skip this folder") : tr("Skip this file");
}

QString Clash::skipText() const
{
    const QString theirs = facts(m_destTime, m_destSize, m_folders);
    return theirs.isEmpty() ? tr("Keep the existing one") : tr("Keep the existing one: %1").arg(theirs);
}

QString Clash::keepBothText() const
{
    const QString shown = Format::visibleName(m_keepBothName, m_folders, DirModel::showExtensions());
    return tr("The copy is saved as \"%1\"").arg(shown);
}

void Clash::answer(const QString &choice, bool all)
{
    if (m_done) {
        return;
    }
    m_done = true;
    Q_EMIT answered(choice, all && m_multiple, choice == QStringLiteral("both") ? m_keepBothName : QString());
}

ClashService::ClashService(QObject *parent)
    : QObject(parent)
{
}

QString ClashService::AskClash(const QVariantMap &info, bool &all, QString &newName)
{
    Q_UNUSED(all);
    Q_UNUSED(newName);
    setDelayedReply(true);
    const QDBusMessage call = message();
    const QString token = info.value(QStringLiteral("token")).toString();

    auto clash = new Clash(info, this);
    QQuickWindow *window = Windows::instance()->open(QStringLiteral("ClashDialog"), {{QStringLiteral("clash"), QVariant::fromValue(clash)}},
                                                     info.value(QStringLiteral("parentWindow")).toString());
    if (!window) {
        QDBusConnection::sessionBus().send(call.createErrorReply(QDBusError::Failed, QStringLiteral("The Wild OS clash dialog could not open")));
        clash->deleteLater();
        return QString();
    }
    if (!token.isEmpty()) {
        m_open.insert(token, clash);
    }
    connect(window, &QObject::destroyed, clash, &QObject::deleteLater);
    QPointer<QQuickWindow> guard(window);
    connect(clash, &Clash::answered, this, [this, call, guard, token](const QString &choice, bool everyClash, const QString &name) {
        QDBusConnection::sessionBus().send(call.createReply({choice, everyClash, name}));
        m_open.remove(token);
        if (guard) {
            guard->hide();
            guard->deleteLater();
        }
    });
    return QString();
}

void ClashService::CancelClash(const QString &token)
{
    if (auto clash = m_open.value(token)) {
        clash->cancel();
    }
}
