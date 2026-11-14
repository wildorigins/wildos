#include "flatpakbackend.h"
#include "timing.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QMutex>
#include <QStandardPaths>

#include <flatpak.h>

namespace
{
const char *const remoteName = "flathub";

// Adding the Flathub remote must happen once, whichever thread gets there
// first; the others then find it on disk.
QMutex installationMutex;

QString takeError(GError *&error, const QString &fallback = {})
{
    QString text = fallback;
    if (error) {
        if (text.isEmpty()) {
            text = QString::fromUtf8(error->message);
        }
        g_clear_error(&error);
    }
    return text;
}

// The platforms apps are built on, as people are told about them: "the KDE
// base". Anything else a transaction pulls in (graphics drivers, codecs,
// translations) is counted but not named.
QString platformLabel(const QString &name)
{
    if (name == QLatin1String("org.kde.Platform")) {
        return QStringLiteral("the KDE base");
    }
    if (name == QLatin1String("org.gnome.Platform")) {
        return QStringLiteral("the GNOME base");
    }
    if (name == QLatin1String("org.freedesktop.Platform")) {
        return QStringLiteral("the Freedesktop base");
    }
    if (name == QLatin1String("io.elementary.Platform")) {
        return QStringLiteral("the elementary base");
    }
    if (name.endsWith(QLatin1String(".Platform"))) {
        return QStringLiteral("a shared base");
    }
    return {};
}

// libflatpak's messages are written for the command line; the line under an
// app's button says what happened in plain words. The original goes to the
// log.
QString plainError(const QString &message)
{
    const QString m = message.toLower();
    if (m.contains(QLatin1String("resolve")) || m.contains(QLatin1String("connect")) || m.contains(QLatin1String("network"))
        || m.contains(QLatin1String("timeout")) || m.contains(QLatin1String("timed out")) || m.contains(QLatin1String("curl"))) {
        return QStringLiteral("Couldn't reach Flathub. Check you're online.");
    }
    if (m.contains(QLatin1String("space"))) {
        return QStringLiteral("There isn't enough free space for this app.");
    }
    if (m.contains(QLatin1String("not found")) || m.contains(QLatin1String("nothing matches"))) {
        return QStringLiteral("Flathub doesn't have this app any more.");
    }
    return QStringLiteral("Flathub stopped part way through. Try again in a few minutes.");
}

// The same for an app installed from a file, which may come from anywhere.
QString plainFileError(const QString &message)
{
    const QString m = message.toLower();
    if (m.contains(QLatin1String("resolve")) || m.contains(QLatin1String("connect")) || m.contains(QLatin1String("network"))
        || m.contains(QLatin1String("timeout")) || m.contains(QLatin1String("timed out")) || m.contains(QLatin1String("curl"))) {
        return QStringLiteral("Couldn't reach the place this app downloads from. Check you're online.");
    }
    if (m.contains(QLatin1String("space"))) {
        return QStringLiteral("There isn't enough free space for this app.");
    }
    if (m.contains(QLatin1String("gpg")) || m.contains(QLatin1String("signature")) || m.contains(QLatin1String("signed"))) {
        return QStringLiteral("The app's signature didn't match, so nothing was installed. The file may be damaged or changed.");
    }
    if (m.contains(QLatin1String("runtime")) || m.contains(QLatin1String("not found")) || m.contains(QLatin1String("nothing matches"))) {
        return QStringLiteral("This app needs a part Wild OS can't find on Flathub, so it can't be installed.");
    }
    if (m.contains(QLatin1String("invalid")) || m.contains(QLatin1String("not a")) || m.contains(QLatin1String("parse"))) {
        return QStringLiteral("This file can't be read. It may be damaged or only partly downloaded.");
    }
    return QStringLiteral("The install stopped part way through. Try again, or download the file again.");
}

QString refName(const char *ref)
{
    return QString::fromUtf8(ref).section(QLatin1Char('/'), 1, 1);
}

bool isRuntimeRef(const char *ref)
{
    return qstrncmp(ref, "runtime/", 8) == 0;
}

struct OpInfo {
    qint64 weight = 1;
    qint64 bytes = 0;
    QString detail;
};

struct TransactionState {
    FlatpakWorker *worker;
    QList<int> jobs;
    bool batch = false;
    QHash<FlatpakTransactionOperation *, OpInfo> ops;
    qint64 totalWeight = 0;
    qint64 doneWeight = 0;
    qint64 totalBytes = 0;
    qint64 doneBytes = 0;
    FlatpakTransactionOperation *current = nullptr;
    bool removing = false;
    QString error;
    QHash<QString, QString> refErrors;
};

void onProgressChanged(FlatpakTransactionProgress *progress, gpointer data)
{
    auto state = static_cast<TransactionState *>(data);
    const int part = qBound(0, flatpak_transaction_progress_get_progress(progress), 100);
    const OpInfo info = state->ops.value(state->current);
    // Weighted by what each step downloads, so a large runtime moves the bar
    // as much as it costs and a small translation barely does.
    int whole = 0;
    if (state->totalWeight > 0) {
        whole = int((state->doneWeight + info.weight * part / 100) * 100 / state->totalWeight);
    }
    whole = qBound(0, whole, 100);
    const bool estimating = flatpak_transaction_progress_get_is_estimating(progress);
    QString status;
    if (state->removing) {
        status = QStringLiteral("remove");
    } else {
        // A step at 100% is being unpacked into place.
        status = estimating ? QStringLiteral("prepare") : part >= 100 ? QStringLiteral("install") : QStringLiteral("download");
    }
    const qint64 done = state->doneBytes + info.bytes * part / 100;
    for (int job : std::as_const(state->jobs)) {
        Q_EMIT state->worker->progress(job, whole, status, info.detail, done, state->totalBytes);
    }
}

void onNewOperation(FlatpakTransaction *, FlatpakTransactionOperation *operation, FlatpakTransactionProgress *progress, gpointer data)
{
    auto state = static_cast<TransactionState *>(data);
    if (state->current) {
        state->doneWeight += state->ops.value(state->current).weight;
        state->doneBytes += state->ops.value(state->current).bytes;
    }
    state->current = operation;
    state->removing = flatpak_transaction_operation_get_operation_type(operation) == FLATPAK_TRANSACTION_OPERATION_UNINSTALL;
    flatpak_transaction_progress_set_update_frequency(progress, 250);
    g_signal_connect(progress, "changed", G_CALLBACK(onProgressChanged), data);
    onProgressChanged(progress, data);
}

gboolean onReady(FlatpakTransaction *transaction, gpointer data)
{
    auto state = static_cast<TransactionState *>(data);
    GList *ops = flatpak_transaction_get_operations(transaction);
    for (GList *l = ops; l; l = l->next) {
        auto op = static_cast<FlatpakTransactionOperation *>(l->data);
        OpInfo info;
        const auto type = flatpak_transaction_operation_get_operation_type(op);
        if (type != FLATPAK_TRANSACTION_OPERATION_UNINSTALL && !flatpak_transaction_operation_get_is_skipped(op)) {
            info.bytes = qint64(flatpak_transaction_operation_get_download_size(op));
            // A floor, so steps that report no size still move the bar.
            info.weight = qMax<qint64>(info.bytes, 256 * 1024);
        }
        const char *ref = flatpak_transaction_operation_get_ref(op);
        if (type == FLATPAK_TRANSACTION_OPERATION_INSTALL && isRuntimeRef(ref)) {
            info.detail = platformLabel(refName(ref));
        }
        state->totalWeight += info.weight;
        state->totalBytes += info.bytes;
        state->ops.insert(op, info);
    }
    g_list_free_full(ops, g_object_unref);
    return TRUE;
}

gboolean onOperationError(FlatpakTransaction *, FlatpakTransactionOperation *operation, GError *error, gint details, gpointer data)
{
    auto state = static_cast<TransactionState *>(data);
    // A related ref that fails (a translation, say) should not undo the app.
    if (details & FLATPAK_TRANSACTION_ERROR_DETAILS_NON_FATAL) {
        return TRUE;
    }
    const QString message = error ? QString::fromUtf8(error->message) : QString();
    if (!message.isEmpty() && state->error.isEmpty()) {
        state->error = message;
    }
    if (operation) {
        state->refErrors.insert(QString::fromUtf8(flatpak_transaction_operation_get_ref(operation)), message);
    }
    // In a batch the other apps carry on; whatever needed the failed step is
    // skipped by libflatpak and reported here too.
    return state->batch ? TRUE : FALSE;
}

gboolean onAddNewRemote(FlatpakTransaction *, FlatpakTransactionRemoteReason, const char *, const char *, const char *, gpointer)
{
    // Apps only ever come from Flathub; never add a remote an app asks for.
    return FALSE;
}

int onChooseRemote(FlatpakTransaction *, const char *, const char *, const char *const *remotes, gpointer)
{
    for (int i = 0; remotes && remotes[i]; ++i) {
        if (qstrcmp(remotes[i], remoteName) == 0) {
            return i;
        }
    }
    return 0;
}

struct PlanState {
    QString appId;
    bool resolved = false;
    FlatpakPlan plan;
    QString metadata;
    qint64 runtimeBytes = 0;
};

gboolean onPlanReady(FlatpakTransaction *transaction, gpointer data)
{
    auto state = static_cast<PlanState *>(data);
    state->resolved = true;
    state->plan.total = 0;
    state->plan.shared = 0;
    state->plan.installed = 0;
    GList *ops = flatpak_transaction_get_operations(transaction);
    for (GList *l = ops; l; l = l->next) {
        auto op = static_cast<FlatpakTransactionOperation *>(l->data);
        if (flatpak_transaction_operation_get_is_skipped(op)) {
            continue;
        }
        const qint64 size = qint64(flatpak_transaction_operation_get_download_size(op));
        const char *ref = flatpak_transaction_operation_get_ref(op);
        state->plan.total += size;
        state->plan.installed += qint64(flatpak_transaction_operation_get_installed_size(op));
        if (!isRuntimeRef(ref) && refName(ref) == state->appId) {
            GKeyFile *meta = flatpak_transaction_operation_get_metadata(op);
            if (meta) {
                gsize length = 0;
                char *text = g_key_file_to_data(meta, &length, nullptr);
                state->metadata = QString::fromUtf8(text, qsizetype(length));
                g_free(text);
            }
            continue;
        }
        state->plan.shared += size;
        const QString label = platformLabel(refName(ref));
        if (flatpak_transaction_operation_get_operation_type(op) == FLATPAK_TRANSACTION_OPERATION_INSTALL && !label.isEmpty() && size > state->runtimeBytes) {
            state->plan.runtime = label;
            state->runtimeBytes = size;
        }
    }
    g_list_free_full(ops, g_object_unref);
    // Resolved is all this was for; stop before anything downloads.
    return FALSE;
}
}

