#pragma once

#include <KIO/AskUserActionInterface>
#include <KIO/OpenWithHandlerInterface>
#include <KIO/UntrustedProgramHandlerInterface>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QSet>
#include <QUrl>
#include <QVariantMap>

class KCoreDirLister;
class KJob;
class KJobUiDelegate;
class Sharer;

// Answers KIO's questions (a name clash while copying, a file it cannot
// read) through Files' own dialogs instead of KDE's widget dialogs.
class Asker : public KIO::AskUserActionInterface
{
    Q_OBJECT

public:
    explicit Asker(QObject *parent);

    void askUserRename(KJob *job, const QString &title, const QUrl &src, const QUrl &dest, KIO::RenameDialog_Options options,
                       KIO::filesize_t sizeSrc, KIO::filesize_t sizeDest, const QDateTime &ctimeSrc, const QDateTime &ctimeDest,
                       const QDateTime &mtimeSrc, const QDateTime &mtimeDest) override;
    void askUserSkip(KJob *job, KIO::SkipDialog_Options options, const QString &errorText) override;
    void askUserDelete(const QList<QUrl> &urls, DeletionType deletionType, ConfirmationType confirmationType, QWidget *parent) override;
    void requestUserMessageBox(MessageDialogType type, const QString &text, const QString &title, const QString &primaryActionText,
                               const QString &secondaryActionText, const QString &primaryActionIconName, const QString &secondaryActionIconName,
                               const QString &dontAskAgainName, const QString &details, QWidget *parent) override;
    void askIgnoreSslErrors(const QVariantMap &sslErrorData, QWidget *parent) override;

private:
    // Files' own in-window question, for when the system clash dialog
    // (wildos-portal) is not there.
    void askInWindow(KJob *job, const QString &title, const QUrl &src, const QUrl &dest, KIO::RenameDialog_Options options,
                     KIO::filesize_t sizeSrc, KIO::filesize_t sizeDest, const QDateTime &ctimeSrc, const QDateTime &ctimeDest,
                     const QDateTime &mtimeSrc, const QDateTime &mtimeDest);
    static QUrl keepBothUrl(const QUrl &dest);
};

// "Open with" for a file no app claims: Files' own chooser instead of
// KOpenWithDialog. The job is let go and the chooser opens the file.
class OpenWithAsker : public KIO::OpenWithHandlerInterface
{
    Q_OBJECT

public:
    explicit OpenWithAsker(QObject *parent);
    void promptUserForApplication(KJob *job, const QList<QUrl> &urls, const QString &mimeType) override;
};

// A shortcut (.desktop file) that is not marked as trusted: asked in a
// Wild OS dialog rather than KIO's widget one.
class UntrustedAsker : public KIO::UntrustedProgramHandlerInterface
{
    Q_OBJECT

public:
    explicit UntrustedAsker(QObject *parent);
    void showUntrustedProgramWarning(KJob *job, const QString &programName) override;
};

// Everything Files does to files: open, copy, move, rename, delete,
// restore, share. One instance serves every window.
class FileOps : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool canPaste READ canPaste NOTIFY clipboardChanged)
    Q_PROPERTY(int binCount READ binCount NOTIFY binCountChanged)
    Q_PROPERTY(QVariantMap prompt READ prompt NOTIFY promptChanged)
    Q_PROPERTY(QObject *activeWindow READ activeWindow WRITE setActiveWindow NOTIFY activeWindowChanged)

