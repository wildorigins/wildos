#include "accessibility.h"

#include "accesscore.h"

#include <KConfig>
#include <KConfigGroup>
#include <KSharedConfig>

#include <QDBusConnection>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

Accessibility::Accessibility(QObject *parent)
    : QObject(parent)
{
    m_pointerColours = AccessCore::pointerColours();
    m_screenReaderInstalled = AccessCore::screenReaderInstalled();
    m_highContrastInstalled =
        !QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("color-schemes/%1.colors").arg(AccessCore::highContrastScheme())).isEmpty();
    m_monoAudioInstalled = QFile::exists(QStringLiteral("/usr/lib/systemd/user/wildos-mono-audio.service"));
    m_keyboardInstalled = !AccessCore::keyboardDesktopFile().isEmpty();
    m_windowEffectsInstalled = AccessCore::windowEffectsInstalled();
    m_windowEffectsSupported = m_windowEffectsInstalled && AccessCore::windowEffectsSupported();

    // Shortcuts, kcm_access, the Plasma keyboard indicator or another
    // Settings window can change any of these while the page is open.
    watch(QStringLiteral("kaccessrc"));
    watch(QStringLiteral("kdeglobals"));
    watch(QStringLiteral("kcminputrc"));
    watch(QStringLiteral("kwinrc"));
    watch(QStringLiteral("wildosrc"));
    QDBusConnection::sessionBus().connect(QStringLiteral("org.kde.KWin"), QStringLiteral("/VirtualKeyboard"), QStringLiteral("org.kde.kwin.VirtualKeyboard"),
                                          QStringLiteral("enabledChanged"), this, SLOT(refresh()));

    m_monoAudio = AccessCore::monoAudio();
    readCustomPointer();
    refresh();
    refreshMagnifier();
}

void Accessibility::watch(const QString &file)
{
    KConfigWatcher::Ptr watcher = KConfigWatcher::create(KSharedConfig::openConfig(file, KConfig::NoGlobals));
    if (watcher) {
        connect(watcher.data(), &KConfigWatcher::configChanged, this, &Accessibility::refresh);
        m_watchers.append(watcher);
    }
}

QList<int> Accessibility::pointerSizes() const
{
    return AccessCore::pointerSizes();
}

void Accessibility::refresh()
{
    m_pointerColour = AccessCore::pointerColour();
    m_pointerSize = AccessCore::pointerSize();
    m_screenReader = AccessCore::screenReader();
    if (!m_highContrastBusy) {
        m_highContrast = AccessCore::highContrast();
    }
    m_visibleBell = AccessCore::visibleBell();
    m_onScreenKeyboard = AccessCore::onScreenKeyboard();
    m_stickyKeys = AccessCore::stickyKeys();
    m_slowKeys = AccessCore::slowKeys();
    m_slowKeysDelay = AccessCore::slowKeysDelay();
    m_bounceKeys = AccessCore::bounceKeys();
    m_bounceKeysDelay = AccessCore::bounceKeysDelay();
    m_outlineWindows = AccessCore::outlineWindows();
    m_outlineFillingScreen = AccessCore::outlineFillingScreen();
    m_blurBackground = AccessCore::blurBackground();
    m_blurStrength = AccessCore::blurStrength();
    m_blurDesktop = AccessCore::blurDesktop();
    m_strongerText = AccessCore::strongerText();
    Q_EMIT changed();
}

void Accessibility::refreshMagnifier()
{
    const bool on = AccessCore::magnifier();
    if (on != m_magnifier) {
        m_magnifier = on;
        Q_EMIT magnifierChanged();
    }
}

void Accessibility::setPointerColour(const QString &id)
{
    AccessCore::setPointer(id, m_pointerSize);
    m_pointerColour = id;
    Q_EMIT changed();
}

void Accessibility::setPointerSize(int size)
{
    AccessCore::setPointer(m_pointerColour, size);
    m_pointerSize = size;
    Q_EMIT changed();
}

void Accessibility::readCustomPointer()
{
    const QString dir = AccessCore::customPointerDir();
    m_customPointer = QFileInfo::exists(dir + QStringLiteral("/index.theme"));
    m_customPreview.clear();
    m_customSource.clear();
    if (m_customPointer) {
        const KConfig index(dir + QStringLiteral("/index.theme"), KConfig::SimpleConfig);
        const KConfigGroup group = index.group(QStringLiteral("Wild OS"));
        m_customSource = group.readEntry("Source", QString());
        // The stamp changes with every new pointer, so the swatch's picture
        // is read again rather than taken from the image cache.
        m_customPreview = QUrl::fromLocalFile(dir + QStringLiteral("/preview.png"));
        m_customPreview.setFragment(group.readEntry("Stamp", QString()));
    }
    Q_EMIT customPointerChanged();
}

QString Accessibility::x11Link()
{
    return QDir::homePath() + QStringLiteral("/.icons/") + AccessCore::customPointerTheme();
}

// X11 apps load pointers through libXcursor, whose Debian build looks in
// ~/.icons and /usr/share/icons but not ~/.local/share/icons, where KWin,
// GTK and Qt on Wayland find the theme. A link puts it where both look.
void Accessibility::linkForX11()
{
    const QFileInfo link(x11Link());
    if (link.exists() || link.isSymLink()) {
        return;
    }
    QDir().mkpath(link.path());
    QFile::link(AccessCore::customPointerDir(), link.filePath());
}