FlatpakBackend::FlatpakBackend(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<FlatpakPlan>();
    qRegisterMetaType<FlatpakBatch>();

    m_catalogue = new FlatpakWorker("catalogue");
    m_details = new FlatpakWorker("details");
    m_installer = new FlatpakWorker("install");
    const QList<QPair<FlatpakWorker *, QThread *>> pairs = {
        {m_catalogue, &m_catalogueThread},
        {m_details, &m_detailsThread},
        {m_installer, &m_installThread},
    };
    for (const auto &[worker, thread] : pairs) {
        worker->moveToThread(thread);
        connect(thread, &QThread::finished, worker, &QObject::deleteLater);
        connect(worker, &FlatpakWorker::unavailable, this, &FlatpakBackend::unavailable);
        connect(worker, &FlatpakWorker::installedListed, this,
                [this](const QStringList &apps, const QHash<QString, qint64> &sizes, const QHash<QString, qint64> &updated) {
            m_installed = QSet<QString>(apps.begin(), apps.end());
            m_sizes = sizes;
            m_updated = updated;
            m_ready = true;
            Q_EMIT installedChanged();
        });
    }

    connect(this, &FlatpakBackend::requestStart, m_catalogue, &FlatpakWorker::start);
    connect(this, &FlatpakBackend::requestCheck, m_catalogue, &FlatpakWorker::check);
    connect(this, &FlatpakBackend::requestUpdateCheck, m_catalogue, &FlatpakWorker::checkUpdates);
    connect(this, &FlatpakBackend::requestDetails, m_details, &FlatpakWorker::details);
    connect(this, &FlatpakBackend::requestInstall, m_installer, &FlatpakWorker::install);
    connect(this, &FlatpakBackend::requestUninstall, m_installer, &FlatpakWorker::uninstall);
    connect(this, &FlatpakBackend::requestUpdate, m_installer, &FlatpakWorker::update);
    connect(this, &FlatpakBackend::requestInstallFile, m_installer, &FlatpakWorker::installFile);

    connect(m_catalogue, &FlatpakWorker::appstreamReady, this, [this](const QString &dir) {
        m_appstreamDir = dir;
        Q_EMIT appstreamChanged();
    });
    connect(m_catalogue, &FlatpakWorker::updatesListed, this, [this](const QStringList &updates, const QHash<QString, qint64> &sizes) {
        m_updateCheckQueued = false;
        const QSet<QString> fresh(updates.begin(), updates.end());
        if (fresh != m_updates || sizes != m_updateSizes) {
            m_updates = fresh;
            m_updateSizes = sizes;
            Q_EMIT updatesChanged();
        }
    });
    connect(m_catalogue, &FlatpakWorker::checked, this, &FlatpakBackend::refreshFinished);
    connect(m_installer, &FlatpakWorker::progress, this, &FlatpakBackend::progress);
    connect(m_installer, &FlatpakWorker::finished, this, [this](int job, bool ok, const QString &error) {
        if (!ok && error != QLatin1String(cancelledMarker)) {
            qCWarning(lcStore) << "Flathub job" << job << "failed:" << error;
        }
        const bool plain = ok || error == QLatin1String(cancelledMarker);
        Q_EMIT finished(job, ok, plain ? error : job < 0 ? plainFileError(error) : plainError(error));
        checkUpdatesSoon();
    });
    connect(m_details, &FlatpakWorker::metadataReady, this, &FlatpakBackend::metadataReady);
    connect(m_details, &FlatpakWorker::planReady, this, &FlatpakBackend::planReady);

    m_catalogueThread.setObjectName(QStringLiteral("flatpak-catalogue"));
    m_detailsThread.setObjectName(QStringLiteral("flatpak-details"));
    m_installThread.setObjectName(QStringLiteral("flatpak-install"));
    m_catalogueThread.start();
    m_detailsThread.start();
    m_installThread.start();
}

