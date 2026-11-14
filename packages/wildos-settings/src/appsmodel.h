#pragma once

#include <QAbstractListModel>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QQmlEngine>
#include <QSet>

// The apps someone can see in Start, with where each came from and whether
// removing it is safe. What the desktop depends on (Settings, the Store,
// Files) is protected, the same way Windows does not offer to uninstall
// Settings.
class AppsModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(QString busyId READ busyId NOTIFY busyChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        IconRole,
        IdRole,
        SourceRole,
        PackageRole,
        CanUninstallRole,
    };

    explicit AppsModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString filter() const { return m_filter; }
    void setFilter(const QString &filter);
    QString busyId() const { return m_busyId; }

    Q_INVOKABLE void reload();
    // withData also takes the app's settings and data, for apps in the home
    // folder; packages and Store apps leave them anyway.
    Q_INVOKABLE void uninstall(const QString &id, bool withData = false);
    // What Uninstall would move to the Trash for an app in the home folder,
    // as ~/ paths, so the dialog can show it before anything goes.
    Q_INVOKABLE QStringList homeFiles(const QString &id) const;
    Q_INVOKABLE QStringList homeData(const QString &id) const;
    Q_INVOKABLE QString nameOf(const QString &id) const;
    Q_INVOKABLE bool isWindowsApp(const QString &id) const;
    Q_INVOKABLE bool canUninstall(const QString &id) const;

Q_SIGNALS:
    void filterChanged();
    void countChanged();
    void busyChanged();
    void uninstalled(const QString &name);
    void uninstallFailed(const QString &name, const QString &reason);
    // A Windows app's own uninstaller is open in its window; the app leaves
    // the list once that finishes.
    void uninstallOpened(const QString &name);

private:
    struct App {
        QString name;
        QString icon;
        QString id;
        QString flatpak;
        bool userFlatpak = false;
        QString package;
        bool protectedApp = false;
        // Installed by its own script or as an AppImage into the home
        // folder, so it belongs to no package: its files are moved to the
        // Trash instead.
        bool homeApp = false;
        // Runs in Windows mode: Windows itself uninstalls it.
        bool windowsApp = false;
        QStringList files;
        QStringList data;
    };

    static void findHomeFiles(App &app, const QString &entryPath, const QString &exec, const QString &icon);

    void applyFilter();
    void finishUninstall(const App &app, bool ok, const QString &reason);
    void uninstallWindowsApp(const App &app);
    QHash<QString, QString> desktopOwners() const;
    QSet<QString> protectedPackages() const;

    QList<App> m_all;
    QList<App> m_shown;
    QString m_filter;
    QString m_busyId;
    // Windows mode writes and removes its apps' launchers there.
    QFileSystemWatcher m_launchers;
    QTimer m_reloadSoon;
};
