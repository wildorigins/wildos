#pragma once

#include <QDBusInterface>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QQmlEngine>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

// Setting Windows mode up, as the setup window (boards 9.1 to 9.7) and first
// run's step 7 (boards 3.9 and 3.10) both see it: the checks of this PC, the
// choices of 9.4, and the request that starts setup.
//
// The checks run here, against the real PC, once begin() is called. The
// licence comes from the system service, because the MSDM table is root's
// to read. Setup itself runs in the system service, so closing a window
// never stops it.
//
// The setup window asks the per-user agent, which passes setup on to the
// system service. First run asks the system service directly for the
// licence and nothing else: in the first boot setup session there is no
// account yet, so the agent has nobody to run for, and the request is
// saved for the account to send at its first sign-in.
class SetupApp : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Setup)
    QML_SINGLETON

    Q_PROPERTY(bool checking READ checking NOTIFY checksChanged)
    // ok, off, unsupported or noaccess
    Q_PROPERTY(QString virtualisation READ virtualisation NOTIFY checksChanged)
    Q_PROPERTY(QString technology READ technology NOTIFY checksChanged)
    Q_PROPERTY(int cores READ cores NOTIFY checksChanged)
    Q_PROPERTY(bool coresOk READ coresOk NOTIFY checksChanged)
    // Windows 11 itself runs on two cores, so a smaller processor is a
    // warning the person can choose past, not a wall.
    Q_PROPERTY(bool slowAccepted READ slowAccepted WRITE setSlowAccepted NOTIFY checksChanged)
    Q_PROPERTY(bool canAcceptSlow READ canAcceptSlow NOTIFY checksChanged)
    Q_PROPERTY(QString memoryLabel READ memoryLabel NOTIFY checksChanged)
    // ok, low (works, slowly) or short
    Q_PROPERTY(QString memoryState READ memoryState NOTIFY checksChanged)
    Q_PROPERTY(QString freeLabel READ freeLabel NOTIFY checksChanged)
    Q_PROPERTY(QString needLabel READ needLabel NOTIFY checksChanged)
    Q_PROPERTY(bool spaceOk READ spaceOk NOTIFY checksChanged)
    // checking, connected, metered or offline
    Q_PROPERTY(QString internet READ internet NOTIFY checksChanged)
    // checking, firmware, none, inuse (Windows is still on this PC) or error
    Q_PROPERTY(QString licence READ licence NOTIFY checksChanged)
    // home, pro or "" (not known until Windows installs)
    Q_PROPERTY(QString edition READ edition NOTIFY checksChanged)
    // Windows is installed on one of this PC's disks: first run only offers
    // Windows mode then (board 3.9).
    Q_PROPERTY(bool windowsFound READ windowsFound NOTIFY checksChanged)
    // Every row of 9.1 is met: what Set up Windows mode and Continue wait for.
    Q_PROPERTY(bool requirementsMet READ requirementsMet NOTIFY checksChanged)
    Q_PROPERTY(QString downloadsLabel READ downloadsLabel NOTIFY checksChanged)
    Q_PROPERTY(QString binLabel READ binLabel NOTIFY checksChanged)
    Q_PROPERTY(bool canRestartToFirmware READ canRestartToFirmware NOTIFY checksChanged)
    Q_PROPERTY(QVariantList memoryChoices READ memoryChoices NOTIFY checksChanged)
    // About how long setup takes, from the same figures the system service
    // uses for "About N minutes left" (setup.py MINUTES).
    Q_PROPERTY(int setupMinutes READ setupMinutes NOTIFY optionsChanged)

    // The Windows licence drop-down of 9.4: firmware, key or none.
    Q_PROPERTY(QString licenceChoice MEMBER m_licenceChoice NOTIFY optionsChanged)
    Q_PROPERTY(QString productKey MEMBER m_productKey NOTIFY optionsChanged)
    // The built-in licence can activate Windows mode: there is one, and it
    // isn't for Home.
    Q_PROPERTY(bool firmwareUsable READ firmwareUsable NOTIFY checksChanged)
    Q_PROPERTY(bool office READ office WRITE setOffice NOTIFY optionsChanged)
    // personal, business, work or key
    Q_PROPERTY(QString officePlan MEMBER m_officePlan NOTIFY optionsChanged)
    Q_PROPERTY(bool openFiles MEMBER m_openFiles NOTIFY optionsChanged)
    Q_PROPERTY(QString whenIdle MEMBER m_whenIdle NOTIFY optionsChanged)
    Q_PROPERTY(int memoryGb MEMBER m_memoryGb NOTIFY optionsChanged)
    Q_PROPERTY(QStringList folders READ folders NOTIFY optionsChanged)
    // Hyper-threading is off in Wild OS; choosing it here turns it on from the
    // next start-up through Settings' helper, which asks for the password.
    Q_PROPERTY(bool hyperThreading MEMBER m_hyperThreading NOTIFY optionsChanged)
    Q_PROPERTY(bool hyperThreadingAvailable READ hyperThreadingAvailable NOTIFY checksChanged)
    // True once the helper has turned it on and only a restart is left.
    Q_PROPERTY(bool hyperThreadingNeedsRestart READ hyperThreadingNeedsRestart NOTIFY optionsChanged)
    // The footer's tick: Microsoft's terms for Windows 11, and for Office
    // when it is chosen. Windows' own licence screen is skipped.
    Q_PROPERTY(bool termsAccepted MEMBER m_termsAccepted NOTIFY optionsChanged)
    // Everything Set up (or Finish) needs: a key of the right shape when one
    // is being entered, the space, and the terms.
    Q_PROPERTY(bool canStart READ canStart NOTIFY optionsChanged)

    // A Windows 11 file the person already has, used in place of the
    // download: "" for none, checking, or ok once it has been checked.
    Q_PROPERTY(QString isoState READ isoState NOTIFY isoChanged)
    Q_PROPERTY(QString isoFile READ isoFile NOTIFY isoChanged)
    Q_PROPERTY(QUrl downloadsFolder READ downloadsFolder CONSTANT)

    // The daemon's Status, or empty while it has not answered.
    Q_PROPERTY(QVariantMap status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool preview READ preview NOTIFY checksChanged)
    Q_PROPERTY(QString previewBoard READ previewBoard NOTIFY checksChanged)

