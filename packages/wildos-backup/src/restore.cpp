#include "restore.h"
#include "drives.h"
#include "keyring.h"
#include "paths.h"
#include "plan.h"
#include "restic.h"
#include "settings.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRandomGenerator>
#include <QThread>
#include <QUrl>

#include <algorithm>

namespace
{
constexpr int searchLimit = 500;

QString parentOf(const QString &path)
{
    const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
    return slash <= 0 ? QStringLiteral("/") : path.left(slash);
}

QString nameOf(const QString &path)
{
    return path.mid(path.lastIndexOf(QLatin1Char('/')) + 1);
}

QString commonFolder(const QStringList &paths)
{
    if (paths.isEmpty()) {
        return Paths::home();
    }
    if (paths.size() == 1) {
        return paths.first();
    }
    QStringList common = paths.first().split(QLatin1Char('/'));
    for (const QString &p : paths) {
        const QStringList parts = p.split(QLatin1Char('/'));
        qsizetype i = 0;
        while (i < common.size() && i < parts.size() && common.at(i) == parts.at(i)) {
            ++i;
        }
        common = common.mid(0, i);
    }
    const QString joined = common.join(QLatin1Char('/'));
    return joined.isEmpty() ? QStringLiteral("/") : joined;
}

QVariantMap entryFrom(const QJsonObject &node)
{
    return {
        {QStringLiteral("name"), node.value(QStringLiteral("name")).toString()},
        {QStringLiteral("path"), node.value(QStringLiteral("path")).toString()},
        {QStringLiteral("dir"), node.value(QStringLiteral("type")).toString() == QLatin1String("dir")},
        {QStringLiteral("size"), node.value(QStringLiteral("size")).toDouble()},
        {QStringLiteral("mtime"), QDateTime::fromString(node.value(QStringLiteral("mtime")).toString(), Qt::ISODateWithMs)},
    };
}

void sortEntries(QVariantList &entries)
{
    std::sort(entries.begin(), entries.end(), [](const QVariant &a, const QVariant &b) {
        const QVariantMap x = a.toMap();
        const QVariantMap y = b.toMap();
        const bool xd = x.value(QStringLiteral("dir")).toBool();
        const bool yd = y.value(QStringLiteral("dir")).toBool();
        if (xd != yd) {
            return xd;
        }
        return QString::localeAwareCompare(x.value(QStringLiteral("name")).toString(), y.value(QStringLiteral("name")).toString()) < 0;
    });
}

// "Report (restored 9 Nov).pdf", then "Report (restored 9 Nov) 2.pdf".
QString keepBothName(const QString &path, const QDateTime &when)
{
    const QFileInfo info(path);
    const QString folder = info.absolutePath();
    const bool dir = info.isDir();
    QString base = dir ? info.fileName() : info.completeBaseName();
    QString suffix = dir || info.suffix().isEmpty() ? QString() : QLatin1Char('.') + info.suffix();
    if (base.isEmpty()) {
        base = info.fileName();
        suffix.clear();
    }
    const QString date = QLocale().toString(when.date(), QStringLiteral("d MMM"));
    const QString stem = QCoreApplication::translate("Restore", "%1 (restored %2)").arg(base, date);
    QString candidate = folder + QLatin1Char('/') + stem + suffix;
    for (int n = 2; QFileInfo::exists(candidate) || QFileInfo(candidate).isSymLink(); ++n) {
        candidate = folder + QLatin1Char('/') + stem + QLatin1Char(' ') + QString::number(n) + suffix;
    }
    return candidate;
}

bool copyTree(const QString &from, const QString &to)
{
    const QFileInfo info(from);
    if (info.isSymLink()) {
        return QFile::link(info.symLinkTarget(), to);
    }
    if (!info.isDir()) {
        return QFile::copy(from, to);
    }
    if (!QDir().mkpath(to)) {
        return false;
    }
    bool ok = true;
    for (const QString &name : QDir(from).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot)) {
        ok = copyTree(from + QLatin1Char('/') + name, to + QLatin1Char('/') + name) && ok;
    }
    return ok;
}

bool removeTree(const QString &path)
{
    const QFileInfo info(path);
    if (info.isDir() && !info.isSymLink()) {
        return QDir(path).removeRecursively();
    }
    return QFile::remove(path);
}