FlatpakBackend::~FlatpakBackend()
{
    for (QThread *thread : {&m_catalogueThread, &m_detailsThread, &m_installThread}) {
        thread->quit();
    }
    for (QThread *thread : {&m_catalogueThread, &m_detailsThread, &m_installThread}) {
        thread->wait(3000);
    }
}

QString FlatpakBackend::userDataDir()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).filePath(QStringLiteral("flatpak"));
}

void FlatpakBackend::seedInstalled(const QSet<QString> &apps)
{
    if (!m_ready) {
        m_installed = apps;
    }
}

void FlatpakBackend::start()
{
    Q_EMIT requestStart();
}

void FlatpakBackend::install(const FlatpakBatch &jobs)
{
    Q_EMIT requestInstall(jobs);
}

void FlatpakBackend::uninstall(int job, const QString &ref)
{
    Q_EMIT requestUninstall(job, ref);
}

void FlatpakBackend::update(int job, const QStringList &refs)
{
    Q_EMIT requestUpdate(job, refs);
}

void FlatpakBackend::cancel(int job)
{
    m_installer->cancel(job);
}

void FlatpakBackend::installFile(int job, const QString &path, bool bundle)
{
    Q_EMIT requestInstallFile(job, path, bundle);
}

void FlatpakBackend::fetchDetails(const QString &appId, const QString &ref)
{
    Q_EMIT requestDetails(appId, ref);
}

