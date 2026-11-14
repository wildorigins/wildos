#pragma once

#include <KFileItem>
#include <QAbstractListModel>
#include <QCollator>
#include <QQmlEngine>
#include <QUrl>

#include "dbustypes.h"

class KCoreDirLister;

// The file list of the Open and Save dialog: one folder listed through
// KIO (so network places work as they do in Files), folders first, files
// narrowed to the chosen type, names shown the way Files shows them. A
// second place, This computer, lists the drives.
class DirModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QUrl url READ url NOTIFY urlChanged)
    Q_PROPERTY(QString title READ title NOTIFY urlChanged)
    Q_PROPERTY(QString pathText READ pathText NOTIFY urlChanged)
    Q_PROPERTY(QVariantList crumbs READ crumbs NOTIFY urlChanged)
    Q_PROPERTY(bool isComputer READ isComputer NOTIFY urlChanged)
    Q_PROPERTY(bool writable READ writable NOTIFY urlChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY historyChanged)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY historyChanged)
    Q_PROPERTY(bool canGoUp READ canGoUp NOTIFY urlChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(int sortColumn READ sortColumn WRITE setSortColumn NOTIFY sortChanged)
    Q_PROPERTY(bool sortDescending READ sortDescending WRITE setSortDescending NOTIFY sortChanged)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
    Q_PROPERTY(bool foldersOnly READ foldersOnly WRITE setFoldersOnly NOTIFY filterChanged)
    Q_PROPERTY(bool multiple READ multiple WRITE setMultiple NOTIFY multipleChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        FileNameRole,
        IconRole,
        UrlRole,
        IsDirRole,
        DateRole,
        TypeRole,
        SizeRole,
        SelectedRole,
        HiddenRole,
        PreviewRole,
        StampRole,
    };
    Q_ENUM(Roles)

    explicit DirModel(QObject *parent = nullptr);
    ~DirModel() override;

    static QUrl computerUrl();
    static bool showExtensions();

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QUrl url() const { return m_url; }
    QString title() const;
    QString pathText() const;
    QVariantList crumbs() const;
    bool isComputer() const { return m_url == computerUrl(); }
    bool writable() const;
    bool canGoBack() const { return !m_back.isEmpty(); }
    bool canGoForward() const { return !m_forward.isEmpty(); }
    bool canGoUp() const;
    bool loading() const { return m_loading; }
    QString errorText() const { return m_error; }
    int count() const { return int(m_rows.size()); }
    int selectedCount() const;
    int currentIndex() const { return m_current; }
    void setCurrentIndex(int row);
    int sortColumn() const { return m_sortColumn; }
    void setSortColumn(int column);
    bool sortDescending() const { return m_descending; }
    void setSortDescending(bool descending);
    QString searchText() const { return m_search; }
    void setSearchText(const QString &text);
    bool foldersOnly() const { return m_foldersOnly; }
    void setFoldersOnly(bool on);
    bool multiple() const { return m_multiple; }
    void setMultiple(bool on);

    // Files of this type are listed; the rest are hidden, folders never.
    void setFilter(const Portal::Filter &filter);

    Q_INVOKABLE void openUrl(const QUrl &url);
    // What was typed in the path bar: a path, ~, a place name or a URL.
    Q_INVOKABLE bool openText(const QString &text);
    Q_INVOKABLE void back();
    Q_INVOKABLE void forward();
    Q_INVOKABLE void up();
    Q_INVOKABLE void refresh();

    Q_INVOKABLE void select(int row, int modifiers);
    Q_INVOKABLE void moveCurrent(int row, int modifiers);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE QList<QUrl> selectedUrls() const;
    Q_INVOKABLE QStringList selectedNames() const;
    Q_INVOKABLE QUrl urlAt(int row) const;
    Q_INVOKABLE bool isDirAt(int row) const;
    Q_INVOKABLE QString nameAt(int row) const;
    Q_INVOKABLE QString fileNameAt(int row) const;
    Q_INVOKABLE int rowForFileName(const QString &fileName) const;
    Q_INVOKABLE int findPrefix(const QString &prefix, int from) const;
    Q_INVOKABLE QUrl childUrl(const QString &fileName) const;
    // Makes "New folder" (or "New folder (2)") here and reports it by name
    // through folderMade once it exists, so the list can rename it.
    Q_INVOKABLE void newFolder();
    Q_INVOKABLE bool renameItem(int row, const QString &newName);
    Q_INVOKABLE QString uniqueName(const QString &base, const QString &suffix) const;

Q_SIGNALS:
    void urlChanged();
    void historyChanged();
    void loadingChanged();
    void errorChanged();
    void countChanged();
    void selectionChanged();
    void currentIndexChanged();
    void sortChanged();
    void searchTextChanged();
    void filterChanged();
    void multipleChanged();
    void folderMade(int row);
    void failed(const QString &message);

private:
    struct Entry {
        KFileItem item;
        QUrl url;
        QString fileName;
        QString name;
        QString icon;
        QString date;
        qint64 dateKey = 0;
        QString type;
        QString size;
        qint64 sizeKey = -1;
        bool isDir = false;
        bool hidden = false;
        bool preview = false;
        bool selected = false;
    };

    void navigate(const QUrl &url, bool record);
    Entry makeEntry(const KFileItem &item) const;
    QList<Entry> driveEntries() const;
    bool passes(const Entry &entry) const;
    bool lessThan(const Entry &a, const Entry &b) const;
    void rebuild();
    void setLoading(bool on);
    void setError(const QString &text);
    void notifySelection();

    KCoreDirLister *m_lister;
    QUrl m_url;
    QList<QUrl> m_back;
    QList<QUrl> m_forward;
    QList<Entry> m_all;
    QList<Entry> m_rows;
    QCollator m_collator;
    Portal::Filter m_filter;
    QString m_search;
    QString m_error;
    QString m_madeFolder;
    int m_current = -1;
    int m_anchor = -1;
    int m_sortColumn = 0;
    bool m_descending = false;
    bool m_loading = false;
    bool m_foldersOnly = false;
    bool m_multiple = false;
    bool m_showHidden = false;
};
