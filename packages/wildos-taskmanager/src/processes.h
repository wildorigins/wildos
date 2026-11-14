#pragma once

#include "winmode.h"

#include <QAbstractListModel>
#include <QHash>
#include <QQmlEngine>
#include <QSet>
#include <QTimer>

namespace KSysGuard
{
class ProcessDataModel;
class ProcessController;
}

// The Processes page as Windows Task Manager lays it out: "Apps" (programs
// started from Start, the taskbar or a file), "Background processes" (the
// rest of this account's processes, plus the system jobs a person cares
// about such as security updates) and "Wild OS processes" (everything the
// system runs for itself). Each entry can be expanded into its processes.
//
// Processes and their numbers come from libksysguard's ProcessDataModel.
// They are grouped by their systemd unit: the desktop starts every app in
// a scope or service named after its launcher (app-<desktop id>-<n>.scope,
// app-<desktop id>@<n>.service), which is the reliable way to tell which
// app a process belongs to on Wayland. Windows mode is one entry with each
// Windows app's session (wildos-winmode-<id>.scope) nested inside it.
//
// The model is the flattened, visible list of rows (group headers, entries
// and the children of expanded entries), updated in place so the view
// keeps its scroll position and selection.
class Processes : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged)
    Q_PROPERTY(QString sortColumn READ sortColumn WRITE setSortColumn NOTIFY sortChanged)
    Q_PROPERTY(bool sortDescending READ sortDescending WRITE setSortDescending NOTIFY sortChanged)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
    // "Wild OS processes", the system's own, can be left out of the list.
    Q_PROPERTY(bool showSystem READ showSystem WRITE setShowSystem NOTIFY showSystemChanged)
    Q_PROPERTY(int processCount READ processCount NOTIFY updated)
    Q_PROPERTY(int threadCount READ threadCount NOTIFY updated)
    Q_PROPERTY(bool ready READ ready NOTIFY updated)

