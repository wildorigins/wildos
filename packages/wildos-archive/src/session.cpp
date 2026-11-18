#include "session.h"

#include "archiveapp.h"
#include "format.h"
#include "thumbnails.h"
#include "tools.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUuid>
#include <QCoreApplication>

namespace
{
// Thumbnails are made for at most this many pictures, each no bigger than
// this, so a huge photo archive cannot fill the memory.
constexpr int maxThumbnails = 600;
constexpr qint64 maxThumbnailSource = 48 * 1024 * 1024;
const QSize thumbnailBox(176, 132);
}

Archive::Archive(QObject *parent)
    : QObject(parent)
    , m_model(new EntryModel(this))
    , m_key(QUuid::createUuid().toString(QUuid::Id128))
{
    m_ticker.setInterval(100);
    connect(&m_ticker, &QTimer::timeout, this, &Archive::jobProgressChanged);
}

Archive::~Archive()
{
    stopAll();
    Thumbnails::drop(m_key);
    Tools::wipe(m_password);
}

void Archive::stopAll()
{
    if (m_progress) {
        m_progress->cancel = true;
    }
    if (m_thumbProgress) {
        m_thumbProgress->cancel = true;
    }
    // Every worker checks for a cancel between blocks, so this is short.
    for (const QPointer<QThread> &thread : std::as_const(m_threads)) {
        if (thread) {
            thread->wait();
        }
    }
    m_threads.clear();
}