// Into place by renaming, as both are on one filesystem; copying only if
// not. With `merge`, a folder going onto a folder goes in file by file,
// replacing what is there.
bool moveTree(const QString &from, const QString &to, bool merge)
{
    const QFileInfo source(from);
    const QFileInfo dest(to);
    if (merge && source.isDir() && !source.isSymLink() && dest.isDir() && !dest.isSymLink()) {
        bool ok = true;
        for (const QString &name : QDir(from).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot)) {
            ok = moveTree(from + QLatin1Char('/') + name, to + QLatin1Char('/') + name, true) && ok;
        }
        QDir().rmdir(from);
        return ok;
    }
    if (dest.exists() || dest.isSymLink()) {
        if (!merge || !removeTree(to)) {
            return false;
        }
    }
    QDir().mkpath(QFileInfo(to).absolutePath());
    if (QFile::rename(from, to) || QDir().rename(from, to)) {
        return true;
    }
    return copyTree(from, to) && removeTree(from);
}
}

Restore::Restore(QObject *parent)
    : QObject(parent)
{
}

Restore::~Restore()
{
    m_password.fill('\0');
    if (!m_stage.isEmpty()) {
        QDir(m_stage).removeRecursively();
    }
}

Restore *Restore::create(QQmlEngine *, QJSEngine *)
{
    auto *restore = new Restore(QCoreApplication::instance());
    QQmlEngine::setObjectOwnership(restore, QQmlEngine::CppOwnership);
    return restore;
}

void Restore::setState(const QString &state, const QString &error, const QString &detail)
{
    m_state = state;
    m_error = error;
    m_errorDetail = detail;
    Q_EMIT stateChanged();
}

Restic *Restore::newRestic()
{
    if (m_restic) {
        m_restic->disconnect(this);
        m_restic->interrupt();
        m_restic->deleteLater();
    }
    m_restic = new Restic(m_repo, m_password, this);
    m_restic->setCollect(true);
    return m_restic;
}

void Restore::open()
{
    if (m_state == QLatin1String("opening") || m_restoring) {
        return;
    }
    const QString uuid = Drives::instance()->pluggedIn();
    if (uuid.isEmpty()) {
        setState(QStringLiteral("noDrive"));
        return;
    }
    m_driveName = Settings::instance()->drive(uuid).value(QStringLiteral("name")).toString();
    setState(QStringLiteral("opening"));
    Drives::mountLater(this, uuid, [this](const QString &mountPoint, const QString &error, const DriveInfo &) {
        if (mountPoint.isEmpty()) {
            setState(QStringLiteral("failed"), tr("The drive couldn't be opened: %1").arg(error));
            return;
        }
        m_repo = mountPoint + QLatin1Char('/') + Paths::repoFolder();
        if (!QFileInfo::exists(m_repo + QStringLiteral("/config"))) {
            setState(QStringLiteral("failed"), tr("There are no backups on %1.").arg(m_driveName));
            return;
        }
        Keyring::read(this, [this](bool ok, const QByteArray &password, bool missing, const QString &why) {
            if (ok) {
                m_password = password;
                loadSnapshots();
            } else if (missing) {
                setState(QStringLiteral("needPassword"));
            } else {
                setState(QStringLiteral("failed"), tr("Your keyring couldn't be opened: %1").arg(why));
            }
        });
    });
}

void Restore::givePassword(const QString &password)
{
    const QByteArray typed = password.trimmed().toUtf8();
    if (typed.isEmpty() || m_repo.isEmpty()) {
        return;
    }
    m_password = typed;
    setState(QStringLiteral("opening"));
    loadSnapshots();
}

void Restore::loadSnapshots()
{
    Restic *restic = newRestic();
    connect(restic, &Restic::finished, this, [this, restic](int code) {
        if (code == Restic::WrongPassword) {
            m_password.fill('\0');
            m_password.clear();
            setState(QStringLiteral("needPassword"), tr("That password doesn't open the backups on this drive."));
            return;
        }
        if (code != Restic::Ok) {
            setState(QStringLiteral("failed"), Restic::explain(code, restic->errorText()), Restic::detail(code, restic->errorText()));
            return;
        }
        const QJsonArray list = QJsonDocument::fromJson(restic->output()).array();
        m_snapshots.clear();
        // restic lists oldest first.
        for (qsizetype i = list.size() - 1; i >= 0; --i) {
            const QJsonObject s = list.at(i).toObject();
            const QJsonObject summary = s.value(QStringLiteral("summary")).toObject();
            QStringList paths;
            for (const QJsonValue &p : s.value(QStringLiteral("paths")).toArray()) {
                paths << p.toString();
            }
            m_snapshots.append(QVariantMap{
                {QStringLiteral("id"), s.value(QStringLiteral("id")).toString()},
                {QStringLiteral("time"), QDateTime::fromString(s.value(QStringLiteral("time")).toString(), Qt::ISODateWithMs)},
                {QStringLiteral("dataAdded"), summary.value(QStringLiteral("data_added")).toDouble(-1)},
                {QStringLiteral("files"), summary.value(QStringLiteral("total_files_processed")).toDouble(-1)},
                {QStringLiteral("paths"), paths},
            });
        }
        Q_EMIT snapshotsChanged();
        setState(QStringLiteral("ready"));
        if (!m_snapshots.isEmpty()) {
            chooseSnapshot(m_snapshots.first().toMap().value(QStringLiteral("id")).toString());
        }
    });
    restic->start({QStringLiteral("snapshots"), QStringLiteral("--json"), QStringLiteral("--tag"), QStringLiteral("wildos-files")});
}