public:
    explicit SetupApp(QObject *parent = nullptr);

    bool checking() const { return m_pending > 0; }
    QString virtualisation() const { return m_virt; }
    QString technology() const { return m_technology; }
    int cores() const { return m_cores; }
    bool coresOk() const;
    bool slowAccepted() const { return m_slowAccepted; }
    void setSlowAccepted(bool accepted);
    bool canAcceptSlow() const;
    QString memoryLabel() const { return m_memoryLabel; }
    QString memoryState() const { return m_memoryState; }
    QString freeLabel() const { return m_freeLabel; }
    QString needLabel() const;
    bool hyperThreadingAvailable() const;
    bool hyperThreadingNeedsRestart() const;
    bool spaceOk() const;
    QString internet() const { return m_internet; }
    QString licence() const { return m_licence; }
    QString edition() const { return m_edition; }
    bool windowsFound() const { return m_windowsFound; }
    bool requirementsMet() const;
    bool firmwareUsable() const;
    QString downloadsLabel() const { return m_downloadsLabel; }
    QString binLabel() const { return m_binLabel; }
    bool canRestartToFirmware() const { return m_canFirmware; }
    QVariantList memoryChoices() const;
    int setupMinutes() const;
    bool office() const { return m_office; }
    void setOffice(bool on);
    QStringList folders() const;
    bool canStart() const;
    QString isoState() const { return m_isoState; }
    QString isoFile() const;
    QUrl downloadsFolder() const;
    QVariantMap status() const { return m_status; }
    bool preview() const { return !m_preview.isEmpty(); }
    QString previewBoard() const { return m_preview; }

    // Starts the checks. use is "setup" for the setup window, "firstrun"
    // for first run's step 7, or "preview" with a board name, for design
    // reviews with made-up values.
    Q_INVOKABLE void begin(const QString &use, const QString &board = {});
    Q_INVOKABLE void recheck();
    Q_INVOKABLE void restartToFirmware();
    Q_INVOKABLE void openStorage();
    Q_INVOKABLE void openUrl(const QString &url);
    Q_INVOKABLE bool keyShapeValid(const QString &typed) const;
    Q_INVOKABLE QString normaliseKey(const QString &typed) const;
    // The request the system service starts setup from, as JSON: the
    // licence chosen, the options and the terms accepted. background: from
    // first run, which goes straight to the desktop (board 9.11) and leaves
    // the progress to the taskbar.
    Q_INVOKABLE QString setupRequest(bool background) const;
    // Sends setupRequest() through the agent; "" once it has started, or
    // why not.
    Q_INVOKABLE QString startSetup(bool background = false);
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void openDownloadPage();
    Q_INVOKABLE QString chooseIso(const QString &url);
    // Checks a Windows 11 file before setup will use it; isoRefused says
    // why one can't be.
    Q_INVOKABLE void checkIso(const QString &url);
    Q_INVOKABLE void forgetIso();
    Q_INVOKABLE void openApp(const QString &id);
    // Whether the daemon has set up Windows mode already, or is doing so.
    Q_INVOKABLE QString daemonState() const;

Q_SIGNALS:
    void checksChanged();
    void optionsChanged();
    void statusChanged();
    void isoChanged();
    void isoRefused(const QString &text);

private Q_SLOTS:
    void refreshStatus();

private:
    void checkInternet();
    void checkLicence();
    void checkSpace();
    void finishOne();
    void loadPreview();
    void chooseDefaults();

    QString m_preview;
    bool m_canned = false;
    int m_pending = 0;
    QString m_virt = QStringLiteral("off");
    QString m_technology;
    int m_cores = 0;
    bool m_slowAccepted = false;
    qint64 m_memory = 0;
    QString m_memoryLabel;
    QString m_memoryState = QStringLiteral("ok");
    qint64 m_free = -1;
    QString m_freeLabel;
    QString m_internet = QStringLiteral("checking");
    QString m_licence = QStringLiteral("checking");
    QString m_edition;
    bool m_windowsFound = false;
    QString m_downloadsLabel;
    QString m_binLabel;
    bool m_canFirmware = false;

    QString m_licenceChoice = QStringLiteral("none");
    QString m_productKey;
    bool m_office = true;
    QString m_officePlan = QStringLiteral("personal");
    bool m_openFiles = true;
    QString m_whenIdle = QStringLiteral("automatic");
    int m_memoryGb = 4;
    QStringList m_folders;
    bool m_hyperThreading = false;
    bool m_termsAccepted = false;

    QString m_isoPath;
    QString m_isoState;
    QPointer<QProcess> m_isoCheck;

    QVariantMap m_status;
    QDBusInterface *m_daemon = nullptr;
};
