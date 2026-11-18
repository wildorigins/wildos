#pragma once

#include <KFileItem>
#include <QAbstractListModel>
#include <QCollator>
#include <KIO/ListJob>
#include <QPointer>
#include <QQmlEngine>
#include <QSet>
#include <QTimer>
#include <QUrl>

class KCoreDirLister;
class KJob;
namespace KIO
{
class Job;
class ListJob;
class UDSEntry;
}

// One tab's worth of Files: the folder it shows, its history, what is
// selected and the search running in it. Folders are listed with KIO, so
// local folders, the Recycle Bin and network places all behave the same way.
class FolderModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QUrl url READ url NOTIFY urlChanged)
    Q_PROPERTY(QString title READ title NOTIFY urlChanged)
    Q_PROPERTY(QString iconName READ iconName NOTIFY urlChanged)
    Q_PROPERTY(QString editText READ editText NOTIFY urlChanged)
    Q_PROPERTY(QVariantList crumbs READ crumbs NOTIFY urlChanged)
    Q_PROPERTY(QString kind READ kind NOTIFY urlChanged)
    Q_PROPERTY(bool writable READ writable NOTIFY urlChanged)
    Q_PROPERTY(QString scope READ scope NOTIFY urlChanged)
    Q_PROPERTY(QString scopeDetail READ scopeDetail NOTIFY urlChanged)
    Q_PROPERTY(QStringList columnTitles READ columnTitles NOTIFY columnsChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorChanged)
    // What the system reported, when errorText is only a general sentence.
    Q_PROPERTY(QString errorDetail READ errorDetail NOTIFY errorChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY historyChanged)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY historyChanged)
    Q_PROPERTY(bool canGoUp READ canGoUp NOTIFY urlChanged)
    Q_PROPERTY(QVariantList recent READ recent NOTIFY historyChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged)
    Q_PROPERTY(QString selectionSize READ selectionSize NOTIFY selectionChanged)
    Q_PROPERTY(int selectionRevision READ selectionRevision NOTIFY selectionChanged)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(int sortColumn READ sortColumn WRITE setSortColumn NOTIFY sortChanged)
    Q_PROPERTY(bool sortDescending READ sortDescending WRITE setSortDescending NOTIFY sortChanged)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
    Q_PROPERTY(bool searching READ searching NOTIFY searchingChanged)
    Q_PROPERTY(QString viewMode READ viewMode WRITE setViewMode NOTIFY viewModeChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        FileNameRole,
        IconRole,
        UrlRole,
        IsDirRole,
        IsDriveRole,
        Col2Role,
        Col3Role,
        SizeRole,
        SelectedRole,
        PreviewRole,
        ModifiedRole,
        FadedRole,
        SubtitleRole,
        UsageRole,
        MimeRole,
        ChipRole,
        RemovableRole,
    };
    Q_ENUM(Roles)

    explicit FolderModel(QObject *parent = nullptr);
    ~FolderModel() override;

    static QUrl computerUrl();
    static QUrl binUrl();

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QUrl url() const { return m_url; }
    QString title() const;
    QString iconName() const;
    QString editText() const;
    QVariantList crumbs() const;
    QString kind() const;
    bool writable() const;
    QString scope() const;
    QString scopeDetail() const;
    QStringList columnTitles() const;
    bool loading() const { return m_loading; }
    QString errorText() const { return m_error; }
    QString errorDetail() const { return m_errorDetail; }
    bool canGoBack() const { return !m_back.isEmpty(); }
    bool canGoForward() const { return !m_forward.isEmpty(); }
    bool canGoUp() const;
    QVariantList recent() const;
    int count() const { return int(m_rows.size()); }
    int selectedCount() const;
    QString selectionSize() const;
    int selectionRevision() const { return m_selectionRevision; }
    int currentIndex() const { return m_current; }
    void setCurrentIndex(int row);
    int sortColumn() const { return m_sortColumn; }
    void setSortColumn(int column);
    bool sortDescending() const { return m_descending; }
    void setSortDescending(bool descending);
    QString searchText() const { return m_searchText; }
    void setSearchText(const QString &text);
    bool searching() const { return m_searchJob != nullptr; }
    QString viewMode() const;
    void setViewMode(const QString &mode);

    Q_INVOKABLE void openUrl(const QUrl &url);
    // Address bar input: a path, ~, a place name or a URL. False when
    // nothing by that name exists.
    Q_INVOKABLE bool openText(const QString &text);
    // The folders a half-typed address could be going to, written the way
    // it is being typed, for the list under the address bar.
    Q_INVOKABLE QStringList completions(const QString &text) const;
    Q_INVOKABLE void back();
    Q_INVOKABLE void forward();
    Q_INVOKABLE void up();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void goToRecent(int index);
    Q_INVOKABLE void activate(int row);
    // Opens a drive that may not be mounted yet, once it is.
    Q_INVOKABLE void openDrive(const QString &udi);

    Q_INVOKABLE void select(int row, int modifiers);
    Q_INVOKABLE void moveCurrent(int row, int modifiers);
    Q_INVOKABLE void selectRows(const QList<int> &rows, bool additive);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void invertSelection();
    Q_INVOKABLE bool isSelected(int row) const;
    Q_INVOKABLE QList<QUrl> selectedUrls() const;
    Q_INVOKABLE bool selectionHasFolders() const;
    Q_INVOKABLE QUrl urlAt(int row) const;
    Q_INVOKABLE bool isDirAt(int row) const;
    Q_INVOKABLE bool isDriveAt(int row) const;
    Q_INVOKABLE QString fileNameAt(int row) const;
    Q_INVOKABLE QString nameAt(int row) const;
    Q_INVOKABLE QString udiAt(int row) const;
    Q_INVOKABLE bool isRemovableAt(int row) const;
    Q_INVOKABLE QString iconAt(int row) const;
    Q_INVOKABLE QString mimeAt(int row) const;
    Q_INVOKABLE int rowForUrl(const QUrl &url) const;
    Q_INVOKABLE int findPrefix(const QString &prefix, int from) const;
    // Selects these as soon as they appear in the listing; with rename set,
    // the first one is opened for renaming, as after New folder.
    Q_INVOKABLE void selectWhenListed(const QList<QUrl> &urls, bool rename);
    Q_INVOKABLE QVariantList subfolders(const QUrl &url) const;

