#include "settings.h"

#include <KConfigGroup>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QScreen>
#include <QSize>
#include <QStandardPaths>

#include <algorithm>

namespace
{
const QList<qreal> designWeights = {2.4, 1.4, 1.1, 0.7};
}

Settings *Settings::instance()
{
    static Settings *settings = new Settings;
    return settings;
}

Settings *Settings::create(QQmlEngine *, QJSEngine *)
{
    auto settings = instance();
    QJSEngine::setObjectOwnership(settings, QJSEngine::CppOwnership);
    return settings;
}

Settings::Settings(QObject *parent)
    : QObject(parent)
    , m_config(KSharedConfig::openConfig(QStringLiteral("wildosfilesrc")))
{
    const KConfigGroup group(m_config, QStringLiteral("View"));
    m_showExtensions = group.readEntry("ShowExtensions", false);
    m_showHidden = group.readEntry("ShowHidden", false);
    m_detailsPane = group.readEntry("DetailsPane", false);
    m_columnWeights = group.readEntry("ColumnWeights", designWeights);
    if (m_columnWeights.size() != designWeights.size()) {
        m_columnWeights = designWeights;
    }
    m_sidebarWidth = std::clamp(group.readEntry("SidebarWidth", 196), 140, 420);
    m_restoreTabs = group.readEntry("RestoreTabs", false);
}

void Settings::setRestoreTabs(bool on)
{
    if (on == m_restoreTabs) {
        return;
    }
    m_restoreTabs = on;
    write("RestoreTabs", on);
    if (!on) {
        // Nothing is kept about the last window once this is off.
        m_config->deleteGroup(QStringLiteral("Session"));
        m_config->sync();
    }
    Q_EMIT restoreTabsChanged();
}

QList<QUrl> Settings::takeSavedTabs()
{
    if (!m_restoreTabs || m_tabsTaken) {
        return {};
    }
    m_tabsTaken = true;
    const KConfigGroup group(m_config, QStringLiteral("Session"));
    const QStringList saved = group.readEntry("Tabs", QStringList());
    QList<QUrl> urls;
    for (const QString &address : saved) {
        const QUrl url(address);
        // A folder deleted since would only open as an error.
        if (url.isValid() && (!url.isLocalFile() || QFileInfo(url.toLocalFile()).isDir())) {
            urls.append(url);
        }
    }
    return urls;
}

int Settings::savedTab() const
{
    const KConfigGroup group(m_config, QStringLiteral("Session"));
    return group.readEntry("CurrentTab", 0);
}

void Settings::saveTabs(const QList<QUrl> &urls, int current)
{
    if (!m_restoreTabs) {
        return;
    }
    QStringList addresses;
    for (const QUrl &url : urls) {
        addresses.append(url.toString());
    }
    KConfigGroup group(m_config, QStringLiteral("Session"));
    group.writeEntry("Tabs", addresses);
    group.writeEntry("CurrentTab", current);
    m_config->sync();
}

void Settings::setSidebarWidth(int width)
{
    width = std::clamp(width, 140, 420);
    if (width != m_sidebarWidth) {
        m_sidebarWidth = width;
        write("SidebarWidth", width);
        Q_EMIT sidebarWidthChanged();
    }
}

QStringList Settings::pinnedPlaces() const
{
    const KConfigGroup group(m_config, QStringLiteral("Sidebar"));
    return group.readEntry("Pinned", QStringList());
}

void Settings::setPinnedPlaces(const QStringList &places)
{
    KConfigGroup group(m_config, QStringLiteral("Sidebar"));
    group.writeEntry("Pinned", places);
    m_config->sync();
}

void Settings::write(const char *key, const QVariant &value)
{
    KConfigGroup group(m_config, QStringLiteral("View"));
    group.writeEntry(key, value);
    m_config->sync();
}

void Settings::setShowExtensions(bool on)
{
    if (on != m_showExtensions) {
        m_showExtensions = on;
        write("ShowExtensions", on);
        Q_EMIT showExtensionsChanged();
    }
}