void FlatpakBackend::refreshAppstream()
{
    Q_EMIT requestCheck();
}

void FlatpakBackend::checkUpdatesSoon()
{
    // The update check asks Flathub, so it runs on the catalogue thread after
    // the install has been reported, and several finishing together share one.
    if (m_updateCheckQueued) {
        return;
    }
    m_updateCheckQueued = true;
    Q_EMIT requestUpdateCheck();
}

FlatpakWorker::FlatpakWorker(const char *role)
    : m_role(role)
{
}

FlatpakWorker::~FlatpakWorker()
{
    g_clear_object(&m_installation);
}

void FlatpakWorker::cancel(int job)
{
    QMutexLocker lock(&m_runningMutex);
    if (m_cancellable && m_runningJobs.contains(job)) {
        g_cancellable_cancel(m_cancellable);
    }
}

bool FlatpakWorker::ensureInstallation()
{
    if (m_installation) {
        return true;
    }
    QMutexLocker lock(&installationMutex);
    GError *error = nullptr;
    m_installation = flatpak_installation_new_user(nullptr, &error);
    if (!m_installation) {
        Q_EMIT unavailable(takeError(error));
        return false;
    }

    FlatpakRemote *remote = flatpak_installation_get_remote_by_name(m_installation, remoteName, nullptr, &error);
    if (remote) {
        // Someone may have switched it off with the command line; the Store
        // cannot work without it.
        if (flatpak_remote_get_disabled(remote)) {
            flatpak_remote_set_disabled(remote, FALSE);
            flatpak_installation_modify_remote(m_installation, remote, nullptr, nullptr);
        }
        g_object_unref(remote);
        return true;
    }
    g_clear_error(&error);

    QFile file(QStringLiteral(":/qt/qml/WildOS/Store/data/flathub.flatpakrepo"));
    if (!file.open(QIODevice::ReadOnly)) {
        g_clear_object(&m_installation);
        Q_EMIT unavailable(QStringLiteral("The Flathub details are missing from the Store."));
        return false;
    }
    const QByteArray data = file.readAll();
    GBytes *bytes = g_bytes_new(data.constData(), data.size());
    remote = flatpak_remote_new_from_file(remoteName, bytes, &error);
    g_bytes_unref(bytes);
    if (!remote) {
        g_clear_object(&m_installation);
        Q_EMIT unavailable(takeError(error));
        return false;
    }
    flatpak_remote_set_gpg_verify(remote, TRUE);
    const bool added = flatpak_installation_add_remote(m_installation, remote, TRUE, nullptr, &error);
    g_object_unref(remote);
    if (!added) {
        g_clear_object(&m_installation);
        Q_EMIT unavailable(takeError(error));
        return false;
    }
    flatpak_installation_drop_caches(m_installation, nullptr, nullptr);
    return true;
}