Q_SIGNALS:
    void urlChanged();
    void columnsChanged();
    void loadingChanged();
    void errorChanged();
    void historyChanged();
    void countChanged();
    void selectionChanged();
    void currentIndexChanged();
    void sortChanged();
    void searchTextChanged();
    void searchingChanged();
    void viewModeChanged();
    void renameRequested(int row);
    void openInNewTab(const QUrl &url);
    void failed(const QString &message);

private:
    struct Entry {
        KFileItem item;
        QUrl url;
        QString fileName;
        QString name;
        QString icon;
        QString col2;
        qint64 col2Key = 0;
        QString col3;
        QString sizeText;
        qint64 sizeKey = -1;
        QString subtitle;
        QString udi;
        QString mime;
        QString chip;
        bool removable = false;
        qint64 modified = 0;
        qreal usage = -1;
        bool isDir = false;
        bool isDrive = false;
        bool hidden = false;
        bool preview = false;
        bool selected = false;
    };

    void navigate(const QUrl &url, bool record);
    Entry makeEntry(const KFileItem &item, const QString &relativeDir = QString()) const;
    QList<Entry> driveEntries() const;
    void applyName(Entry &entry) const;
    bool lessThan(const Entry &a, const Entry &b) const;
    bool matchesSearch(const Entry &entry) const;
    void rebuild();
    void insertEntry(Entry entry);
    void removeUrl(const QUrl &url);
    void resetCurrent(const QUrl &url);
    void takePending(int row);
    void notifySelection(int first = -1, int last = -1);
    void startSearch();
    void stopSearch();
    void setLoading(bool on);
    void setError(const QString &text, const QString &detail = QString());

    void onNewItems(const KFileItemList &items);
    void onItemsDeleted(const KFileItemList &items);
    void onRefreshItems(const QList<QPair<KFileItem, KFileItem>> &items);
    void onCompleted();
    void onJobError(KIO::Job *job);
    void onSearchEntries(KIO::Job *job, const QList<KIO::UDSEntry> &entries);

    KCoreDirLister *m_lister;
    QUrl m_url;
    QList<QUrl> m_back;
    QList<QUrl> m_forward;
    QList<Entry> m_base;
    QList<Entry> m_deep;
    QList<Entry> m_rows;
    QCollator m_collator;
    int m_current = -1;
    int m_anchor = -1;
    int m_sortColumn = 0;
    bool m_descending = false;
    bool m_loading = false;
    bool m_listed = false;
    QString m_error;
    QString m_errorDetail;
    QString m_searchText;
    QPointer<KIO::ListJob> m_searchJob;
    QTimer m_searchDelay;
    QSet<QUrl> m_pending;
    QUrl m_pendingRename;
    QString m_pendingUdi;
    int m_selectionRevision = 0;
};