void Accessibility::addPointer(const QUrl &file, qreal hotX, qreal hotY)
{
    if (m_pointerBusy || !file.isLocalFile()) {
        return;
    }
    const QString dir = AccessCore::customPointerDir();
    QDir().mkpath(QFileInfo(dir).path());
    m_pointerBusy = true;
    Q_EMIT customPointerChanged();

    // A separate process reads the file, so a damaged or hostile one can at
    // worst end that process, never Settings.
    auto *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    auto *timeout = new QTimer(process);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, process, &QProcess::kill);
    connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
        process->deleteLater();
        m_pointerBusy = false;
        readCustomPointer();
        if (status == QProcess::NormalExit && code == 0) {
            linkForX11();
            if (m_pointerColour == QLatin1String("custom")) {
                // KWin only reloads the pointer when its theme's name
                // changes, so it goes through white to pick up the new one.
                AccessCore::setPointer(QStringLiteral("white"), m_pointerSize);
                QTimer::singleShot(400, this, [this] {
                    AccessCore::setPointer(QStringLiteral("custom"), m_pointerSize);
                    refresh();
                });
            } else {
                setPointerColour(QStringLiteral("custom"));
            }
        } else if (status == QProcess::NormalExit && code == 1) {
            Q_EMIT pointerFailed(QStringLiteral("Wild OS couldn't save the pointer. Check there's space left on the disk."));
        } else {
            Q_EMIT pointerFailed(QStringLiteral("That file isn't a pointer Wild OS can use."));
        }
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            process->deleteLater();
            m_pointerBusy = false;
            Q_EMIT customPointerChanged();
            Q_EMIT pointerFailed(QStringLiteral("That file isn't a pointer Wild OS can use."));
        }
    });
    process->start(QStringLiteral("/usr/libexec/wildos/pointer-import"),
                   {QStringLiteral("convert"), file.toLocalFile(), dir, QString::number(qBound(0.0, hotX, 1.0)), QString::number(qBound(0.0, hotY, 1.0))});
    timeout->start(90000);
}

void Accessibility::removeCustomPointer()
{
    if (m_pointerColour == QLatin1String("custom")) {
        setPointerColour(QStringLiteral("white"));
    }
    QDir(AccessCore::customPointerDir()).removeRecursively();
    const QFileInfo link(x11Link());
    if (link.isSymLink()) {
        QFile::remove(link.filePath());
    }
    readCustomPointer();
}

void Accessibility::setMagnifier(bool on)
{
    AccessCore::setMagnifier(on);
    m_magnifier = on;
    Q_EMIT magnifierChanged();
}

void Accessibility::setScreenReader(bool on)
{
    AccessCore::setScreenReader(on);
    m_screenReader = on;
    Q_EMIT changed();
}

void Accessibility::setHighContrast(bool on)
{
    if (m_highContrastBusy) {
        return;
    }
    m_highContrastBusy = true;
    m_highContrast = on;
    Q_EMIT changed();
    AccessCore::setHighContrast(on, this, [this] {
        m_highContrastBusy = false;
        refresh();
    });
}

void Accessibility::setVisibleBell(bool on)
{
    AccessCore::setVisibleBell(on);
    m_visibleBell = on;
    Q_EMIT changed();
}

void Accessibility::setMonoAudio(bool on)
{
    AccessCore::setMonoAudio(on);
    m_monoAudio = on;
    Q_EMIT changed();
}

void Accessibility::setOnScreenKeyboard(bool on)
{
    m_onScreenKeyboard = on;
    Q_EMIT changed();
    AccessCore::setOnScreenKeyboard(on, this, [this] {
        refresh();
    });
}

void Accessibility::setStickyKeys(bool on)
{
    AccessCore::setStickyKeys(on);
    m_stickyKeys = on;
    Q_EMIT changed();
}

void Accessibility::setSlowKeys(bool on)
{
    AccessCore::setSlowKeys(on);
    m_slowKeys = on;
    Q_EMIT changed();
}

void Accessibility::setSlowKeysDelay(int ms)
{
    AccessCore::setSlowKeysDelay(ms);
    m_slowKeysDelay = ms;
    Q_EMIT changed();
}

void Accessibility::setBounceKeys(bool on)
{
    AccessCore::setBounceKeys(on);
    m_bounceKeys = on;
    Q_EMIT changed();
}

void Accessibility::setBounceKeysDelay(int ms)
{
    AccessCore::setBounceKeysDelay(ms);
    m_bounceKeysDelay = ms;
    Q_EMIT changed();
}

void Accessibility::setOutlineWindows(bool on)
{
    AccessCore::setOutlineWindows(on);
    m_outlineWindows = on;
    Q_EMIT changed();
}

void Accessibility::setOutlineFillingScreen(bool on)
{
    AccessCore::setOutlineFillingScreen(on);
    m_outlineFillingScreen = on;
    Q_EMIT changed();
}

void Accessibility::setBlurBackground(bool on)
{
    AccessCore::setBlurBackground(on);
    m_blurBackground = on;
    Q_EMIT changed();
}

void Accessibility::setBlurStrength(int step)
{
    AccessCore::setBlurStrength(step);
    m_blurStrength = AccessCore::blurStrength();
    Q_EMIT changed();
}

void Accessibility::setBlurDesktop(bool on)
{
    AccessCore::setBlurDesktop(on);
    m_blurDesktop = on;
    Q_EMIT changed();
}

void Accessibility::setStrongerText(bool on)
{
    AccessCore::setStrongerText(on);
    m_strongerText = on;
    Q_EMIT changed();
}