void Settings::setShowHidden(bool on)
{
    if (on != m_showHidden) {
        m_showHidden = on;
        write("ShowHidden", on);
        Q_EMIT showHiddenChanged();
    }
}

void Settings::setDetailsPane(bool on)
{
    if (on != m_detailsPane) {
        m_detailsPane = on;
        write("DetailsPane", on);
        Q_EMIT detailsPaneChanged();
    }
}

void Settings::setColumnWeights(const QList<qreal> &weights)
{
    if (weights.size() == designWeights.size() && weights != m_columnWeights) {
        m_columnWeights = weights;
        KConfigGroup group(m_config, QStringLiteral("View"));
        group.writeEntry("ColumnWeights", weights);
        m_config->sync();
        Q_EMIT columnWeightsChanged();
    }
}

QString Settings::viewFor(const QUrl &url) const
{
    const KConfigGroup group(m_config, QStringLiteral("FolderViews"));
    // Photo and video folders open with large icons, as they do on
    // Windows; everything else starts in the design's medium icons.
    QString fallback = QStringLiteral("icons");
    // This computer lists its drives as tiles with a space bar, as on
    // Windows.
    if (url.scheme() == QLatin1String("wildos")) {
        fallback = QStringLiteral("tiles");
    }
    if (url.isLocalFile()) {
        const QString path = QDir::cleanPath(url.toLocalFile());
        for (auto location : {QStandardPaths::PicturesLocation, QStandardPaths::MoviesLocation}) {
            const QString media = QStandardPaths::writableLocation(location);
            if (!media.isEmpty() && media != QDir::homePath() && (path == media || path.startsWith(media + QLatin1Char('/')))) {
                fallback = QStringLiteral("large");
            }
        }
    }
    return group.readEntry(url.toString(QUrl::StripTrailingSlash), fallback);
}

void Settings::sortFor(const QUrl &url, int *column, bool *descending) const
{
    *column = 0;
    *descending = false;
    const KConfigGroup group(m_config, QStringLiteral("FolderSorts"));
    const QStringList saved = group.readEntry(url.toString(QUrl::StripTrailingSlash), QString()).split(QLatin1Char(','));
    if (saved.size() == 2) {
        *column = std::clamp(saved.at(0).toInt(), 0, 3);
        *descending = saved.at(1) == QLatin1String("1");
    }
}

void Settings::setSortFor(const QUrl &url, int column, bool descending)
{
    KConfigGroup group(m_config, QStringLiteral("FolderSorts"));
    const QString key = url.toString(QUrl::StripTrailingSlash);
    if (column == 0 && !descending) {
        group.deleteEntry(key);
    } else {
        group.writeEntry(key, QStringLiteral("%1,%2").arg(column).arg(descending ? 1 : 0));
    }
    m_config->sync();
}

QSize Settings::windowSize() const
{
    const KConfigGroup group(m_config, QStringLiteral("Window"));
    QSize size = group.readEntry("Size", QSize(980, 620));
    // A size saved on a large monitor would hang off a laptop's screen.
    if (const QScreen *screen = QGuiApplication::primaryScreen()) {
        size = size.boundedTo(screen->availableSize());
    }
    return size;
}

bool Settings::windowMaximised() const
{
    const KConfigGroup group(m_config, QStringLiteral("Window"));
    return group.readEntry("Maximised", false);
}

void Settings::setWindowMaximised(bool on)
{
    KConfigGroup group(m_config, QStringLiteral("Window"));
    group.writeEntry("Maximised", on);
    m_config->sync();
}

void Settings::setWindowSize(const QSize &size)
{
    KConfigGroup group(m_config, QStringLiteral("Window"));
    group.writeEntry("Size", size);
    m_config->sync();
}

void Settings::setViewFor(const QUrl &url, const QString &mode)
{
    KConfigGroup group(m_config, QStringLiteral("FolderViews"));
    group.writeEntry(url.toString(QUrl::StripTrailingSlash), mode);
    m_config->sync();
    Q_EMIT viewsChanged();
}
