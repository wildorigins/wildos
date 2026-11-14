#pragma once

#include <QColor>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QSet>
#include <QStringList>
#include <QVariantList>

#include <AppStreamQt/component.h>

#include "storeapp.h"

#include <memory>
#include <optional>

namespace AppStream
{
class Pool;
}

class FlatpakBackend;
class PackageKitBackend;
class PackageSizes;
struct FlatpakPlan;

// Hand-picked details that AppStream does not carry: what an app replaces
// on Windows, the short lines the design shows on cards, and which source to
// prefer when an app is in both catalogues. Read from curated.json.
struct Curated {
    QString key;
    QStringList ids;
    QStringList packages;
    QString flatpak;
    QString prefer;
    QString name;
    QString summary;
    QString tagline;
    QString insteadOf;
    QStringList aliases;
    QString cardNote;
    QString chip;
    QString swapTitle;
    QString swapText;
    QString description;
    QString icon;
    QString desktop;
    QString category;
};

// One app as the Store shows it, joined from the Wild OS (apt) catalogue,
// Flathub and the curated file. The name, summary and icon are worked out
// when the catalogue is built, off the window's thread, and saved with it,
// so lists never touch AppStream and the next start can draw them from disk
// before either catalogue has loaded.
struct Record {
    QString key;
    std::optional<AppStream::Component> apt;
    std::optional<AppStream::Component> flat;
    const Curated *curated = nullptr;
    QStringList packages;
    QString flatpakId;
    QString flatpakRef;
    QStringList categories;
    int quality = 0;
    bool hasApt = false;
    bool hasFlat = false;
    QString aptName;
    QString flatName;
    QString aptSummary;
    QString flatSummary;
    QString aptIcon;
    QString flatIcon;
    // Lower case name, then everything else search looks at.
    QString searchName;
    QString searchText;
};

struct AppState {
    bool busy = false;
    bool queued = false;
    int progress = -1;
    QString phase;
    QString action;
    QString error;
    QString chosenSource;
    qint64 downloadSize = -1;
    QString candidateVersion;
    QString flatpakMetadata;
    bool metadataFailed = false;
    // The step a download is on when it is worth naming (a shared runtime),
    // and how far it has got in bytes.
    QString detail;
    qint64 doneBytes = -1;
    qint64 totalBytes = -1;
    // From Flathub before installing: everything it downloads, and the part
    // of that a later app of the same kind would not need again.
    qint64 planTotal = -1;
    qint64 planShared = -1;
    QString planRuntime;
    qint64 planInstalled = -1;
    // Stopped part way by its stop button or a lost connection; Get carries
    // on from what was already fetched.
    bool paused = false;
    // The stop square was pressed and libflatpak is still winding down; the
    // button shows Paused at once. Pressing it again in that time carries
    // on as soon as the stop has gone through.
    bool stopping = false;
    bool resumeAfterStop = false;
    // For the time left: when the download began and how much was in hand.
    qint64 rateStarted = -1;
    qint64 rateStartBytes = 0;
    qint64 secondsLeft = -1;
};

// Everything a rebuild makes, swapped in whole on the window's thread.
struct CatalogData {
    QHash<QString, Record> records;
    QHash<QString, QString> aliases;
    QStringList sorted;
    QHash<QString, QStringList> categories;
};

class Catalog : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool ready READ isReady NOTIFY readyChanged)
    // Bumps when the list of apps changes; pages showing lists of apps bind
    // to it. stateRevision bumps when what is installed, updatable or busy
    // changes, which only the Installed and Updates pages list by.
    Q_PROPERTY(int catalogRevision READ catalogRevision NOTIFY catalogRevisionChanged)
    Q_PROPERTY(int stateRevision READ stateRevision NOTIFY stateRevisionChanged)
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(int updateCount READ updateCount NOTIFY stateRevisionChanged)
    Q_PROPERTY(int workingCount READ workingCount NOTIFY stateRevisionChanged)
    Q_PROPERTY(int updateAllProgress READ updateAllProgress NOTIFY updateAllProgressChanged)
    Q_PROPERTY(bool flathubAvailable READ flathubAvailable NOTIFY catalogRevisionChanged)
    // What every pending update downloads together, when Flathub said.
    Q_PROPERTY(qint64 updatesBytes READ updatesBytes NOTIFY stateRevisionChanged)
    // Flathub apps update overnight on mains power (a systemd user timer).
    Q_PROPERTY(bool autoUpdate READ autoUpdate WRITE setAutoUpdate NOTIFY autoUpdateChanged)
    Q_PROPERTY(QVariantList appCategories READ appCategories CONSTANT)
    Q_PROPERTY(QVariantList gameCategories READ gameCategories CONSTANT)
    Q_PROPERTY(bool busy READ isBusy NOTIFY stateRevisionChanged)
    // "Check for updates": running, and how the last one went ("", ok or
    // failed) and when, so the button can say what it did.
    Q_PROPERTY(bool checking READ isChecking NOTIFY checkChanged)
    Q_PROPERTY(QString checkResult READ checkResult NOTIFY checkChanged)
    Q_PROPERTY(QString checkedAt READ checkedAt NOTIFY checkChanged)