public:
    static FileOps *instance();
    static FileOps *create(QQmlEngine *, QJSEngine *);

    bool canPaste() const;
    bool isCut(const QUrl &url) const { return m_cut.contains(url); }
    int binCount() const { return m_binCount; }
    QVariantMap prompt() const { return m_prompts.isEmpty() ? QVariantMap() : m_prompts.constFirst(); }
    QObject *activeWindow() const { return m_activeWindow; }
    void setActiveWindow(QObject *window);

    Q_INVOKABLE void open(const QList<QUrl> &urls);
    Q_INVOKABLE QVariantList appsFor(const QUrl &url) const;
    Q_INVOKABLE QVariantList allApps() const;
    Q_INVOKABLE QString defaultAppName(const QUrl &url) const;
    Q_INVOKABLE void openWith(const QList<QUrl> &urls, const QString &appId, bool makeDefault);
    Q_INVOKABLE void openTerminal(const QUrl &folder);

    Q_INVOKABLE void copy(const QList<QUrl> &urls);
    Q_INVOKABLE void cut(const QList<QUrl> &urls);
    Q_INVOKABLE void copyPaths(const QList<QUrl> &urls);
    Q_INVOKABLE void paste(const QUrl &folder);
    Q_INVOKABLE int dropAction(const QList<QUrl> &urls, const QUrl &folder, int modifiers) const;
    Q_INVOKABLE void drop(const QList<QUrl> &urls, const QUrl &folder, int action);

    Q_INVOKABLE void trash(const QList<QUrl> &urls);
    Q_INVOKABLE void deleteForGood(const QList<QUrl> &urls);
    Q_INVOKABLE void restore(const QList<QUrl> &urls);
    Q_INVOKABLE void emptyBin();

    // Rename takes the name as shown: with extensions hidden, the hidden
    // extension is kept, as on Windows.
    Q_INVOKABLE QString editableName(const QUrl &url, const QString &fileName, bool isDir) const;
    Q_INVOKABLE void rename(const QUrl &url, const QString &fileName, bool isDir, const QString &typed);
    Q_INVOKABLE void newFolder(const QUrl &folder);
    // The New menu: Folder, Shortcut and the document types that have an
    // app to open them (see newitems.cpp).
    Q_INVOKABLE QVariantList newFileTypes() const;
    Q_INVOKABLE void newFile(const QUrl &folder, const QString &id);
    Q_INVOKABLE QVariantMap checkShortcut(const QString &target) const;
    Q_INVOKABLE void newShortcut(const QUrl &folder, const QString &target, const QString &name);

    Q_INVOKABLE QVariantList shareTargets(const QList<QUrl> &urls);
    Q_INVOKABLE void share(int index);

    // The file menu (DialogFileMenu.dc.html), see fileextras.cpp.
    Q_INVOKABLE void compress(const QList<QUrl> &urls);
    // Archive (wildos-archive): Compress... opens its New archive with the
    // selection, Extract here unpacks archives beside themselves.
    Q_INVOKABLE bool canUseArchive() const;
    Q_INVOKABLE bool areArchives(const QList<QUrl> &urls) const;
    Q_INVOKABLE void compressWith(const QList<QUrl> &urls);
    Q_INVOKABLE void extractHere(const QList<QUrl> &urls);
    Q_INVOKABLE bool canWriteIn(const QUrl &folder) const;
    Q_INVOKABLE QVariantList sendTargets(const QList<QUrl> &urls) const;
    Q_INVOKABLE void sendTo(const QList<QUrl> &urls, const QString &kind, const QUrl &target);
    Q_INVOKABLE bool canPinToStart() const;
    Q_INVOKABLE void pinToStart(const QList<QUrl> &urls);
    // Properties: network sharing through Samba user shares, snapshots of
    // earlier versions, and a copy made by hand.
    Q_INVOKABLE QVariantMap networkShare(const QUrl &folder) const;
    Q_INVOKABLE void setNetworkShare(const QUrl &folder, bool shared);
    Q_INVOKABLE QVariantList previousVersions(const QUrl &url) const;
    Q_INVOKABLE void restoreVersion(const QUrl &version, const QUrl &current);
    Q_INVOKABLE void duplicate(const QUrl &url);
    Q_INVOKABLE void applyProperties(const QUrl &url, const QString &fileName, bool isDir, const QString &typed, bool hidden);

    // Advanced protection, see protectionops.cpp: a copy without personal
    // details, and opening a file in the safe box.
    Q_INVOKABLE bool canRemoveDetails(const QList<QUrl> &urls) const;
    Q_INVOKABLE void removeDetails(const QList<QUrl> &urls);
    Q_INVOKABLE QVariantMap safeBoxPlan(const QUrl &url) const;
    Q_INVOKABLE void openInSafeBox(const QUrl &url);

    Q_INVOKABLE void answer(const QString &choice, bool applyToAll);
    Q_INVOKABLE void ejectDrive(const QString &udi);

    void ask(const QVariantMap &prompt, std::function<void(const QString &, bool)> reply);

Q_SIGNALS:
    void clipboardChanged();
    void binCountChanged();
    void promptChanged();
    void activeWindowChanged();
    void failed(const QString &message);
    void notice(const QString &message);
    void needsFormat(const QString &udi, const QString &name);
    // Items that just appeared in a folder because of the user, to select
    // (and with rename, to start renaming) once they are listed.
    void created(const QUrl &folder, const QList<QUrl> &urls, bool rename);
    // No app is set for these files; the window shows its app chooser.
    void chooseAppRequested(const QList<QUrl> &urls);

private:
    explicit FileOps(QObject *parent = nullptr);
    KJobUiDelegate *delegate();
    void reportLaunch(KJob *job);
    void askToRun(const QUrl &url);
    void watchJob(KJob *job, const QString &verb);
    void transfer(const QList<QUrl> &urls, const QUrl &folder, bool move);
    QString uniqueName(const QUrl &folder, const QString &base, const QString &suffix) const;
    QUrl childUrl(const QUrl &folder, const QString &name) const;
    void putNew(const QUrl &folder, const QUrl &url, const QByteArray &data, bool rename);
    void updateCut();


    Sharer *m_sharer;
    KCoreDirLister *m_bin;
    int m_binCount = 0;
    QSet<QUrl> m_cut;
    QList<QVariantMap> m_prompts;
    QList<std::function<void(const QString &, bool)>> m_replies;
    QPointer<QObject> m_activeWindow;
};
