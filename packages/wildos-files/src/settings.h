#pragma once

#include <KSharedConfig>
#include <QSize>
#include <QObject>
#include <QQmlEngine>
#include <QUrl>

// View options the user changes from the View menu, kept between sessions
// in wildosfilesrc.
class Settings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool showExtensions READ showExtensions WRITE setShowExtensions NOTIFY showExtensionsChanged)
    Q_PROPERTY(bool showHidden READ showHidden WRITE setShowHidden NOTIFY showHiddenChanged)
    Q_PROPERTY(bool detailsPane READ detailsPane WRITE setDetailsPane NOTIFY detailsPaneChanged)
    Q_PROPERTY(QList<qreal> columnWeights READ columnWeights WRITE setColumnWeights NOTIFY columnWeightsChanged)
    Q_PROPERTY(int sidebarWidth READ sidebarWidth WRITE setSidebarWidth NOTIFY sidebarWidthChanged)
    Q_PROPERTY(bool restoreTabs READ restoreTabs WRITE setRestoreTabs NOTIFY restoreTabsChanged)

public:
    static Settings *instance();
    static Settings *create(QQmlEngine *, QJSEngine *);

    bool showExtensions() const { return m_showExtensions; }
    void setShowExtensions(bool on);
    bool showHidden() const { return m_showHidden; }
    void setShowHidden(bool on);
    bool detailsPane() const { return m_detailsPane; }
    void setDetailsPane(bool on);
    QList<qreal> columnWeights() const { return m_columnWeights; }
    void setColumnWeights(const QList<qreal> &weights);
    // Off unless chosen: Files then opens with the tabs its last window
    // had when it closed.
    bool restoreTabs() const { return m_restoreTabs; }
    void setRestoreTabs(bool on);
    int sidebarWidth() const { return m_sidebarWidth; }
    void setSidebarWidth(int width);
    // The folders pinned to the sidebar, as addresses, in the order pinned.
    QStringList pinnedPlaces() const;
    void setPinnedPlaces(const QStringList &places);

    // "xlarge", "large", "icons" (medium), "small", "list", "details",
    // "tiles" or "content", remembered for each folder the way
    // Windows does, with medium icons for folders never changed.
    Q_INVOKABLE QString viewFor(const QUrl &url) const;
    Q_INVOKABLE void setViewFor(const QUrl &url, const QString &mode);
    // The column a folder is sorted by and which way, remembered for each
    // folder like its layout; by name, A to Z, for folders never changed.
    void sortFor(const QUrl &url, int *column, bool *descending) const;
    void setSortFor(const QUrl &url, int column, bool descending);
    // New windows open at the size the last one was closed at, cut down
    // to the screen if that has become smaller, and maximised if the last
    // one was.
    Q_INVOKABLE QSize windowSize() const;
    Q_INVOKABLE void setWindowSize(const QSize &size);
    Q_INVOKABLE bool windowMaximised() const;
    Q_INVOKABLE void setWindowMaximised(bool on);
    // The tabs to reopen. Given out once, to the first window opened
    // without a folder of its own; later windows start at Home.
    Q_INVOKABLE QList<QUrl> takeSavedTabs();
    Q_INVOKABLE int savedTab() const;
    Q_INVOKABLE void saveTabs(const QList<QUrl> &urls, int current);

Q_SIGNALS:
    void showExtensionsChanged();
    void showHiddenChanged();
    void detailsPaneChanged();
    void columnWeightsChanged();
    void sidebarWidthChanged();
    void restoreTabsChanged();
    void viewsChanged();

private:
    explicit Settings(QObject *parent = nullptr);
    void write(const char *key, const QVariant &value);


    KSharedConfigPtr m_config;
    bool m_showExtensions = false;
    bool m_showHidden = false;
    bool m_detailsPane = false;
    QList<qreal> m_columnWeights;
    int m_sidebarWidth = 196;
    bool m_restoreTabs = false;
    bool m_tabsTaken = false;
};