public:
    explicit Catalog(QObject *parent = nullptr);
    ~Catalog() override;

    static Catalog *instance();
    static Catalog *create(QQmlEngine *, QJSEngine *);
    // Reads the catalogues and starts the package systems; called once the
    // first frame is on screen, or at once without a snapshot to show.
    void startBackgroundWork();

    bool isReady() const { return m_ready; }
    int catalogRevision() const { return m_catalogRevision; }
    int stateRevision() const { return m_stateRevision; }
    int revision() const { return m_catalogRevision + m_stateRevision; }
    int updateCount() const;
    int workingCount() const;
    int updateAllProgress() const;
    bool flathubAvailable() const;
    QVariantList appCategories() const;
    QVariantList gameCategories() const;
    bool isBusy() const { return !m_jobs.isEmpty(); }
    bool isChecking() const { return m_checking; }
    qint64 updatesBytes() const;
    bool autoUpdate() const { return m_autoUpdate; }
    void setAutoUpdate(bool on);
    QString checkResult() const { return m_checkResult; }
    QString checkedAt() const { return m_checkedAt; }

    Q_INVOKABLE StoreApp *app(const QString &key);
    Q_INVOKABLE QString resolve(const QString &query) const;
    Q_INVOKABLE QStringList homeSwaps() const;
    Q_INVOKABLE QStringList homeInstalled() const;
    Q_INVOKABLE QStringList swaps() const;
    Q_INVOKABLE QStringList essentials() const;
    Q_INVOKABLE QStringList featured(const QString &list) const;
    Q_INVOKABLE QStringList category(const QString &id, int limit = -1) const;
    Q_INVOKABLE QStringList search(const QString &text) const;
    Q_INVOKABLE QStringList swapMatches(const QString &text) const;
    Q_INVOKABLE QStringList installedApps() const;
    Q_INVOKABLE QStringList updates() const;
    // Installed apps whose current version arrived in the last month, newest
    // first, read from the installed files rather than any record the
    // Store keeps.
    Q_INVOKABLE QStringList recentlyUpdated(int limit = 5) const;
    // Apps being installed, updated or removed, and those waiting, in the
    // order they will run.
    Q_INVOKABLE QStringList workingApps() const;
    // Apps in any of these freedesktop categories, best first.
    Q_INVOKABLE QStringList matching(const QStringList &categories, int limit = -1) const;
    // "Yesterday", "Sat" or a date, for a time in milliseconds.
    Q_INVOKABLE QString dayText(qint64 msecs) const;
    Q_INVOKABLE QString sizeText(qint64 bytes) const;
    Q_INVOKABLE QString categoryLabel(const QString &id) const;
    // The image from a screenshot's list of sizes that fills width by height
    // device pixels without being much larger.
    Q_INVOKABLE QString pickImage(const QVariantList &images, int width, int height) const;
    Q_INVOKABLE void installEssentials();
    // The Wild OS section: the apps we make, in the curated file's order,
    // and putting back every one that was removed.
    Q_INVOKABLE QStringList wildosApps() const;
    Q_INVOKABLE void reinstallWildOS();
    // What a Wild OS package's app takes up, installed, or would take up
    // once put back; -1 until dpkg and apt have said.
    Q_INVOKABLE qint64 appSize(const QString &key) const;
    Q_INVOKABLE void updateAll();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void openSettings(const QString &page);

    // Used by LocalPackage, for files someone opened.
    FlatpakBackend *flatpakBackend() const { return m_flatpak; }
    PackageKitBackend *packageKitBackend() const { return m_packagekit; }

    // Used by StoreApp.
    const Record *record(const QString &key) const;
    AppState &state(const QString &key) { return m_states[key]; }
    QString source(const QString &key) const;
    QString installedSource(const QString &key) const;
    bool isInstalled(const QString &key) const;
    bool hasUpdate(const QString &key) const;
    QString installedVersion(const QString &key) const;
    qint64 installedSize(const QString &key) const;
    QString flatpakAppstreamDir() const;
    void install(const QString &key);
    void remove(const QString &key);
    void update(const QString &key);
    void cancel(const QString &key);
    void pause(const QString &key);
    qint64 updatedTime(const QString &key) const;
    qint64 updateSize(const QString &key) const;
    void fetchDetails(const QString &key);
    void stateChangedFor(const QString &key);