void FlatpakWorker::start()
{
    if (!ensureInstallation()) {
        return;
    }
    qCInfo(lcStore) << "timing: Flathub installation open at" << storeClock() << "ms";
    // Everything on disk first, so the Store fills before Flathub is asked
    // anything.
    listInstalled();
    qCInfo(lcStore) << "timing: Flathub apps listed at" << storeClock() << "ms";
    appstream(false);
    checkUpdates();
}

bool FlatpakWorker::appstream(bool force)
{
    if (!ensureInstallation()) {
        return false;
    }
    GError *error = nullptr;
    FlatpakRemote *remote = flatpak_installation_get_remote_by_name(m_installation, remoteName, nullptr, &error);
    if (!remote) {
        takeError(error);
        return false;
    }
    GFile *dirFile = flatpak_remote_get_appstream_dir(remote, nullptr);
    GFile *stampFile = flatpak_remote_get_appstream_timestamp(remote, nullptr);
    char *dirPath = g_file_get_path(dirFile);
    char *stampPath = g_file_get_path(stampFile);
    const QString dir = QString::fromUtf8(dirPath);
    const QString stamp = QString::fromUtf8(stampPath);
    g_free(dirPath);
    g_free(stampPath);
    g_object_unref(dirFile);
    g_object_unref(stampFile);
    g_object_unref(remote);

    const bool present = QFileInfo::exists(dir + QStringLiteral("/appstream.xml.gz")) || QFileInfo::exists(dir + QStringLiteral("/appstream.xml"));
    if (present) {
        Q_EMIT appstreamReady(dir);
    }

    // Flathub's catalogue changes many times a day; once a day is plenty for
    // browsing, and installs always fetch the current version anyway.
    const QFileInfo stampInfo(stamp);
    const bool stale = !stampInfo.exists() || stampInfo.lastModified().secsTo(QDateTime::currentDateTime()) > 24 * 3600;
    if (!force && present && !stale) {
        return true;
    }
    gboolean changed = FALSE;
    const qint64 began = storeClock();
    if (!flatpak_installation_update_appstream_full_sync(m_installation, remoteName, nullptr, nullptr, nullptr, &changed, nullptr, &error)) {
        const QString message = takeError(error);
        if (!present) {
            Q_EMIT unavailable(message);
        }
        return false;
    }
    qCInfo(lcStore) << "timing: Flathub catalogue download took" << storeClock() - began << "ms, changed" << bool(changed);
    if (changed || !present) {
        Q_EMIT appstreamReady(dir);
    }
    return true;
}

void FlatpakWorker::check()
{
    const bool ok = appstream(true);
    checkUpdates();
    Q_EMIT checked(ok);
}

void FlatpakWorker::listInstalled()
{
    // Only what is on disk: this runs after every transaction, and nothing
    // here may wait on the network.
    GError *error = nullptr;
    QStringList apps;
    QHash<QString, qint64> sizes;
    QHash<QString, qint64> updated;
    GPtrArray *refs = flatpak_installation_list_installed_refs_by_kind(m_installation, FLATPAK_REF_KIND_APP, nullptr, &error);
    if (refs) {
        for (guint i = 0; i < refs->len; ++i) {
            auto ref = static_cast<FlatpakInstalledRef *>(g_ptr_array_index(refs, i));
            const QString name = QString::fromUtf8(flatpak_ref_get_name(FLATPAK_REF(ref)));
            apps << name;
            sizes.insert(name, qint64(flatpak_installed_ref_get_installed_size(ref)));
            // A deploy folder is made for each version, so its time is when
            // this version arrived.
            const QFileInfo deployed(QString::fromUtf8(flatpak_installed_ref_get_deploy_dir(ref)));
            updated.insert(name, deployed.lastModified().toMSecsSinceEpoch());
        }
        g_ptr_array_unref(refs);
    }
    takeError(error);
    Q_EMIT installedListed(apps, sizes, updated);
}

namespace
{
struct SizesState {
    QHash<QString, qint64> sizes;
};

gboolean onSizesReady(FlatpakTransaction *transaction, gpointer data)
{
    auto state = static_cast<SizesState *>(data);
    GList *ops = flatpak_transaction_get_operations(transaction);
    for (GList *l = ops; l; l = l->next) {
        auto op = static_cast<FlatpakTransactionOperation *>(l->data);
        const char *ref = flatpak_transaction_operation_get_ref(op);
        if (!flatpak_transaction_operation_get_is_skipped(op) && !isRuntimeRef(ref)) {
            state->sizes.insert(refName(ref), qint64(flatpak_transaction_operation_get_download_size(op)));
        }
    }
    g_list_free_full(ops, g_object_unref);
    return FALSE;
}
}

