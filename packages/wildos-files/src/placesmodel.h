#pragma once

#include <QAbstractListModel>
#include <QQmlEngine>
#include <QUrl>
#include <QVariantMap>

// The sidebar: the Home folders, This computer, plugged-in drives and the
// Recycle Bin, in the order of FilesDetails.dc.html.
class PlacesModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // Goes up whenever the rows change, so bindings that ask rowFor() ask
    // again.
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        IconRole,
        UrlRole,
        KindRole,
        UdiRole,
        RemovableRole,
    };

    static PlacesModel *create(QQmlEngine *, QJSEngine *);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // The sidebar row that stands for a folder, so it can be highlighted.
    Q_INVOKABLE int rowFor(const QUrl &url) const;
    int revision() const { return m_revision; }
    // A row as the sidebar's delegate sees it, for opening a place or its
    // menu from the keyboard.
    Q_INVOKABLE QVariantMap get(int row) const;
    // Your own folders, pinned under the Home folders.
    Q_INVOKABLE bool canPin(const QUrl &url) const;
    Q_INVOKABLE bool isPinned(const QUrl &url) const;
    Q_INVOKABLE void pin(const QUrl &url);
    Q_INVOKABLE void unpin(const QUrl &url);
    Q_INVOKABLE void mount(const QString &udi);

Q_SIGNALS:
    void revisionChanged();

private:
    explicit PlacesModel(QObject *parent = nullptr);
    void reload();

    struct Place {
        QString name;
        QString icon;
        QUrl url;
        QString kind;
        QString udi;
        bool removable = false;
    };
    QList<Place> m_places;
    int m_revision = 0;
};
