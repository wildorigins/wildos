#include "maker.h"

#include "session.h"
#include "format.h"
#include "reader.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QStandardPaths>
#include <QCoreApplication>

namespace
{
qint64 sizeOf(const QFileInfo &info)
{
    if (info.isSymLink()) {
        return 0;
    }
    if (!info.isDir()) {
        return info.size();
    }
    qint64 total = 0;
    QDirIterator it(info.absoluteFilePath(), QDir::Files | QDir::Hidden | QDir::System | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}
}

ArchiveMaker::ArchiveMaker(QObject *parent)
    : QObject(parent)
{
    m_ticker.setInterval(100);
    connect(&m_ticker, &QTimer::timeout, this, &ArchiveMaker::progressChanged);
    m_folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

ArchiveMaker::~ArchiveMaker()
{
    stop();
}

void ArchiveMaker::stop()
{
    if (m_progress) {
        m_progress->cancel = true;
    }
    if (m_thread) {
        m_thread->wait();
    }
}

QVariantList ArchiveMaker::items() const
{
    QVariantList list;
    for (const Item &item : m_items) {
        const QFileInfo info(item.path);
        list.append(QVariantMap{
            {QStringLiteral("name"), info.fileName()},
            {QStringLiteral("folder"), Format::folder(info.absolutePath())},
            {QStringLiteral("sizeText"), Format::size(item.size)},
            {QStringLiteral("isDir"), item.dir},
        });
    }
    return list;
}

QString ArchiveMaker::totalText() const
{
    qint64 total = m_sourceSize;
    for (const Item &item : m_items) {
        total += item.size;
    }
    int count = int(m_items.size());
    if (!m_source.isEmpty()) {
        ++count;
    }
    if (count == 0) {
        return QString();
    }
    return count == 1 ? QCoreApplication::translate("ArchiveMaker", "1 item, %1").arg(Format::size(total)) : QCoreApplication::translate("ArchiveMaker", "%1 items, %2").arg(count).arg(Format::size(total));
}

QString ArchiveMaker::sourceName() const
{
    return m_source.isEmpty() ? QString() : QFileInfo(m_source).fileName();
}

QString ArchiveMaker::suffix() const
{
    return m_format == QLatin1String("7z") ? QStringLiteral(".7z") : QStringLiteral(".zip");
}

void ArchiveMaker::setName(const QString &name)
{
    QString clean = name;
    if (clean.endsWith(suffix(), Qt::CaseInsensitive)) {
        clean.chop(suffix().size());
    }
    if (clean == m_name) {
        return;
    }
    m_name = clean;
    m_named = true;
    Q_EMIT nameChanged();
}

void ArchiveMaker::setFormat(const QString &format)
{
    const QString f = format == QLatin1String("7z") ? QStringLiteral("7z") : QStringLiteral("zip");
    if (f == m_format) {
        return;
    }
    m_format = f;
    if (!m_named) {
        suggestName();
    }
    Q_EMIT nameChanged();
}

QString ArchiveMaker::folderText() const
{
    return Format::folder(m_folder);
}

QString ArchiveMaker::targetPath() const
{
    return m_folder + QLatin1Char('/') + m_name.trimmed() + suffix();
}

QString ArchiveMaker::problem() const
{
    const QString name = m_name.trimmed();
    if (name.isEmpty()) {
        return QCoreApplication::translate("ArchiveMaker", "Give the archive a name.");
    }
    if (name.contains(QLatin1Char('/')) || name == QLatin1String(".") || name == QLatin1String("..")) {
        return QCoreApplication::translate("ArchiveMaker", "A name can't contain a /.");
    }
    if (m_items.isEmpty() && m_source.isEmpty()) {
        return QCoreApplication::translate("ArchiveMaker", "Add the files and folders to put in it.");
    }
    const QFileInfo target(targetPath());
    if (target.exists() || target.isSymLink()) {
        return QCoreApplication::translate("ArchiveMaker", "There's already a file called %1 in this folder.").arg(target.fileName());
    }
    if (!QFileInfo(m_folder).isDir() || !QFileInfo(m_folder).isWritable()) {
        return QCoreApplication::translate("ArchiveMaker", "You can't save in this folder. Choose another.");
    }
    return QString();
}

double ArchiveMaker::progress() const
{
    return m_progress ? m_progress->fraction() : 0;
}

QString ArchiveMaker::progressText() const
{
    if (!m_progress) {
        return QString();
    }
    const QString name = m_progress->currentName();
    return name.isEmpty() ? QCoreApplication::translate("ArchiveMaker", "Compressing") : QCoreApplication::translate("ArchiveMaker", "Compressing %1").arg(name);
}

void ArchiveMaker::suggestName()
{
    QString base;
    QString folder = m_folder;
    if (!m_source.isEmpty()) {
        base = Format::archiveBaseName(QFileInfo(m_source).fileName());
    } else if (m_items.size() == 1) {
        const QFileInfo info(m_items.first().path);
        base = info.isDir() || info.completeBaseName().isEmpty() ? info.fileName() : info.completeBaseName();
    } else if (!m_items.isEmpty()) {
        // Several things: named after the folder they are in, as Windows
        // names a ZIP of a selection.
        base = QFileInfo(QFileInfo(m_items.first().path).absolutePath()).fileName();
    }
    if (base.isEmpty()) {
        base = QCoreApplication::translate("ArchiveMaker", "New archive");
    }
    const QString unique = Format::uniqueName(folder, base, suffix());
    m_name = unique.left(unique.size() - suffix().size());
}

void ArchiveMaker::reset(const QList<QUrl> &urls)
{
    m_items.clear();
    m_source.clear();
    m_sourcePassword.clear();
    m_sourceSize = 0;
    m_named = false;
    m_folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    addUrls(urls);
    if (!m_items.isEmpty()) {
        const QString beside = QFileInfo(m_items.first().path).absolutePath();
        if (QFileInfo(beside).isWritable()) {
            m_folder = beside;
        }
    }
    suggestName();
    Q_EMIT folderChanged();
    Q_EMIT nameChanged();
    Q_EMIT itemsChanged();
}

void ArchiveMaker::resetFrom(Archive *archive, const QList<QUrl> &urls)
{
    reset(urls);
    if (!archive || archive->path().isEmpty()) {
        return;
    }
    m_source = archive->path();
    m_sourcePassword = archive->password();
    m_sourceSize = QFileInfo(m_source).size();
    const QString beside = QFileInfo(m_source).absolutePath();
    if (QFileInfo(beside).isWritable()) {
        m_folder = beside;
    }
    // 7z for a 7z, ZIP for everything else.
    m_format = archive->formatName() == QLatin1String("7z") ? QStringLiteral("7z") : QStringLiteral("zip");
    suggestName();
    Q_EMIT folderChanged();
    Q_EMIT nameChanged();
    Q_EMIT itemsChanged();
}

void ArchiveMaker::addUrls(const QList<QUrl> &urls)
{
    for (const QUrl &url : urls) {
        if (!url.isLocalFile()) {
            continue;
        }
        const QFileInfo info(url.toLocalFile());
        const QString path = info.absoluteFilePath();
        if (!info.exists() && !info.isSymLink()) {
            continue;
        }
        const bool known = std::any_of(m_items.cbegin(), m_items.cend(), [&](const Item &item) {
            return item.path == path;
        });
        if (!known) {
            m_items.append({path, sizeOf(info), info.isDir() && !info.isSymLink()});
        }
    }
    if (!m_named) {
        suggestName();
        Q_EMIT nameChanged();
    }
    Q_EMIT itemsChanged();
}

void ArchiveMaker::removeAt(int index)
{
    if (index >= 0 && index < m_items.size()) {
        m_items.removeAt(index);
        Q_EMIT itemsChanged();
        Q_EMIT nameChanged();
    }
}

void ArchiveMaker::setFolderUrl(const QUrl &url)
{
    if (!url.isLocalFile()) {
        return;
    }
    m_folder = QDir::cleanPath(url.toLocalFile());
    if (!m_named) {
        suggestName();
    }
    Q_EMIT folderChanged();
    Q_EMIT nameChanged();
}

QUrl ArchiveMaker::folderUrl() const
{
    return QUrl::fromLocalFile(m_folder);
}

void ArchiveMaker::create()
{
    if (m_progress || !problem().isEmpty()) {
        return;
    }
    Reader::CreateOptions options;
    options.target = targetPath();
    options.format = m_format;
    for (const Item &item : m_items) {
        options.paths.append(item.path);
    }
    options.source = m_source;
    options.sourcePassword = m_sourcePassword;

    auto progress = std::make_shared<Progress>();
    m_progress = progress;
    QThread *thread = QThread::create([this, progress, options] {
        const Reader::Result result = Reader::create(options, progress.get());
        QMetaObject::invokeMethod(
            this,
            [this, progress, result, options] {
                if (progress != m_progress) {
                    return;
                }
                m_progress.reset();
                m_ticker.stop();
                Q_EMIT busyChanged();
                Q_EMIT progressChanged();
                Q_EMIT nameChanged();
                if (result.cancelled) {
                    Q_EMIT done(false, QString(), QString(), {});
                } else if (!result.ok) {
                    Q_EMIT done(false, QString(), result.error, result.failed);
                } else {
                    Q_EMIT done(true, options.target, QCoreApplication::translate("ArchiveMaker", "Made %1").arg(QFileInfo(options.target).fileName()), result.failed);
                }
            },
            Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    m_thread = thread;
    thread->start();
    m_ticker.start();
    Q_EMIT busyChanged();
    Q_EMIT progressChanged();
}

void ArchiveMaker::cancel()
{
    if (m_progress) {
        m_progress->cancel = true;
    }
}
