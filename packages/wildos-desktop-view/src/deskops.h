#pragma once

#include <KIO/AskUserActionInterface>
#include <KIO/OpenWithHandlerInterface>
#include <KIO/UntrustedProgramHandlerInterface>
#include <QObject>
#include <QQmlEngine>
#include <QSet>
#include <QUrl>
#include <QVariantMap>
#include <QWindow>

#include <functional>
#include <utility>

class KCoreDirLister;

class KJob;
class KJobUiDelegate;

// KIO's questions (a name clash on a drop, a file it cannot read, a
// shortcut that starts a program) asked in the desktop's own dialogs rather
// than KDE's widget ones. The same wording as Files.
class DeskAsker : public KIO::AskUserActionInterface
{
    Q_OBJECT

public:
    explicit DeskAsker(QObject *parent);

    void askUserRename(KJob *job, const QString &title, const QUrl &src, const QUrl &dest, KIO::RenameDialog_Options options,
                       KIO::filesize_t sizeSrc, KIO::filesize_t sizeDest, const QDateTime &ctimeSrc, const QDateTime &ctimeDest,
                       const QDateTime &mtimeSrc, const QDateTime &mtimeDest) override;
    void askUserSkip(KJob *job, KIO::SkipDialog_Options options, const QString &errorText) override;
    void askUserDelete(const QList<QUrl> &urls, DeletionType deletionType, ConfirmationType confirmationType, QWidget *parent) override;
    void requestUserMessageBox(MessageDialogType type, const QString &text, const QString &title, const QString &primaryActionText,
                               const QString &secondaryActionText, const QString &primaryActionIconName, const QString &secondaryActionIconName,
                               const QString &dontAskAgainName, const QString &details, QWidget *parent) override;
    void askIgnoreSslErrors(const QVariantMap &sslErrorData, QWidget *parent) override;
};

class DeskOpenWithAsker : public KIO::OpenWithHandlerInterface
{
    Q_OBJECT

public:
    explicit DeskOpenWithAsker(QObject *parent);
    void promptUserForApplication(KJob *job, const QList<QUrl> &urls, const QString &mimeType) override;
};

class DeskUntrustedAsker : public KIO::UntrustedProgramHandlerInterface
{
    Q_OBJECT

public:
    explicit DeskUntrustedAsker(QObject *parent);
    void showUntrustedProgramWarning(KJob *job, const QString &programName) override;
};

// Everything the desktop does to files and to the rest of the system:
// open, copy, move, rename, delete, the New menu, and starting Files,
// Settings and the Terminal. One instance per plasmashell.
class DeskOps : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool canPaste READ canPaste NOTIFY clipboardChanged)
    Q_PROPERTY(int binCount READ binCount NOTIFY binCountChanged)
    Q_PROPERTY(QVariantMap prompt READ prompt NOTIFY promptChanged)
    Q_PROPERTY(bool showExtensions READ showExtensions NOTIFY viewChanged)
    Q_PROPERTY(bool showHidden READ showHidden NOTIFY viewChanged)
    // What Ctrl+Z would take back: "delete", "move", "rename", or "" when
    // there is nothing.
    Q_PROPERTY(QString undoKind READ undoKind NOTIFY undoChanged)

