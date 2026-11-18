#pragma once

#include "entrymodel.h"
#include "progress.h"
#include "reader.h"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <functional>
#include <memory>

// One open archive, the one a window shows. Reading, extracting, testing
// and the thumbnails all run on worker threads, so a big archive never
// stops the window; the window follows their progress on a timer.
class Archive : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString path READ path NOTIFY loaded)
    Q_PROPERTY(QString fileName READ fileName NOTIFY loaded)
    Q_PROPERTY(QString baseName READ baseName NOTIFY loaded)
    // "empty", "loading", "ready" or "failed".
    Q_PROPERTY(QString state READ state NOTIFY loaded)
    Q_PROPERTY(QString errorText READ errorText NOTIFY loaded)
    Q_PROPERTY(QString formatName READ formatName NOTIFY loaded)
    Q_PROPERTY(QString summary READ summary NOTIFY loaded)
    Q_PROPERTY(bool encrypted READ encrypted NOTIFY loaded)
    // Protected in a way Archive cannot decrypt: RAR 4 encryption.
    Q_PROPERTY(bool lockedFormat READ lockedFormat NOTIFY loaded)
    // The names are behind the password too, and it hasn't been given.
    Q_PROPERTY(bool namesHidden READ namesHidden NOTIFY loaded)
    Q_PROPERTY(bool hasPassword READ hasPassword NOTIFY passwordChanged)
    Q_PROPERTY(int unsafeCount READ unsafeCount NOTIFY loaded)
    Q_PROPERTY(EntryModel *model READ model CONSTANT)

    Q_PROPERTY(QString folder READ folder NOTIFY viewChanged)
    Q_PROPERTY(QVariantList crumbs READ crumbs NOTIFY viewChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY viewChanged)
    Q_PROPERTY(bool canGoUp READ canGoUp NOTIFY viewChanged)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY viewChanged)
    Q_PROPERTY(int itemCount READ itemCount NOTIFY viewChanged)
    Q_PROPERTY(QString sortKey READ sortKey NOTIFY viewChanged)
    Q_PROPERTY(bool sortAscending READ sortAscending NOTIFY viewChanged)

    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged)
    Q_PROPERTY(QString paneTitle READ paneTitle NOTIFY selectionChanged)
    Q_PROPERTY(QString paneSize READ paneSize NOTIFY selectionChanged)
    Q_PROPERTY(QStringList paneThumbs READ paneThumbs NOTIFY selectionChanged)

    Q_PROPERTY(QString destination READ destination WRITE setDestination NOTIFY destinationChanged)
    Q_PROPERTY(QString destinationText READ destinationText NOTIFY destinationChanged)
    Q_PROPERTY(bool keepStructure READ keepStructure WRITE setKeepStructure NOTIFY optionsChanged)
    Q_PROPERTY(bool openWhenDone READ openWhenDone WRITE setOpenWhenDone NOTIFY optionsChanged)
    Q_PROPERTY(bool replaceExisting READ replaceExisting WRITE setReplaceExisting NOTIFY optionsChanged)

    // "", "list", "password", "extract", "test" or "open".
    Q_PROPERTY(QString job READ job NOTIFY jobChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY jobChanged)
    Q_PROPERTY(double jobProgress READ jobProgress NOTIFY jobProgressChanged)
    Q_PROPERTY(QString jobText READ jobText NOTIFY jobProgressChanged)