void Restore::chooseSnapshot(const QString &id)
{
    for (const QVariant &v : std::as_const(m_snapshots)) {
        const QVariantMap s = v.toMap();
        if (s.value(QStringLiteral("id")).toString() == id) {
            m_snapshot = id;
            m_snapshotTime = s.value(QStringLiteral("time")).toDateTime();
            m_root = commonFolder(s.value(QStringLiteral("paths")).toStringList());
            m_selected.clear();
            Q_EMIT selectionChanged();
            // The same folder in the other backup, if it was there.
            const bool keep = !m_folder.isEmpty() && (m_folder == m_root || m_folder.startsWith(m_root + QLatin1Char('/')));
            list(keep ? m_folder : m_root);
            return;
        }
    }
}

void Restore::openFolder(const QString &path)
{
    list(path);
}

void Restore::up()
{
    if (m_folder != m_root && m_folder.startsWith(m_root)) {
        list(parentOf(m_folder));
    }
}

void Restore::list(const QString &path)
{
    m_folder = path;
    m_searchText.clear();
    m_entries.clear();
    m_listing = true;
    Q_EMIT folderChanged();
    Restic *restic = newRestic();
    connect(restic, &Restic::message, this, [this, path](const QJsonObject &node) {
        const QString type = node.value(QStringLiteral("struct_type")).toString();
        const QString nodePath = node.value(QStringLiteral("path")).toString();
        if (type == QLatin1String("node") && nodePath != path && parentOf(nodePath) == path) {
            m_entries.append(entryFrom(node));
        }
    });
    connect(restic, &Restic::finished, this, [this, path](int code) {
        if (path != m_folder) {
            return;
        }
        m_listing = false;
        sortEntries(m_entries);
        if (code != Restic::Ok && m_entries.isEmpty()) {
            const QString said = m_restic ? m_restic->errorText() : QString();
            setState(QStringLiteral("failed"), Restic::explain(code, said), Restic::detail(code, said));
        }
        Q_EMIT folderChanged();
    });
    restic->setCollect(false);
    restic->start({QStringLiteral("ls"), QStringLiteral("--json"), m_snapshot, path});
}

void Restore::search(const QString &text)
{
    const QString t = text.trimmed();
    if (t.isEmpty()) {
        if (!m_searchText.isEmpty()) {
            list(m_folder);
        }
        return;
    }
    m_searchText = t;
    m_entries.clear();
    m_listing = true;
    Q_EMIT folderChanged();
    Restic *restic = newRestic();
    connect(restic, &Restic::finished, this, [this, restic, t](int code) {
        if (t != m_searchText) {
            return;
        }
        m_listing = false;
        if (code == Restic::Ok) {
            const QJsonArray results = QJsonDocument::fromJson(restic->output()).array();
            for (const QJsonValue &r : results) {
                for (const QJsonValue &m : r.toObject().value(QStringLiteral("matches")).toArray()) {
                    QJsonObject node = m.toObject();
                    if (!node.contains(QStringLiteral("name"))) {
                        node.insert(QStringLiteral("name"), nameOf(node.value(QStringLiteral("path")).toString()));
                    }
                    m_entries.append(entryFrom(node));
                    if (m_entries.size() >= searchLimit) {
                        break;
                    }
                }
            }
            sortEntries(m_entries);
        }
        Q_EMIT folderChanged();
    });
    restic->start({QStringLiteral("find"), QStringLiteral("--json"), QStringLiteral("--ignore-case"), QStringLiteral("--snapshot"), m_snapshot,
                   QStringLiteral("--"), QLatin1Char('*') + Plan::escape(t) + QLatin1Char('*')});
}

QStringList Restore::selected() const
{
    QStringList list = m_selected.values();
    list.sort();
    return list;
}

void Restore::toggle(const QString &path)
{
    if (!m_selected.remove(path)) {
        m_selected.insert(path);
    }
    Q_EMIT selectionChanged();
}