QHash<QString, qint64> FlatpakWorker::updateSizes(const QStringList &refs)
{
    // Resolved and stopped before it downloads, like the install plans.
    SizesState state;
    if (refs.isEmpty()) {
        return state.sizes;
    }
    GMainContext *context = g_main_context_new();
    g_main_context_push_thread_default(context);
    GError *error = nullptr;
    FlatpakTransaction *transaction = flatpak_transaction_new_for_installation(m_installation, nullptr, &error);
    if (transaction) {
        flatpak_transaction_set_no_interaction(transaction, TRUE);
        for (const QString &ref : refs) {
            flatpak_transaction_add_update(transaction, ref.toUtf8().constData(), nullptr, nullptr, nullptr);
        }
        g_signal_connect(transaction, "ready", G_CALLBACK(onSizesReady), &state);
        flatpak_transaction_run(transaction, nullptr, nullptr);
        g_object_unref(transaction);
    }
    g_clear_error(&error);
    g_main_context_pop_thread_default(context);
    g_main_context_unref(context);
    return state.sizes;
}

void FlatpakWorker::checkUpdates()
{
    if (!ensureInstallation()) {
        return;
    }
    const qint64 began = storeClock();
    GError *error = nullptr;
    QStringList updates;
    QStringList full;
    // Asks Flathub for the current summary, so it never runs where something
    // waits on it.
    GPtrArray *refs = flatpak_installation_list_installed_refs_for_update(m_installation, nullptr, &error);
    if (refs) {
        for (guint i = 0; i < refs->len; ++i) {
            auto ref = static_cast<FlatpakInstalledRef *>(g_ptr_array_index(refs, i));
            if (flatpak_ref_get_kind(FLATPAK_REF(ref)) == FLATPAK_REF_KIND_APP) {
                updates << QString::fromUtf8(flatpak_ref_get_name(FLATPAK_REF(ref)));
                char *formatted = flatpak_ref_format_ref(FLATPAK_REF(ref));
                full << QString::fromUtf8(formatted);
                g_free(formatted);
            }
        }
        g_ptr_array_unref(refs);
    }
    takeError(error);
    const QHash<QString, qint64> sizes = updateSizes(full);
    qCInfo(lcStore) << "timing: Flathub update check took" << storeClock() - began << "ms";
    Q_EMIT updatesListed(updates, sizes);
}

void FlatpakWorker::install(const FlatpakBatch &jobs)
{
    QList<int> ids;
    QStringList refs;
    for (const auto &[job, ref] : jobs) {
        ids << job;
        refs << ref;
    }
    runTransaction(ids, Op::Install, refs);
}

void FlatpakWorker::uninstall(int job, const QString &ref)
{
    runTransaction({job}, Op::Uninstall, {ref});
}

void FlatpakWorker::update(int job, const QStringList &refs)
{
    runTransaction({job}, Op::Update, refs);
}

void FlatpakWorker::installFile(int job, const QString &path, bool bundle)
{
    runTransaction({job}, bundle ? Op::InstallBundle : Op::InstallRefFile, {path});
}