public:
    static DeskOps *instance();
    static DeskOps *create(QQmlEngine *, QJSEngine *);

    bool canPaste() const;
    bool isCut(const QUrl &url) const { return m_cut.contains(url); }
    int binCount() const { return m_binCount; }
    QVariantMap prompt() const { return m_prompts.isEmpty() ? QVariantMap() : m_prompts.constFirst(); }
    // Files' View > Show settings (wildosfilesrc), which the desktop follows
    // as Explorer's do on Windows.
    bool showExtensions() const { return m_showExtensions; }
    bool showHidden() const { return m_showHidden; }
    QString undoKind() const { return m_undoKind; }

    Q_INVOKABLE void open(const QList<QUrl> &urls);
    // Folders, This computer and the Recycle Bin open in Wild OS Files.
    Q_INVOKABLE void openInFiles(const QList<QUrl> &urls);
    Q_INVOKABLE QVariantList appsFor(const QUrl &url) const;
    Q_INVOKABLE QVariantList allApps() const;
    Q_INVOKABLE QString defaultAppName(const QUrl &url) const;
    Q_INVOKABLE void openWith(const QList<QUrl> &urls, const QString &appId, bool makeDefault);
    Q_INVOKABLE void openTerminal(const QUrl &folder);
    Q_INVOKABLE void openSettings(const QString &page);
    // Brings a desktop dialog window to the front with the keyboard.
    // Windows the desktop opens itself do not come from a click in them,
    // so the compositor would otherwise leave them behind the active app.
    Q_INVOKABLE void bringForward(QWindow *window, QWindow *from);

    Q_INVOKABLE void copy(const QList<QUrl> &urls);
    Q_INVOKABLE void cut(const QList<QUrl> &urls);
    Q_INVOKABLE void copyPaths(const QList<QUrl> &urls);
    Q_INVOKABLE void paste(const QUrl &folder);
    Q_INVOKABLE int dropAction(const QList<QUrl> &urls, const QUrl &folder, int modifiers) const;
    Q_INVOKABLE void drop(const QList<QUrl> &urls, const QUrl &folder, int action);

    Q_INVOKABLE void trash(const QList<QUrl> &urls);
    Q_INVOKABLE void deleteForGood(const QList<QUrl> &urls);
    Q_INVOKABLE void emptyBin();
    // Takes back the last delete to the Recycle Bin, move or rename. Only
    // the last one is kept, and anything else done to files since (a copy,
    // a new folder) forgets it, so Ctrl+Z never reaches back past what the
    // person just did.
    Q_INVOKABLE void undo();

    Q_INVOKABLE QString editableName(const QString &fileName, bool isDir) const;
    Q_INVOKABLE void rename(const QUrl &url, const QString &fileName, bool isDir, const QString &typed);
    Q_INVOKABLE void newFolder(const QUrl &folder);
    // The New menu, the same list and names as Files (see newitems.cpp).
    Q_INVOKABLE QVariantList newFileTypes() const;
    Q_INVOKABLE void newFile(const QUrl &folder, const QString &id);
    Q_INVOKABLE QVariantMap checkShortcut(const QString &target) const;
    Q_INVOKABLE void newShortcut(const QUrl &folder, const QString &target, const QString &name);

    Q_INVOKABLE void answer(const QString &choice, bool applyToAll);

    void ask(const QVariantMap &prompt, std::function<void(const QString &, bool)> reply);

Q_SIGNALS:
    void clipboardChanged();
    void binCountChanged();
    void promptChanged();
    void viewChanged();
    void undoChanged();
    void failed(const QString &message);
    // Items that just appeared in a folder because of the user, to select
    // (and with rename, to start renaming) once they are listed.
    void created(const QUrl &folder, const QList<QUrl> &urls, bool rename);
    void chooseAppRequested(const QList<QUrl> &urls);

private:
    explicit DeskOps(QObject *parent = nullptr);
    KJobUiDelegate *delegate();
    void reportLaunch(KJob *job);
    void askToRun(const QUrl &url);
    void watchJob(KJob *job, const QString &verb);
    void transfer(const QList<QUrl> &urls, const QUrl &folder, bool move);
    QString uniqueName(const QUrl &folder, const QString &base, const QString &suffix) const;
    QUrl childUrl(const QUrl &folder, const QString &name) const;
    void putNew(const QUrl &folder, const QUrl &url, const QByteArray &data, bool rename);
    void updateCut();
    void readView();
    void setUndo(const QString &kind, const QList<std::pair<QUrl, QUrl>> &steps, const QString &name = QString());
    void clearUndo();

    KCoreDirLister *m_bin;
    int m_binCount = 0;
    QSet<QUrl> m_cut;
    QList<QVariantMap> m_prompts;
    QList<std::function<void(const QString &, bool)>> m_replies;
    bool m_showExtensions = false;
    bool m_showHidden = false;
    QString m_undoKind;
    // Each item as (where it is now, where it was).
    QList<std::pair<QUrl, QUrl>> m_undoSteps;
    // A renamed shortcut keeps its file and changes the name inside it.
    QString m_undoName;
};
