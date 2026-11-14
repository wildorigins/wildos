#pragma once

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QVariantList>

/* What the installer pages share.
 *
 * Calamares gives every page, the sidebar and the footer their own QML
 * engine, so a QML singleton would be one object per page. This one is a
 * single object for the whole process. It holds:
 *  - the configuration objects of steps whose answers another page shows
 *    (time zone, keyboard, finished),
 *  - the checks of the "Before we start" page, run by check-computer,
 *  - the disk password page's state and the recovery key, which never
 *    leaves this process except into the LUKS key slot and onto the USB
 *    stick or paper the person chooses,
 *  - whether the licence page's box is ticked, and the licence text it
 *    shows. The answer lives here, not in the page, so it starts unticked
 *    on every run and stays ticked when someone comes Back to the page.
 */
class Installer : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QObject *welcome READ welcome WRITE setWelcome NOTIFY welcomeChanged)
    Q_PROPERTY(QObject *locale READ locale WRITE setLocale NOTIFY localeChanged)
    Q_PROPERTY(QObject *keyboard READ keyboard WRITE setKeyboard NOTIFY keyboardChanged)
    Q_PROPERTY(QObject *finished READ finished WRITE setFinished NOTIFY finishedChanged)

    Q_PROPERTY(bool checking READ checking NOTIFY checksChanged)
    Q_PROPERTY(QVariantList checks READ checks NOTIFY checksChanged)
    Q_PROPERTY(QString alongsideBlocked READ alongsideBlocked NOTIFY checksChanged)

    Q_PROPERTY(bool licenceAccepted MEMBER m_licenceAccepted NOTIFY licenceAcceptedChanged)
    /// True until the install step: the footer keeps it up to date. While
    /// it holds, however the installer is closed it leaves at once.
    Q_PROPERTY(bool exitQuickly MEMBER m_exitQuickly)
    Q_PROPERTY(bool encryptionReady MEMBER m_encryptionReady NOTIFY encryptionReadyChanged)
    Q_PROPERTY(QString recoveryKey READ recoveryKey CONSTANT)
    Q_PROPERTY(bool keySaved MEMBER m_keySaved NOTIFY keySavedChanged)
    Q_PROPERTY(QVariantList drives READ drives NOTIFY drivesChanged)
    /// "install" when the USB menu started straight into the installer
    /// (wildos.mode=install), so there is no trial desktop behind it.
    Q_PROPERTY(QString startMode READ startMode CONSTANT)

public:
    static Installer *instance();
    static Installer *create(QQmlEngine *, QJSEngine *);

    QObject *welcome() const { return m_welcome; }
    QObject *locale() const { return m_locale; }
    QObject *keyboard() const { return m_keyboard; }
    QObject *finished() const { return m_finished; }
    void setWelcome(QObject *o);
    void setLocale(QObject *o);
    void setKeyboard(QObject *o);
    void setFinished(QObject *o);

    bool checking() const { return m_checking; }
    QVariantList checks() const { return m_checks; }
    QString alongsideBlocked() const { return m_alongsideBlocked; }
    /// Runs check-computer again; checksChanged when it is done.
    Q_INVOKABLE void runChecks();

    /// The GPL as numbered sections for the full licence window:
    /// [{ number, title, paragraphs }], number -1 for the title page and
    /// preamble. The words are the file's own; only the line breaks of its
    /// fixed-width layout are undone, so paragraphs wrap to the window.
    Q_INVOKABLE QVariantList licenceSections() const;
    /// Asks where to save the licence file and copies it there unchanged.
    /// @p window is the licence window, so the file dialog opens above it.
    /// Returns "" when saved or cancelled, else what went wrong.
    Q_INVOKABLE QString saveLicence(QObject *window);
    /// Closes the installer without asking: used before anything has been
    /// chosen, when there is nothing to lose.
    Q_INVOKABLE void quitNow();

    /// Calamares' reversible scrambling for secrets in global storage
    /// (KStringHandler::obscure), so they are not plain text in its logs.
    Q_INVOKABLE QString obscure(const QString &text) const;

    /// Six groups of five characters, from a cryptographic random source.
    QString recoveryKey() const { return m_recoveryKey; }
    QVariantList drives() const { return m_drives; }
    QString startMode() const;
    /// Lists USB drives the key can be saved on: removable, writable, with
    /// a file system Windows and Wild OS can both read.
    Q_INVOKABLE void refreshDrives();
    /// Saves the key as a text file on @p device; returns "" or what went wrong.
    Q_INVOKABLE QString saveKey(const QString &device);

Q_SIGNALS:
    void welcomeChanged();
    void localeChanged();
    void keyboardChanged();
    void finishedChanged();
    void checksChanged();
    void licenceAcceptedChanged();
    void encryptionReadyChanged();
    void keySavedChanged();
    void drivesChanged();

private:
    Installer();

    QPointer<QObject> m_welcome;
    QPointer<QObject> m_locale;
    QPointer<QObject> m_keyboard;
    QPointer<QObject> m_finished;
    bool m_checking = false;
    QVariantList m_checks;
    QString m_alongsideBlocked;
    bool m_licenceAccepted = false;
    bool m_exitQuickly = true;
    bool m_encryptionReady = false;
    QString m_recoveryKey;
    bool m_keySaved = false;
    QVariantList m_drives;
};