void Archive::start(const QString &job, std::function<Done(Progress *)> work)
{
    auto progress = std::make_shared<Progress>();
    m_progress = progress;
    m_job = job;
    const int generation = m_generation;
    QThread *thread = QThread::create([this, progress, work, generation] {
        Done done = work(progress.get());
        QMetaObject::invokeMethod(
            this,
            [this, progress, done, generation] {
                if (progress != m_progress || generation != m_generation) {
                    return;
                }
                m_progress.reset();
                m_job.clear();
                m_ticker.stop();
                if (done) {
                    done();
                }
                Q_EMIT jobChanged();
                Q_EMIT jobProgressChanged();
            },
            Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    m_threads.removeAll(nullptr);
    m_threads.append(thread);
    thread->start();
    m_ticker.start();
    Q_EMIT jobChanged();
    Q_EMIT jobProgressChanged();
}

double Archive::jobProgress() const
{
    return m_progress ? m_progress->fraction() : 0;
}

QString Archive::jobText() const
{
    if (!m_progress) {
        return QString();
    }
    const QString name = m_progress->currentName();
    if (m_job == QLatin1String("extract")) {
        return name.isEmpty() ? QCoreApplication::translate("Archive", "Extracting") : QCoreApplication::translate("Archive", "Extracting %1").arg(name);
    }
    if (m_job == QLatin1String("test")) {
        return name.isEmpty() ? QCoreApplication::translate("Archive", "Testing") : QCoreApplication::translate("Archive", "Testing %1").arg(name);
    }
    if (m_job == QLatin1String("open")) {
        return QCoreApplication::translate("Archive", "Opening %1").arg(name);
    }
    return QCoreApplication::translate("Archive", "Reading the archive");
}

QString Archive::fileName() const
{
    return QFileInfo(m_path).fileName();
}

QString Archive::baseName() const
{
    return Format::archiveBaseName(fileName());
}

QString Archive::formatName() const
{
    return m_listing ? m_listing->format : QString();
}

QString Archive::summary() const
{
    if (!m_listing || m_state != QLatin1String("ready")) {
        return QString();
    }
    return QCoreApplication::translate("Archive", "%1 · %2 unpacked, %3 packed")
        .arg(m_listing->format.isEmpty() ? QCoreApplication::translate("Archive", "Archive") : m_listing->format, Format::size(m_listing->totalSize),
             Format::size(m_listing->packedSize));
}

bool Archive::encrypted() const
{
    return m_listing && m_listing->encrypted;
}

bool Archive::lockedFormat() const
{
    return m_listing && m_listing->cantDecrypt;
}

bool Archive::namesHidden() const
{
    return m_listing && m_listing->lockedHeaders;
}

int Archive::unsafeCount() const
{
    if (!m_listing) {
        return 0;
    }
    return int(std::count_if(m_listing->entries.cbegin(), m_listing->entries.cend(), [](const Entry &e) {
        return e.unsafe;
    }));
}

void Archive::openUrl(const QUrl &url)
{
    open(url.isLocalFile() ? url.toLocalFile() : url.toString());
}

void Archive::open(const QString &path)
{
    stopAll();
    ++m_generation;
    Thumbnails::drop(m_key);
    m_key = QUuid::createUuid().toString(QUuid::Id128);
    m_path = QFileInfo(path).absoluteFilePath();
    m_state = QStringLiteral("loading");
    m_error.clear();
    // Every worker has stopped, so no copy of the password is in use.
    Tools::wipe(m_password);
    m_pending = nullptr;
    m_listing.reset();
    m_dirs.clear();
    m_model->setListing(nullptr, m_key);
    m_folder.clear();
    m_history.clear();
    m_search.clear();
    m_anchor = -1;
    m_destinationChosen = false;
    m_destination = QFileInfo(m_path).absolutePath() + QLatin1Char('/') + baseName();
    Q_EMIT loaded();
    Q_EMIT viewChanged();
    Q_EMIT destinationChanged();
    Q_EMIT passwordChanged();
    updatePane();

    const QString file = m_path;
    start(QStringLiteral("list"), [this, file](Progress *progress) -> Done {
        auto listing = std::make_shared<Listing>(Reader::list(file, progress));
        return [this, listing] {
            if (!listing->error.isEmpty() && listing->entries.isEmpty() && !listing->lockedHeaders) {
                m_state = QStringLiteral("failed");
                m_error = listing->error;
                Q_EMIT loaded();
                return;
            }
            adopt(listing);
            ArchiveApp::instance()->addRecent(m_path);
            Q_EMIT loaded();
            // Asked straight away, as Windows does: the names, or the
            // files, are no use without it.
            if ((listing->encrypted || listing->lockedHeaders) && !listing->cantDecrypt) {
                Q_EMIT passwordNeeded(false);
            }
            startThumbnails();
        };
    });
}

void Archive::adopt(const std::shared_ptr<const Listing> &listing)
{
    m_listing = listing;
    m_state = QStringLiteral("ready");
    m_error = listing->lockedHeaders ? QString() : listing->error;
    m_dirs.clear();
    for (int i = 0; i < listing->entries.size(); ++i) {
        if (listing->entries[i].dir) {
            m_dirs.insert(listing->entries[i].path, i);
        }
    }
    m_model->setListing(m_listing.get(), m_key);
    m_history.clear();
    // An archive that holds only one folder opens inside it.
    const QList<int> &roots = listing->roots;
    const bool single = roots.size() == 1 && listing->entries[roots.first()].dir;
    show(single ? listing->entries[roots.first()].path : QString(), false);
}

void Archive::setPassword(const QString &password)
{
    if (!m_listing || password.isEmpty()) {
        return;
    }
    QByteArray bytes = password.toUtf8();
    const auto listing = m_listing;
    const QString file = m_path;
    start(QStringLiteral("password"), [this, bytes, listing, file](Progress *progress) -> Done {
        auto unlocked = std::make_shared<Reader::Unlock>(Reader::unlock(file, *listing, bytes, progress));
        return [this, bytes, unlocked]() mutable {
            if (unlocked->state != Reader::PasswordState::Fine) {
                Tools::wipe(bytes);
                Q_EMIT passwordNeeded(unlocked->state == Reader::PasswordState::Wrong);
                return;
            }
            m_password = bytes;
            if (unlocked->relisted) {
                const auto named = std::make_shared<const Listing>(std::move(unlocked->listing));
                if (!named->error.isEmpty() && named->entries.isEmpty()) {
                    m_state = QStringLiteral("failed");
                    m_error = named->error;
                    Q_EMIT loaded();
                    return;
                }
                adopt(named);
                Q_EMIT loaded();
            }
            Q_EMIT passwordChanged();
            startThumbnails();
            if (m_pending) {
                const Done pending = m_pending;
                m_pending = nullptr;
                QMetaObject::invokeMethod(this, pending, Qt::QueuedConnection);
            }
        };
    });
}

void Archive::forgetPending()
{
    m_pending = nullptr;
}

void Archive::askPassword()
{
    if (m_listing && !m_listing->cantDecrypt && m_password.isEmpty() && (m_listing->encrypted || m_listing->lockedHeaders) && m_job.isEmpty()) {
        Q_EMIT passwordNeeded(false);
    }
}

bool Archive::needPassword(const Done &retry)
{
    if (!m_listing || m_listing->cantDecrypt || !m_password.isEmpty() || (!m_listing->encrypted && !m_listing->lockedHeaders)) {
        return false;
    }
    m_pending = retry;
    Q_EMIT passwordNeeded(false);
    return true;
}

void Archive::startThumbnails()
{
    if (!m_listing) {
        return;
    }
    if (m_thumbProgress) {
        m_thumbProgress->cancel = true;
    }
    QSet<int> orders;
    for (const Entry &e : m_listing->entries) {
        if (e.image && e.order >= 0 && e.size > 0 && e.size <= maxThumbnailSource && (!e.encrypted || !m_password.isEmpty())
            && Thumbnails::find(m_key + QLatin1Char('/') + QString::number(e.order)).isNull()) {
            orders.insert(e.order);
            if (orders.size() >= maxThumbnails) {
                break;
            }
        }
    }
    if (orders.isEmpty()) {
        return;
    }
    auto progress = std::make_shared<Progress>();
    m_thumbProgress = progress;
    const auto listing = m_listing;
    const QString file = m_path;
    const QString key = m_key;
    const QByteArray password = m_password;
    const int generation = m_generation;
    QThread *thread = QThread::create([this, progress, listing, file, key, password, orders, generation] {
        Reader::thumbnails(file, password, *listing, orders, thumbnailBox, progress.get(), [&](int order, const QImage &image) {
            Thumbnails::insert(key + QLatin1Char('/') + QString::number(order), image);
            const int entry = listing->byOrder.value(order, -1);
            QMetaObject::invokeMethod(
                this,
                [this, entry, generation] {
                    if (generation == m_generation && entry >= 0) {
                        m_model->thumbnailReady(entry);
                        updatePane();
                    }
                },
                Qt::QueuedConnection);
        });
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    m_threads.removeAll(nullptr);
    m_threads.append(thread);
    thread->start(QThread::LowPriority);
}

void Archive::show(const QString &folder, bool remember)
{
    if (remember && folder != m_folder) {
        m_history.append(m_folder);
    }
    m_folder = folder;
    m_search.clear();
    m_anchor = -1;
    refreshView();
}

void Archive::refreshView()
{
    QList<int> rows;
    if (m_listing) {
        if (!m_search.isEmpty()) {
            for (int i = 0; i < m_listing->entries.size() && rows.size() < 5000; ++i) {
                if (m_listing->entries[i].name.contains(m_search, Qt::CaseInsensitive)) {
                    rows.append(i);
                }
            }
        } else if (m_folder.isEmpty()) {
            rows = m_listing->roots;
        } else {
            const int dir = m_dirs.value(m_folder, -1);
            rows = dir >= 0 ? m_listing->entries[dir].children : m_listing->roots;
            if (dir < 0) {
                m_folder.clear();
            }
        }
    }
    m_model->show(rows, !m_search.isEmpty());
    Q_EMIT viewChanged();
    updatePane();
}

QVariantList Archive::crumbs() const
{
    QVariantList list;
    if (m_folder.isEmpty()) {
        return list;
    }
    QString at;
    for (const QString &step : m_folder.split(QLatin1Char('/'))) {
        at = at.isEmpty() ? step : at + QLatin1Char('/') + step;
        list.append(QVariantMap{{QStringLiteral("name"), step}, {QStringLiteral("path"), at}});
    }
    return list;
}

void Archive::setSearch(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed == m_search) {
        return;
    }
    m_search = trimmed;
    m_anchor = -1;
    refreshView();
}

void Archive::goTo(const QString &folder)
{
    show(folder, true);
}

void Archive::up()
{
    if (!m_search.isEmpty()) {
        m_search.clear();
        refreshView();
        return;
    }
    if (m_folder.isEmpty()) {
        return;
    }
    const int slash = int(m_folder.lastIndexOf(QLatin1Char('/')));
    show(slash < 0 ? QString() : m_folder.left(slash), true);
}

void Archive::back()
{
    if (m_history.isEmpty()) {
        return;
    }
    const QString folder = m_history.takeLast();
    show(folder, false);
}

void Archive::sortBy(const QString &key)
{
    if (m_sortKey == key) {
        m_sortAscending = !m_sortAscending;
    } else {
        m_sortKey = key;
        m_sortAscending = true;
    }
    m_model->setSort(m_sortKey, m_sortAscending);
    Q_EMIT viewChanged();
}

void Archive::activate(int row)
{
    const int entry = m_model->entryAt(row);
    if (entry < 0 || !m_listing) {
        return;
    }
    const Entry &e = m_listing->entries[entry];
    if (e.dir) {
        show(e.path, true);
    } else if (!e.unsafe && !e.symlink) {
        openEntry(entry);
    }
}

void Archive::toggle(int row)
{
    const int entry = m_model->entryAt(row);
    if (entry < 0) {
        return;
    }
    QSet<int> checked = m_model->checked();
    if (!checked.remove(entry)) {
        checked.insert(entry);
    }
    m_anchor = row;
    m_model->setChecked(checked);
    updatePane();
}

void Archive::select(int row, int modifiers)
{
    const int entry = m_model->entryAt(row);
    if (entry < 0) {
        return;
    }
    QSet<int> checked = m_model->checked();
    if ((modifiers & Qt::ShiftModifier) && m_anchor >= 0) {
        if (!(modifiers & Qt::ControlModifier)) {
            checked.clear();
        }
        for (int r = qMin(row, m_anchor); r <= qMax(row, m_anchor); ++r) {
            checked.insert(m_model->entryAt(r));
        }
    } else if (modifiers & Qt::ControlModifier) {
        if (!checked.remove(entry)) {
            checked.insert(entry);
        }
        m_anchor = row;
    } else {
        checked = {entry};
        m_anchor = row;
    }
    m_model->setChecked(checked);
    updatePane();
}

void Archive::selectAll()
{
    QSet<int> all;
    for (int entry : m_model->rows()) {
        all.insert(entry);
    }
    m_model->setChecked(all);
    updatePane();
}

void Archive::clearSelection()
{
    m_model->setChecked({});
    m_anchor = -1;
    updatePane();
}

QList<int> Archive::filesUnder(const QList<int> &entries) const
{
    // In the order the rows show them, so the pane's pictures match.
    QList<int> files;
    QList<int> stack(entries.crbegin(), entries.crend());
    while (!stack.isEmpty()) {
        const int i = stack.takeLast();
        const Entry &e = m_listing->entries[i];
        if (e.dir) {
            stack.append(QList<int>(e.children.crbegin(), e.children.crend()));
        } else if (!e.unsafe) {
            files.append(i);
        }
    }
    return files;
}

void Archive::collectOrders(int entry, QSet<int> *orders) const
{
    const Entry &e = m_listing->entries[entry];
    if (e.order >= 0) {
        orders->insert(e.order);
    }
    for (int child : e.children) {
        collectOrders(child, orders);
    }
}

double Archive::rate() const
{
    // Rough speeds for decompressing and writing on an ordinary SSD: plain
    // and deflate ZIPs are quick, LZMA and BZIP2 a good deal slower.
    const QString f = formatName();
    if (f == QLatin1String("ZIP") || f == QLatin1String("TAR") || f == QLatin1String("CPIO")) {
        return 110.0 * 1024 * 1024;
    }
    if (f.endsWith(QLatin1String("GZ")) || f.endsWith(QLatin1String("ZST")) || f.endsWith(QLatin1String("LZ4"))) {
        return 90.0 * 1024 * 1024;
    }
    return 35.0 * 1024 * 1024;
}

void Archive::updatePane()
{
    m_paneThumbs.clear();
    if (!m_listing) {
        m_paneTitle = QCoreApplication::translate("Archive", "Extract everything");
        m_paneSize.clear();
    } else {
        const QSet<int> &checked = m_model->checked();
        QList<int> chosen;
        for (int entry : m_model->rows()) {
            if (checked.contains(entry)) {
                chosen.append(entry);
            }
        }
        const QList<int> files = chosen.isEmpty() ? filesUnder(m_listing->roots) : filesUnder(chosen);
        qint64 bytes = 0;
        bool allImages = !files.isEmpty();
        for (int i : files) {
            bytes += m_listing->entries[i].size;
            allImages = allImages && m_listing->entries[i].image;
        }
        const int n = int(files.size());
        if (chosen.isEmpty()) {
            m_paneTitle = QCoreApplication::translate("Archive", "Extract everything");
        } else if (allImages) {
            m_paneTitle = n == 1 ? QCoreApplication::translate("Archive", "Extract 1 photo") : QCoreApplication::translate("Archive", "Extract %1 photos").arg(n);
        } else {
            m_paneTitle = n == 1 ? QCoreApplication::translate("Archive", "Extract 1 file") : QCoreApplication::translate("Archive", "Extract %1 files").arg(n);
        }
        m_paneSize = Format::size(bytes) + QStringLiteral(" · ") + Format::duration(bytes / rate() + n * 0.004);
        for (int i : chosen.isEmpty() ? QList<int>() : files) {
            const Entry &e = m_listing->entries[i];
            if (!e.image) {
                continue;
            }
            const QString key = m_key + QLatin1Char('/') + QString::number(e.order);
            if (!Thumbnails::find(key).isNull()) {
                m_paneThumbs.append(QStringLiteral("image://archivethumb/") + key);
                if (m_paneThumbs.size() == 3) {
                    break;
                }
            }
        }
    }
    Q_EMIT selectionChanged();
}

void Archive::setDestination(const QString &path)
{
    const QString clean = QDir::cleanPath(path);
    if (clean == m_destination) {
        return;
    }
    m_destination = clean;
    m_destinationChosen = true;
    Q_EMIT destinationChanged();
}

QString Archive::destinationText() const
{
    return Format::folder(m_destination);
}

void Archive::setDestinationUrl(const QUrl &url)
{
    if (url.isLocalFile()) {
        setDestination(url.toLocalFile());
    }
}

QUrl Archive::destinationUrl() const
{
    // The picker opens at the nearest folder that is already there.
    QString at = m_destination;
    while (!at.isEmpty() && !QFileInfo(at).isDir()) {
        at = QFileInfo(at).absolutePath();
    }
    return QUrl::fromLocalFile(at);
}

QUrl Archive::folderUrl() const
{
    return QUrl::fromLocalFile(m_path.isEmpty() ? QDir::homePath() : QFileInfo(m_path).absolutePath());
}

void Archive::setKeepStructure(bool on)
{
    if (m_keepStructure != on) {
        m_keepStructure = on;
        Q_EMIT optionsChanged();
    }
}

void Archive::setOpenWhenDone(bool on)
{
    if (m_openWhenDone != on) {
        m_openWhenDone = on;
        Q_EMIT optionsChanged();
    }
}

void Archive::setReplaceExisting(bool on)
{
    if (m_replace != on) {
        m_replace = on;
        Q_EMIT optionsChanged();
    }
}

void Archive::extract(bool everything)
{
    if (!m_listing || !m_job.isEmpty()) {
        return;
    }
    if (needPassword([this, everything] {
            extract(everything);
        })) {
        return;
    }
    Reader::ExtractOptions options;
    options.archive = m_path;
    options.password = m_password;
    options.destination = m_destination;
    options.keepStructure = m_keepStructure;
    options.replace = m_replace;
    QString what = m_paneTitle;
    QList<int> chosen;
    if (!everything) {
        for (int entry : m_model->rows()) {
            if (m_model->checked().contains(entry)) {
                chosen.append(entry);
            }
        }
    }
    if (chosen.isEmpty()) {
        what = QCoreApplication::translate("Archive", "Extract everything");
    } else {
        for (int entry : chosen) {
            collectOrders(entry, &options.orders);
        }
        // Picked from a folder of the archive, they keep their places
        // below that folder, not the whole path from the top.
        if (m_search.isEmpty()) {
            options.stripPrefix = m_folder;
        }
    }
    runExtract(options, what, m_openWhenDone);
}

void Archive::extractHere(const QString &whenThere)
{
    if (!m_listing || !m_job.isEmpty()) {
        return;
    }
    if (needPassword([this, whenThere] {
            extractHere(whenThere);
        })) {
        return;
    }
    const QString folder = QFileInfo(m_path).absolutePath();
    Reader::ExtractOptions options;
    options.archive = m_path;
    options.password = m_password;
    options.keepStructure = true;
    options.replace = false;
    const QList<int> &roots = m_listing->roots;
    const Entry *single = roots.size() == 1 ? &m_listing->entries[roots.first()] : nullptr;
    QString name;
    if (single && single->dir && !single->unsafe) {
        name = single->name;
        options.stripPrefix = single->path;
    } else if (single && !single->unsafe && !QFileInfo(folder + QLatin1Char('/') + single->name).exists()
               && !QFileInfo(folder + QLatin1Char('/') + single->name).isSymLink()) {
        options.destination = folder;
    } else {
        name = baseName();
    }
    if (!name.isEmpty()) {
        const QFileInfo there(folder + QLatin1Char('/') + name);
        // Extracting the same archive twice asks, as Windows does, rather
        // than quietly making another copy beside the first.
        if ((there.exists() || there.isSymLink()) && whenThere.isEmpty()) {
            Q_EMIT alreadyThere(name);
            return;
        }
        if (there.isDir() && !there.isSymLink() && whenThere == QLatin1String("replace")) {
            options.destination = there.absoluteFilePath();
            options.replace = true;
        } else {
            options.destination = folder + QLatin1Char('/') + Format::uniqueName(folder, name, QString());
        }
    }
    m_destination = options.destination;
    Q_EMIT destinationChanged();
    runExtract(options, QCoreApplication::translate("Archive", "Extract everything"), false);
}

void Archive::runExtract(Reader::ExtractOptions options, const QString &what, bool openAfter)
{
    const auto listing = m_listing;
    const QString destination = options.destination;
    start(QStringLiteral("extract"), [this, options, listing, what, openAfter, destination](Progress *progress) -> Done {
        const Reader::Result result = Reader::extract(options, *listing, progress);
        return [this, result, what, openAfter, destination, options] {
            const QString where = QFileInfo(destination).fileName().isEmpty() ? Format::folder(destination) : QFileInfo(destination).fileName();
            if (result.password != Reader::PasswordState::Fine) {
                m_password.clear();
                Q_EMIT passwordChanged();
                m_pending = [this, options, what, openAfter] {
                    Reader::ExtractOptions again = options;
                    again.password = m_password;
                    runExtract(again, what, openAfter);
                };
                Q_EMIT passwordNeeded(result.password == Reader::PasswordState::Wrong);
                return;
            }
            if (result.cancelled) {
                Q_EMIT finished(QStringLiteral("extract"), false, QString(),
                                result.written > 0 ? QCoreApplication::translate("Archive", "Stopped. The files already extracted are in %1.").arg(where)
                                                   : QCoreApplication::translate("Archive", "Stopped. Nothing was extracted."),
                                {});
                return;
            }
            if (!result.error.isEmpty()) {
                Q_EMIT finished(QStringLiteral("extract"), false, QCoreApplication::translate("Archive", "Couldn't finish extracting"), result.error, result.failed);
                return;
            }
            if (openAfter && result.written > 0) {
                ArchiveApp::instance()->showFolder(destination);
            }
            if (!result.unsafe.isEmpty()) {
                Q_EMIT finished(QStringLiteral("extract"), false, QCoreApplication::translate("Archive", "Some items were skipped"),
                                QCoreApplication::translate("Archive", "These would have been written outside %1, so Archive didn't extract them. Everything else is there.")
                                    .arg(where),
                                result.unsafe);
                return;
            }
            if (!result.failed.isEmpty()) {
                Q_EMIT finished(QStringLiteral("extract"), false, QCoreApplication::translate("Archive", "Some files couldn't be extracted"),
                                QCoreApplication::translate("Archive", "The rest are in %1.").arg(where), result.failed);
                return;
            }
            QString note = result.written == 1 ? QCoreApplication::translate("Archive", "Extracted 1 file to %1").arg(where)
                                               : QCoreApplication::translate("Archive", "Extracted %1 files to %2").arg(result.written).arg(where);
            if (result.kept > 0) {
                note += result.kept == 1 ? QCoreApplication::translate("Archive", ". 1 file with the same name was already there and was kept.")
                                         : QCoreApplication::translate("Archive", ". %1 files with the same names were already there and were kept.").arg(result.kept);
            }
            Q_EMIT finished(QStringLiteral("extract"), true, QString(), note, {});
        };
    });
}

void Archive::extractForDrag(int row)
{
    const int entry = m_model->entryAt(row);
    if (!m_listing || !m_job.isEmpty() || entry < 0) {
        return;
    }
    // Once the password has been typed the drag is over, so nothing is
    // carried on afterwards: the rows are dragged again.
    if (needPassword([] {})) {
        return;
    }
    QList<int> chosen;
    if (m_model->checked().contains(entry)) {
        for (int e : m_model->rows()) {
            if (m_model->checked().contains(e)) {
                chosen.append(e);
            }
        }
    } else {
        chosen.append(entry);
    }
    Reader::ExtractOptions options;
    options.archive = m_path;
    options.password = m_password;
    options.keepStructure = true;
    options.replace = true;
    // From a folder of the archive the rows land by their own names; search
    // results come from all over it and keep their paths.
    const bool flat = m_search.isEmpty();
    if (flat) {
        options.stripPrefix = m_folder;
    }
    QStringList paths;
    for (int e : std::as_const(chosen)) {
        const Entry &item = m_listing->entries[e];
        if (item.unsafe) {
            continue;
        }
        collectOrders(e, &options.orders);
        paths.append(flat ? item.name : item.path);
    }
    // No orders would mean the whole archive.
    if (options.orders.isEmpty()) {
        return;
    }
    const QString dir = ArchiveApp::instance()->openingFolder();
    if (dir.isEmpty()) {
        return;
    }
    options.destination = dir;
    const auto listing = m_listing;
    start(QStringLiteral("extract"), [this, options, listing, dir, paths](Progress *progress) -> Done {
        const Reader::Result result = Reader::extract(options, *listing, progress);
        return [this, result, dir, paths] {
            if (result.password != Reader::PasswordState::Fine) {
                m_password.clear();
                m_pending = nullptr;
                Q_EMIT passwordChanged();
                Q_EMIT passwordNeeded(result.password == Reader::PasswordState::Wrong);
                return;
            }
            if (result.cancelled) {
                return;
            }
            QStringList urls;
            for (const QString &path : paths) {
                const QFileInfo info(dir + QLatin1Char('/') + path);
                if (info.exists() || info.isSymLink()) {
                    urls.append(QUrl::fromLocalFile(info.absoluteFilePath()).toString());
                }
            }
            if (urls.isEmpty()) {
                Q_EMIT finished(QStringLiteral("extract"), false, QCoreApplication::translate("Archive", "Couldn't get those ready to drag"),
                                result.error.isEmpty() ? QCoreApplication::translate("Archive", "Nothing could be read from the archive. Try Extract instead.") : result.error,
                                result.failed);
                return;
            }
            // On the next turn of the event loop: the drag runs a loop of
            // its own until the drop, and the job has to be over first.
            QMetaObject::invokeMethod(this, [this, urls] { Q_EMIT dragReady(urls); }, Qt::QueuedConnection);
        };
    });
}

void Archive::test()
{
    if (!m_listing || !m_job.isEmpty()) {
        return;
    }
    if (needPassword([this] {
            test();
        })) {
        return;
    }
    const auto listing = m_listing;
    const QString file = m_path;
    const QByteArray password = m_password;
    start(QStringLiteral("test"), [this, listing, file, password](Progress *progress) -> Done {
        const Reader::Result result = Reader::test(file, password, *listing, progress);
        return [this, result] {
            if (result.password != Reader::PasswordState::Fine) {
                m_password.clear();
                Q_EMIT passwordChanged();
                m_pending = [this] {
                    test();
                };
                Q_EMIT passwordNeeded(result.password == Reader::PasswordState::Wrong);
                return;
            }
            if (result.cancelled) {
                Q_EMIT finished(QStringLiteral("test"), false, QString(), QCoreApplication::translate("Archive", "Test stopped."), {});
                return;
            }
            if (!result.error.isEmpty()) {
                Q_EMIT finished(QStringLiteral("test"), false, QCoreApplication::translate("Archive", "This archive is damaged"),
                                QCoreApplication::translate("Archive", "%1 files checked out before the damage. %2").arg(result.written).arg(result.error), result.failed);
                return;
            }
            if (!result.failed.isEmpty()) {
                Q_EMIT finished(QStringLiteral("test"), false,
                                result.failed.size() == 1 ? QCoreApplication::translate("Archive", "1 file is damaged") : QCoreApplication::translate("Archive", "%1 files are damaged").arg(result.failed.size()),
                                QCoreApplication::translate("Archive", "The other %1 are OK.").arg(result.written), result.failed);
                return;
            }
            Q_EMIT finished(QStringLiteral("test"), true, QCoreApplication::translate("Archive", "Everything is OK"),
                            result.written == 1 ? QCoreApplication::translate("Archive", "The 1 file in this archive is intact.")
                                                : QCoreApplication::translate("Archive", "All %1 files in this archive are intact.").arg(result.written),
                            {});
        };
    });
}

void Archive::openEntry(int entry)
{
    if (!m_listing || !m_job.isEmpty()) {
        return;
    }
    if (m_listing->entries[entry].encrypted && needPassword([this, entry] {
            openEntry(entry);
        })) {
        return;
    }
    const Entry &e = m_listing->entries[entry];
    // A private copy that the app it opens in can read; Archive clears
    // these out the next time it starts.
    const QString dir = ArchiveApp::instance()->openingFolder();
    if (dir.isEmpty()) {
        return;
    }
    const QString target = dir + QLatin1Char('/') + e.name;
    const auto listing = m_listing;
    const QString file = m_path;
    const QByteArray password = m_password;
    const int order = e.order;
    const QString name = e.name;
    start(QStringLiteral("open"), [this, listing, file, password, order, target, name](Progress *progress) -> Done {
        progress->setCurrent(name);
        const Reader::Result result = Reader::readOne(file, password, *listing, order, target, progress);
        return [this, result, target, name] {
            if (result.password != Reader::PasswordState::Fine) {
                m_password.clear();
                Q_EMIT passwordChanged();
                Q_EMIT passwordNeeded(result.password == Reader::PasswordState::Wrong);
                return;
            }
            if (!result.ok) {
                if (!result.cancelled) {
                    Q_EMIT finished(QStringLiteral("open"), false, QCoreApplication::translate("Archive", "Couldn't open %1").arg(name), result.error, {});
                }
                return;
            }
            QDesktopServices::openUrl(QUrl::fromLocalFile(target));
        };
    });
}

void Archive::cancel()
{
    if (m_progress) {
        m_progress->cancel = true;
    }
    m_pending = nullptr;
}
