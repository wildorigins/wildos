#pragma once

#include "peinfo.h"
#include "winapps.h"

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QQmlEngine>
#include <QVariantList>
#include <QVariantMap>

#include <QQuickWindow>

// The "Opening a Windows .exe" dialog, the front door to Windows mode. wild-run
// (package wildos-run) decides where the program runs, from what the file
// says about itself and Wild OS's list of Windows programs; this shows that
// decision, offers the Wild OS version first when there is one, and hands
// the installer to Windows mode.
class Bridge : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // ask, disabled, installing or failed
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString fileName READ fileName CONSTANT)
    Q_PROPERTY(QString fromText READ fromText CONSTANT)
    Q_PROPERTY(QString appName READ appName CONSTANT)
    Q_PROPERTY(QString iconUrl READ iconUrl CONSTANT)
    // well (Wild OS knows it runs in Windows mode), no (it can't run here,
    // see compatReason) or unknown
    Q_PROPERTY(QString compat READ compat CONSTANT)
    Q_PROPERTY(QString compatReason READ compatReason CONSTANT)
    // checking, verified or unknown
    Q_PROPERTY(QString publisherState READ publisherState NOTIFY publisherChanged)
    Q_PROPERTY(QString signer READ signer NOTIFY publisherChanged)
    Q_PROPERTY(QString nativeText READ nativeText CONSTANT)
    Q_PROPERTY(QString storeId READ storeId CONSTANT)
    // The Store is installed and has the native app to show.
    Q_PROPERTY(bool canOpenStore READ canOpenStore CONSTANT)
    // The file is not a Windows program at all, so nothing can run it.
    Q_PROPERTY(bool notWindows READ notWindows CONSTANT)
    Q_PROPERTY(bool documents READ documents WRITE setDocuments NOTIFY permissionsChanged)
    Q_PROPERTY(bool printer READ printer WRITE setPrinter NOTIFY permissionsChanged)
    Q_PROPERTY(bool internet READ internet WRITE setInternet NOTIFY permissionsChanged)
    Q_PROPERTY(bool supportMissing READ supportMissing NOTIFY supportChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString progressText READ progressText NOTIFY progressChanged)
    Q_PROPERTY(QString message READ message NOTIFY stateChanged)
    Q_PROPERTY(QStringList launchers READ launchers NOTIFY stateChanged)
    Q_PROPERTY(bool alreadyInstalled READ alreadyInstalled CONSTANT)
    // A calling app (Teams, Zoom): its Wild OS version is offered first and
    // Windows mode second, as WinModeCalls.dc draws it.
    Q_PROPERTY(bool callApp READ callApp CONSTANT)
    Q_PROPERTY(bool hasNativeVersion READ hasNativeVersion CONSTANT)
    // Windows mode (package wildos-winmode) is set up and can run it now;
    // otherwise windowsModeReason says why not.
    Q_PROPERTY(bool windowsModeReady READ windowsModeReady CONSTANT)
    Q_PROPERTY(QString windowsModeReason READ windowsModeReason CONSTANT)
    // Windows mode needs setting up before it can run this.
    Q_PROPERTY(bool needsSetup READ needsSetup CONSTANT)

public:
    explicit Bridge(QObject *parent = nullptr);

    // Set before QML first asks for the singleton.
    static void setFile(const QString &path);

    QString state() const { return m_state; }
    QString fileName() const;
    QString fromText() const;
    QString appName() const { return m_appName; }
    QString iconUrl() const { return m_iconUrl; }
    QString compat() const { return m_compat; }
    QString compatReason() const { return m_compatReason; }
    QString publisherState() const { return m_publisherState; }
    QString signer() const { return m_signer; }
    QString nativeText() const { return m_nativeText; }
    QString storeId() const { return m_storeId; }
    bool canOpenStore() const;
    bool notWindows() const { return m_info.kind == PeInfo::NotWindows; }
    bool documents() const { return m_permissions.documents; }
    bool printer() const { return m_permissions.printer; }
    bool internet() const { return m_permissions.internet; }
    void setDocuments(bool on);
    void setPrinter(bool on);
    void setInternet(bool on);
    bool supportMissing() const { return false; }
    int progress() const { return m_progress; }
    QString progressText() const { return m_progressText; }
    QString message() const { return m_message; }
    QStringList launchers() const { return m_launcherNames; }
    bool alreadyInstalled() const { return m_alreadyInstalled; }
    bool callApp() const { return m_callApp; }
    bool hasNativeVersion() const { return !m_webApp.isEmpty() || canOpenStore(); }
    bool windowsModeReady() const { return m_windowsAction == QLatin1String("windows-mode"); }
    QString windowsModeReason() const;
    bool needsSetup() const { return m_action == QLatin1String("setup"); }

    Q_INVOKABLE void install();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void openStore();
    // The Wild OS version of a calling app: its Store page, or a window of
    // its web app with a Start entry of its own.
    Q_INVOKABLE void installNative();
    // Hands the installer to Windows mode; returns a sentence when it can't
    // even ask. Windows may have to start first, so the answer comes later,
    // as windowsModeAnswered: empty once the installer is opening.
    Q_INVOKABLE QString installInWindowsMode();
    Q_INVOKABLE void openSettings();
    Q_INVOKABLE void openApp(int index = 0);
    Q_INVOKABLE void close();
    Q_INVOKABLE void setDialogGeometry(QQuickWindow *window, const QRectF &rect, qreal radius);

Q_SIGNALS:
    void stateChanged();
    void publisherChanged();
    void permissionsChanged();
    void supportChanged();
    void progressChanged();
    void windowsModeAnswered(const QString &message);

private:
    void setState(const QString &state, const QString &message = {});
    void setProgress(int percent, const QString &text);
    void readDecision();
    void readCatalogue();
    void checkSignature();

    static QString s_path;

    QString m_path;
    PeInfo m_info;
    bool m_handingOver = false;
    QString m_state = QStringLiteral("ask");
    QString m_appName;
    QString m_id;
    QString m_iconUrl;
    QString m_compat = QStringLiteral("unknown");
    QString m_compatReason;
    QString m_publisherState = QStringLiteral("checking");
    QString m_signer;
    QString m_nativeText;
    QString m_storeId;
    QString m_catalogueName;
    WinApps::Permissions m_permissions;
    int m_progress = -1;
    QString m_progressText;
    QString m_message;
    QStringList m_launcherNames;
    QStringList m_launcherFiles;
    bool m_alreadyInstalled = false;
    bool m_callApp = false;
    QString m_webApp;
    // wild-run's action: windows-mode, native, unsupported, home, cannot,
    // setup or not-windows
    QString m_action;
    // What Windows mode itself could do with it, whatever action is
    // recommended (wild-run's "windows"); the same as m_action from older
    // versions that don't say.
    QString m_windowsAction;
    QString m_windowsReason;
};