void FlatpakWorker::runTransaction(const QList<int> &jobs, Op op, const QStringList &refs)
{
    if (!ensureInstallation()) {
        for (int job : jobs) {
            Q_EMIT finished(job, false, QStringLiteral("Flathub is not available right now."));
        }
        return;
    }

    // The transaction iterates the thread's default context while it
    // downloads, which is what delivers the progress signals here.
    GMainContext *context = g_main_context_new();
    g_main_context_push_thread_default(context);

    GError *error = nullptr;
    FlatpakTransaction *transaction = flatpak_transaction_new_for_installation(m_installation, nullptr, &error);
    QString failure;
    TransactionState state{this, jobs};
    state.batch = jobs.size() > 1;
    if (!transaction) {
        failure = takeError(error);
    } else {
        flatpak_transaction_set_no_interaction(transaction, TRUE);
        // Runtimes already in the system installation are used from there.
        flatpak_transaction_add_default_dependency_sources(transaction);
        if (op == Op::Uninstall) {
            // Runtimes nothing else uses go with the app, as Windows would.
            flatpak_transaction_set_include_unused_uninstall_ops(transaction, TRUE);
        }

        QStringList targets = refs;
        if (op == Op::Update && targets.isEmpty()) {
            GPtrArray *pending = flatpak_installation_list_installed_refs_for_update(m_installation, nullptr, &error);
            if (pending) {
                for (guint i = 0; i < pending->len; ++i) {
                    char *full = flatpak_ref_format_ref(FLATPAK_REF(g_ptr_array_index(pending, i)));
                    targets << QString::fromUtf8(full);
                    g_free(full);
                }
                g_ptr_array_unref(pending);
            }
            takeError(error);
        }

        int added = 0;
        for (const QString &ref : std::as_const(targets)) {
            const QByteArray r = ref.toUtf8();
            bool ok = false;
            switch (op) {
            case Op::Install:
                ok = flatpak_transaction_add_install(transaction, remoteName, r.constData(), nullptr, &error);
                break;
            case Op::Uninstall:
                ok = flatpak_transaction_add_uninstall(transaction, r.constData(), &error);
                break;
            case Op::Update:
                ok = flatpak_transaction_add_update(transaction, r.constData(), nullptr, nullptr, &error);
                break;
            case Op::InstallRefFile: {
                // The file names its own repository. libflatpak adds it as
                // the app's origin, for this app alone; onAddNewRemote still
                // refuses to add it as a source of other apps.
                QFile file(ref);
                if (!file.open(QIODevice::ReadOnly)) {
                    g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "The file can't be read.");
                    break;
                }
                const QByteArray data = file.readAll();
                GBytes *bytes = g_bytes_new(data.constData(), gsize(data.size()));
                ok = flatpak_transaction_add_install_flatpakref(transaction, bytes, &error);
                g_bytes_unref(bytes);
                break;
            }
            case Op::InstallBundle: {
                GFile *file = g_file_new_for_path(r.constData());
                ok = flatpak_transaction_add_install_bundle(transaction, file, nullptr, &error);
                g_object_unref(file);
                break;
            }
            }
            if (ok) {
                ++added;
                continue;
            }
            if (error && g_error_matches(error, FLATPAK_ERROR, FLATPAK_ERROR_ALREADY_INSTALLED)) {
                g_clear_error(&error);
                continue;
            }
            if (state.batch) {
                state.refErrors.insert(ref, takeError(error));
                continue;
            }
            failure = takeError(error);
            break;
        }

        // A .flatpakref only becomes operations inside flatpak_transaction_run,
        // so its transaction looks empty until then and must run anyway.
        if (failure.isEmpty() && added > 0 && (op == Op::InstallRefFile || !flatpak_transaction_is_empty(transaction))) {
            g_signal_connect(transaction, "new-operation", G_CALLBACK(onNewOperation), &state);
            g_signal_connect(transaction, "ready", G_CALLBACK(onReady), &state);
            g_signal_connect(transaction, "operation-error", G_CALLBACK(onOperationError), &state);
            g_signal_connect(transaction, "add-new-remote", G_CALLBACK(onAddNewRemote), &state);
            g_signal_connect(transaction, "choose-remote-for-ref", G_CALLBACK(onChooseRemote), &state);
            GCancellable *cancellable = g_cancellable_new();
            {
                QMutexLocker lock(&m_runningMutex);
                m_runningJobs = jobs;
                m_cancellable = cancellable;
            }
            if (!flatpak_transaction_run(transaction, cancellable, &error)) {
                if (g_cancellable_is_cancelled(cancellable)) {
                    failure = QString::fromLatin1(cancelledMarker);
                } else if (g_error_matches(error, FLATPAK_ERROR, FLATPAK_ERROR_ALREADY_INSTALLED)) {
                    // A .flatpakref is only checked against what is installed
                    // once it runs; its app being there already is no failure.
                } else {
                    failure = state.error.isEmpty() ? takeError(error) : state.error;
                    if (failure.isEmpty()) {
                        failure = QStringLiteral("Flathub stopped part way through.");
                    }
                }
                g_clear_error(&error);
            }
            {
                QMutexLocker lock(&m_runningMutex);
                m_runningJobs.clear();
                m_cancellable = nullptr;
            }
            g_object_unref(cancellable);
        }
        g_object_unref(transaction);
    }

    g_main_context_pop_thread_default(context);
    g_main_context_unref(context);

    flatpak_installation_drop_caches(m_installation, nullptr, nullptr);
    listInstalled();

    if (op != Op::Install || !state.batch) {
        Q_EMIT finished(jobs.value(0), failure.isEmpty(), failure);
        return;
    }
    // A batch reports each app by whether it is now installed, since one
    // app failing leaves the others in place.
    QSet<QString> installedNow;
    GPtrArray *apps = flatpak_installation_list_installed_refs_by_kind(m_installation, FLATPAK_REF_KIND_APP, nullptr, nullptr);
    if (apps) {
        for (guint i = 0; i < apps->len; ++i) {
            installedNow.insert(QString::fromUtf8(flatpak_ref_get_name(FLATPAK_REF(g_ptr_array_index(apps, i)))));
        }
        g_ptr_array_unref(apps);
    }
    for (int i = 0; i < jobs.size(); ++i) {
        const QString ref = refs.value(i);
        if (installedNow.contains(ref.section(QLatin1Char('/'), 1, 1))) {
            Q_EMIT finished(jobs.at(i), true, QString());
            continue;
        }
        QString message = failure == QLatin1String(cancelledMarker) ? failure : state.refErrors.value(ref);
        if (message.isEmpty()) {
            message = failure.isEmpty() ? state.error : failure;
        }
        if (message.isEmpty()) {
            message = QStringLiteral("Flathub stopped part way through.");
        }
        Q_EMIT finished(jobs.at(i), false, message);
    }
}