Q_SIGNALS:
    void readyChanged();
    void catalogRevisionChanged();
    void stateRevisionChanged();
    void revisionChanged();
    void updateAllProgressChanged();
    void autoUpdateChanged();
    void checkChanged();
    void appStateChanged(const QString &key);
    // A finished install, so the window can offer to open it.
    void installed(const QString &key);
    void failed(const QString &key, const QString &message);

private:
    enum class JobKind { Install, Remove, Update };
    struct Job {
        int id;
        JobKind kind;
        QString source;
        QStringList keys;
        QStringList targets;
        bool running = false;
        bool all = false;
    };

    void loadCurated();
    void loadOsPool();
    void loadFlatpakPool();
    void rebuild();
    void adopt(CatalogData data, qint64 built);
    bool loadSnapshot();
    void saveSnapshot();
    void bumpCatalog();
    void bumpState();
    void enqueue(JobKind kind, const QString &source, const QStringList &keys, const QStringList &targets, bool all = false);
    void scheduleRun();
    void runNext();
    void start(const QList<int> &ids);
    void onProgress(int job, int percent, const QString &phase, const QString &detail, qint64 done, qint64 total);
    void onFinished(int job, bool ok, const QString &error);
    void onPlan(const QString &appId, const FlatpakPlan &plan);
    bool loadDetails(const QString &key);
    void saveDetails(const QString &key);
    void checkPartDone(bool ok);
    QString keyFor(const QString &id) const;
    Job *job(int id);
    QStringList wildosPackages() const;

    static Catalog *s_instance;

    std::shared_ptr<AppStream::Pool> m_osPool;
    std::shared_ptr<AppStream::Pool> m_flatPool;
    // What each catalogue was read from last, so a reload that would read
    // the same files again is skipped.
    QString m_osInputs;
    QString m_flatInputs;
    bool m_osLoaded = false;
    bool m_flatLoaded = false;
    bool m_ready = false;
    bool m_fromSnapshot = false;
    bool m_started = false;
    bool m_building = false;
    bool m_rebuildAgain = false;
    int m_catalogRevision = 0;
    int m_stateRevision = 0;

    QList<Curated> m_curated;
    QStringList m_homeSwaps;
    QStringList m_homeInstalled;
    QStringList m_essentials;
    QHash<QString, QStringList> m_featured;
    QSet<QString> m_hidden;
    QStringList m_wildos;

    QHash<QString, Record> m_records;
    QHash<QString, QString> m_aliases;
    QStringList m_sorted;
    QHash<QString, QStringList> m_categories;

    QHash<QString, AppState> m_states;
    QHash<QString, QPointer<StoreApp>> m_apps;

    QList<Job> m_jobs;
    QSet<int> m_cancelled;
    QSet<int> m_pausing;
    bool m_autoUpdate = false;
    int m_nextJob = 1;
    bool m_runQueued = false;
    QHash<int, qint64> m_jobStarted;
    QHash<QString, qint64> m_detailsAsked;

    PackageKitBackend *m_packagekit;
    FlatpakBackend *m_flatpak;
    PackageSizes *m_sizes;

    bool m_checking = false;
    int m_checkParts = 0;
    bool m_checkOk = true;
    QString m_checkResult;
    QString m_checkedAt;
};