public:
    enum Kind { Header, Entry, Child };
    Q_ENUM(Kind)

    enum Roles {
        KeyRole = Qt::UserRole + 1,
        KindRole,
        DepthRole,
        NameRole,
        IconRole,
        StatusRole,
        CpuRole,
        MemoryRole,
        DiskRole,
        NetworkRole,
        ExpandableRole,
        ExpandedRole,
        CountRole,
        ChipRole,
        CanEndRole,
        EndLabelRole,
        GroupRole,
    };

    static Processes *instance();
    static Processes *create(QQmlEngine *, QJSEngine *);

    QString search() const { return m_search; }
    void setSearch(const QString &search);
    QString sortColumn() const { return m_sortColumn; }
    void setSortColumn(const QString &column);
    bool sortDescending() const { return m_sortDescending; }
    void setSortDescending(bool descending);
    bool paused() const { return m_paused; }
    void setPaused(bool paused);
    bool showSystem() const { return m_showSystem; }
    void setShowSystem(bool show);
    int processCount() const { return m_processCount; }
    int threadCount() const { return m_threadCount; }
    bool ready() const { return m_ready; }

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Clicking a column header: a new column sorts biggest first (names A
    // to Z), the same one again flips it.
    Q_INVOKABLE void sortBy(const QString &column);
    Q_INVOKABLE void toggle(const QString &key);
    Q_INVOKABLE void setExpanded(const QString &key, bool expanded);
    Q_INVOKABLE QVariantMap entry(const QString &key) const;
    Q_INVOKABLE int indexOf(const QString &key) const;
    Q_INVOKABLE QString keyAt(int row) const { return m_rows.value(row).key; }
    Q_INVOKABLE bool isHeading(int row) const { return m_rows.value(row).kind == Header; }
    // Each returns an empty string when it worked, else a sentence for the
    // person.
    Q_INVOKABLE QString endTask(const QString &key);
    Q_INVOKABLE QString endTree(const QString &key);
    Q_INVOKABLE QString setPriority(const QString &key, int nice);
    Q_INVOKABLE void openFileLocation(const QString &key);
    // The entries using the most of "cpu", "memory", "disk", "network" or
    // "gpu", for the Performance page: name, icon, value and share.
    Q_INVOKABLE QVariantList top(const QString &metric, int count) const;

    // Windows mode as one resource: CPU %, memory in bytes, and what its
    // service last said (state, open windows, idle policy).
    double winModeCpu() const { return m_winCpu; }
    qint64 winModeMemory() const { return m_winMemory; }
    const WinMode::Status &winModeStatus() const { return m_winStatus; }

    // Per-process graphics use, sampled by SystemStats from the kernel's
    // DRM fdinfo; percent of the busiest engine.
    void setGpuUsage(const QHash<qint64, double> &usage) { m_gpu = usage; }

Q_SIGNALS:
    void searchChanged();
    void sortChanged();
    void pausedChanged();
    void showSystemChanged();
    void updated();

private:
    struct Proc {
        qint64 pid = 0;
        qint64 ppid = 0;
        qint64 uid = -1;
        QString name;
        QString command;
        QString cgroup;
        QString status;
        double cpu = 0;
        qint64 memory = 0;
        qint64 disk = 0;
        qint64 network = 0;
        int threads = 0;
        int nice = 0;
    };
    struct Item {
        QString key;
        QString name;
        QString icon;
        QString status;
        QString chip;
        QString winId;
        QString endLabel;
        QList<qint64> pids;
        double cpu = 0;
        qint64 memory = 0;
        qint64 disk = 0;
        qint64 network = 0;
        double gpu = 0;
        int group = 0;
        bool canEnd = true;
        bool restart = false;
        bool winMode = false;
        QList<Item> children;
    };
    struct Row {
        QString key;
        int kind = Entry;
        int depth = 0;
        int group = 0;
        QString name;
        QString icon;
        QString status;
        QString chip;
        double cpu = 0;
        qint64 memory = 0;
        qint64 disk = 0;
        qint64 network = 0;
        bool expandable = false;
        bool expanded = false;
        int count = 0;
        bool canEnd = false;
        QString endLabel;
    };
    struct AppInfo {
        QString id;
        QString name;
        QString icon;
        bool app = false;
        bool flatpak = false;
    };

    explicit Processes(QObject *parent = nullptr);
    void refresh();
    void rebuild();
    void applyRows(const QList<Row> &rows);
    QString cgroupOf(qint64 pid, qint64 startTime);
    AppInfo appForUnit(const QString &unit);
    AppInfo appForProgram(const Proc &p);
    const Item *find(const QString &key) const;
    QList<qint64> treeOf(const QList<qint64> &pids) const;
    QString signal(const QList<qint64> &pids, int signal);
    QString end(const QList<qint64> &pids);

    KSysGuard::ProcessDataModel *m_source = nullptr;
    KSysGuard::ProcessController *m_controller = nullptr;
    QTimer m_coalesce;
    QList<Proc> m_procs;
    QList<Item> m_items;
    QList<Row> m_rows;
    QSet<QString> m_expanded;
    QHash<qint64, QPair<qint64, QString>> m_cgroups;
    QHash<QString, AppInfo> m_apps;
    // Launchers by the program they run, for processes started outside the
    // desktop's launcher (from a terminal, or by another program).
    QHash<QString, AppInfo> m_programs;
    qint64 m_programsRead = 0;
    QHash<qint64, double> m_gpu;
    WinMode::Status m_winStatus;
    double m_winCpuSeconds = -1;
    qint64 m_winSampled = 0;
    QString m_search;
    QString m_sortColumn = QStringLiteral("name");
    bool m_sortDescending = false;
    bool m_paused = false;
    bool m_showSystem = true;
    bool m_ready = false;
    int m_processCount = 0;
    int m_threadCount = 0;
    int m_cpuCount = 1;
    qint64 m_uid = -1;
    double m_winCpu = 0;
    qint64 m_winMemory = 0;
};