public:
    explicit Archive(QObject *parent = nullptr);
    ~Archive() override;

    QString path() const
    {
        return m_path;
    }
    QString fileName() const;
    QString baseName() const;
    QString state() const
    {
        return m_state;
    }
    QString errorText() const
    {
        return m_error;
    }
    QString formatName() const;
    QString summary() const;
    bool encrypted() const;
    bool lockedFormat() const;
    bool namesHidden() const;
    bool hasPassword() const
    {
        return !m_password.isEmpty();
    }
    int unsafeCount() const;
    EntryModel *model() const
    {
        return m_model;
    }

    QString folder() const
    {
        return m_folder;
    }
    QVariantList crumbs() const;
    bool canGoBack() const
    {
        return !m_history.isEmpty();
    }
    bool canGoUp() const
    {
        return !m_folder.isEmpty() || !m_search.isEmpty();
    }
    QString search() const
    {
        return m_search;
    }
    void setSearch(const QString &text);
    int itemCount() const
    {
        return m_model->rowCount();
    }
    QString sortKey() const
    {
        return m_sortKey;
    }
    bool sortAscending() const
    {
        return m_sortAscending;
    }

    int selectedCount() const
    {
        return int(m_model->checked().size());
    }
    QString paneTitle() const
    {
        return m_paneTitle;
    }
    QString paneSize() const
    {
        return m_paneSize;
    }
    QStringList paneThumbs() const
    {
        return m_paneThumbs;
    }

    QString destination() const
    {
        return m_destination;
    }
    void setDestination(const QString &path);
    QString destinationText() const;
    bool keepStructure() const
    {
        return m_keepStructure;
    }
    void setKeepStructure(bool on);
    bool openWhenDone() const
    {
        return m_openWhenDone;
    }
    void setOpenWhenDone(bool on);
    bool replaceExisting() const
    {
        return m_replace;
    }
    void setReplaceExisting(bool on);

    QString job() const
    {
        return m_job;
    }
    bool busy() const
    {
        return !m_job.isEmpty() && m_job != QLatin1String("password");
    }
    double jobProgress() const;
    QString jobText() const;

    // For a new archive made from this one with Add files.
    QByteArray password() const
    {
        return m_password;
    }

    Q_INVOKABLE void open(const QString &path);
    Q_INVOKABLE void openUrl(const QUrl &url);
    Q_INVOKABLE void setPassword(const QString &password);
    Q_INVOKABLE void forgetPending();
    Q_INVOKABLE void askPassword();

    Q_INVOKABLE void goTo(const QString &folder);
    Q_INVOKABLE void up();
    Q_INVOKABLE void back();
    Q_INVOKABLE void activate(int row);
    Q_INVOKABLE void sortBy(const QString &key);

    Q_INVOKABLE void toggle(int row);
    Q_INVOKABLE void select(int row, int modifiers);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();

    Q_INVOKABLE void setDestinationUrl(const QUrl &url);
    Q_INVOKABLE QUrl destinationUrl() const;
    Q_INVOKABLE QUrl folderUrl() const;

    // Everything, or what is ticked (everything when nothing is).
    Q_INVOKABLE void extract(bool everything);
    // Files' Extract here: next to the archive, in a folder named after it,
    // or straight in when the archive holds a single folder or file. When
    // that folder is already there it asks first (alreadyThere), and is
    // called again with "replace" or "both".
    Q_INVOKABLE void extractHere(const QString &whenThere = QString());
    // Dragging rows out to Files or the desktop: the row dragged, with the
    // other ticked rows when it is one of them, is extracted to a private
    // folder Archive clears when it next starts, and dragReady hands the
    // window the files to drag.
    Q_INVOKABLE void extractForDrag(int row);
    Q_INVOKABLE void test();
    Q_INVOKABLE void cancel();

Q_SIGNALS:
    void loaded();
    void viewChanged();
    void selectionChanged();
    void destinationChanged();
    void optionsChanged();
    void jobChanged();
    void jobProgressChanged();
    void passwordChanged();
    void passwordNeeded(bool wrong);
    void alreadyThere(const QString &name);
    void dragReady(const QStringList &urls);
    // What came of a job, for the window to say: a short note, or a title
    // with a message and the files concerned.
    void finished(const QString &job, bool ok, const QString &title, const QString &message, const QStringList &details);

private:
    using Done = std::function<void()>;
    void start(const QString &job, std::function<Done(Progress *)> work);
    void startThumbnails();
    void stopAll();
    void show(const QString &folder, bool remember);
    void refreshView();
    void updatePane();
    QList<int> filesUnder(const QList<int> &entries) const;
    void collectOrders(int entry, QSet<int> *orders) const;
    void runExtract(Reader::ExtractOptions options, const QString &what, bool openAfter);
    void openEntry(int entry);
    bool needPassword(const Done &retry);
    void adopt(const std::shared_ptr<const Listing> &listing);
    double rate() const;

    EntryModel *m_model;
    QString m_path;
    QString m_state = QStringLiteral("empty");
    QString m_error;
    std::shared_ptr<const Listing> m_listing;
    QHash<QString, int> m_dirs;
    QString m_key;
    QByteArray m_password;
    Done m_pending;

    QString m_folder;
    QStringList m_history;
    QString m_search;
    QString m_sortKey;
    bool m_sortAscending = true;
    int m_anchor = -1;

    QString m_paneTitle;
    QString m_paneSize;
    QStringList m_paneThumbs;

    QString m_destination;
    bool m_destinationChosen = false;
    bool m_keepStructure = true;
    bool m_openWhenDone = true;
    bool m_replace = false;

    QString m_job;
    std::shared_ptr<Progress> m_progress;
    std::shared_ptr<Progress> m_thumbProgress;
    QList<QPointer<QThread>> m_threads;
    QTimer m_ticker;
    int m_generation = 0;
};