bool Restore::isSelected(const QString &path) const
{
    return m_selected.contains(path);
}

void Restore::clearSelection()
{
    m_selected.clear();
    Q_EMIT selectionChanged();
}

QString Restore::target(const QString &item, const QString &destination) const
{
    return destination.isEmpty() ? item : QDir::cleanPath(destination) + QLatin1Char('/') + nameOf(item);
}

int Restore::clashes(const QString &destination) const
{
    int n = 0;
    for (const QString &item : m_selected) {
        const QFileInfo info(target(item, destination));
        if (info.exists() || info.isSymLink()) {
            ++n;
        }
    }
    return n;
}

void Restore::restore(const QString &destination, bool replace)
{
    if (m_restoring || m_selected.isEmpty() || m_snapshot.isEmpty()) {
        return;
    }
    // A picked folder already brings everything picked inside it.
    QStringList items = selected();
    items.erase(std::remove_if(items.begin(), items.end(),
                               [&items](const QString &p) {
                                   return std::any_of(items.cbegin(), items.cend(), [&p](const QString &q) {
                                       return p != q && p.startsWith(q + QLatin1Char('/'));
                                   });
                               }),
                items.end());

    // Hidden, beside where things go, so moving them in is a rename.
    const QString base = destination.isEmpty() ? Paths::home() : QDir::cleanPath(destination);
    m_stage = base + QStringLiteral("/.wildos-backup-restore-") + QString::number(QRandomGenerator::global()->generate(), 36);
    if (!QDir().mkpath(m_stage)) {
        Q_EMIT restoreFailed(tr("Couldn't write to %1.").arg(base), QString());
        return;
    }
    m_restoring = true;
    m_restorePercent = -1;
    Q_EMIT restoreChanged();

    QStringList args{QStringLiteral("restore"), QStringLiteral("--json"), m_snapshot, QStringLiteral("--target"), m_stage};
    for (const QString &item : std::as_const(items)) {
        args << QStringLiteral("--include") << Plan::escape(item);
    }
    auto *restic = new Restic(m_repo, m_password, this);
    m_restoreRun = restic;
    connect(restic, &Restic::message, this, [this](const QJsonObject &message) {
        if (message.value(QStringLiteral("message_type")).toString() == QLatin1String("status")) {
            m_restorePercent = message.value(QStringLiteral("percent_done")).toDouble(-1);
            Q_EMIT restoreChanged();
        }
    });
    connect(restic, &Restic::finished, this, [this, restic, items, destination, replace](int code) {
        restic->deleteLater();
        if (code != Restic::Ok) {
            QDir(m_stage).removeRecursively();
            m_stage.clear();
            m_restoring = false;
            Q_EMIT restoreChanged();
            if (code != Restic::Interrupted) {
                Q_EMIT restoreFailed(Restic::explain(code, restic->errorText()), Restic::detail(code, restic->errorText()));
            }
            return;
        }
        moveIntoPlace(m_stage, items, destination, replace);
    });
    restic->start(args);
}

void Restore::moveIntoPlace(const QString &stage, const QStringList &items, const QString &destination, bool replace)
{
    QList<QPair<QString, QString>> moves;
    for (const QString &item : items) {
        QString to = target(item, destination);
        const QFileInfo existing(to);
        if (!replace && (existing.exists() || existing.isSymLink())) {
            to = keepBothName(to, m_snapshotTime);
        }
        moves.append({stage + item, to});
    }
    const QString where = destination.isEmpty() ? parentOf(items.value(0)) : QDir::cleanPath(destination);
    QPointer<Restore> self(this);
    QThread *thread = QThread::create([self, stage, moves, replace, where] {
        int count = 0;
        for (const auto &[from, to] : moves) {
            if ((QFileInfo::exists(from) || QFileInfo(from).isSymLink()) && moveTree(from, to, replace)) {
                ++count;
            }
        }
        QDir(stage).removeRecursively();
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, count, where, total = moves.size()] {
            if (!self) {
                return;
            }
            self->m_stage.clear();
            self->m_restoring = false;
            self->m_selected.clear();
            Q_EMIT self->selectionChanged();
            Q_EMIT self->restoreChanged();
            if (count == total) {
                Q_EMIT self->restored(count, where);
            } else {
                Q_EMIT self->restoreFailed(tr("%1 of %2 couldn't be put back.").arg(total - count).arg(total), QString());
            }
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void Restore::cancelRestore()
{
    if (m_restoreRun) {
        m_restoreRun->interrupt();
    }
}

void Restore::showFolder(const QString &path)
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}