bool FlatpakWorker::plan(const QString &appId, const QString &ref)
{
    GMainContext *context = g_main_context_new();
    g_main_context_push_thread_default(context);
    GError *error = nullptr;
    PlanState state;
    state.appId = appId;
    FlatpakTransaction *transaction = flatpak_transaction_new_for_installation(m_installation, nullptr, &error);
    if (transaction) {
        flatpak_transaction_set_no_interaction(transaction, TRUE);
        flatpak_transaction_add_default_dependency_sources(transaction);
        if (flatpak_transaction_add_install(transaction, remoteName, ref.toUtf8().constData(), nullptr, &error)) {
            g_signal_connect(transaction, "ready", G_CALLBACK(onPlanReady), &state);
            g_signal_connect(transaction, "add-new-remote", G_CALLBACK(onAddNewRemote), &state);
            g_signal_connect(transaction, "choose-remote-for-ref", G_CALLBACK(onChooseRemote), &state);
            // Fails with "aborted" by design once onPlanReady has what it needs.
            flatpak_transaction_run(transaction, nullptr, &error);
        }
        g_clear_error(&error);
        g_object_unref(transaction);
    }
    g_clear_error(&error);
    g_main_context_pop_thread_default(context);
    g_main_context_unref(context);

    if (!state.resolved) {
        return false;
    }
    if (!state.metadata.isEmpty()) {
        Q_EMIT metadataReady(appId, state.metadata);
    }
    Q_EMIT planReady(appId, state.plan);
    return !state.metadata.isEmpty();
}

void FlatpakWorker::details(const QString &appId, const QString &ref)
{
    if (!ensureInstallation()) {
        Q_EMIT metadataReady(appId, QString());
        return;
    }
    const qint64 began = storeClock();
    GError *error = nullptr;
    GBytes *bytes = nullptr;
    FlatpakInstalledRef *installed = flatpak_installation_get_installed_ref(m_installation, FLATPAK_REF_KIND_APP, appId.toUtf8().constData(),
                                                                            nullptr, nullptr, nullptr, &error);
    if (installed) {
        bytes = flatpak_installed_ref_load_metadata(installed, nullptr, &error);
        g_object_unref(installed);
    }
    g_clear_error(&error);
    if (bytes || ref.isEmpty()) {
        QString text;
        if (bytes) {
            gsize size = 0;
            const auto data = static_cast<const char *>(g_bytes_get_data(bytes, &size));
            text = QString::fromUtf8(data, qsizetype(size));
            g_bytes_unref(bytes);
        }
        // Said out loud even when empty, so the app's page stops waiting.
        Q_EMIT metadataReady(appId, text);
        return;
    }

    // The permissions first: they are one small fetch, while resolving the
    // whole install for its sizes fetches the runtime's details too.
    FlatpakRef *parsed = flatpak_ref_parse(ref.toUtf8().constData(), &error);
    if (parsed) {
        bytes = flatpak_installation_fetch_remote_metadata_sync(m_installation, remoteName, parsed, nullptr, &error);
        g_clear_error(&error);
    }
    QString text;
    if (bytes) {
        gsize size = 0;
        const auto data = static_cast<const char *>(g_bytes_get_data(bytes, &size));
        text = QString::fromUtf8(data, qsizetype(size));
        g_bytes_unref(bytes);
    }
    Q_EMIT metadataReady(appId, text);
    qCInfo(lcStore) << "timing: permissions for" << appId << "took" << storeClock() - began << "ms";

    if (!plan(appId, ref) && parsed) {
        guint64 download = 0;
        guint64 installedSize = 0;
        if (flatpak_installation_fetch_remote_size_sync(m_installation, remoteName, parsed, &download, &installedSize, nullptr, &error)) {
            FlatpakPlan fallback;
            fallback.total = qint64(download);
            fallback.installed = qint64(installedSize);
            Q_EMIT planReady(appId, fallback);
        }
        g_clear_error(&error);
    }
    if (parsed) {
        g_object_unref(parsed);
    }
    qCInfo(lcStore) << "timing: sizes for" << appId << "took" << storeClock() - began << "ms";
}
