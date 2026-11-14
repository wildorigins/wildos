#include "captureapp.h"

#include "frozenimage.h"
#include "grabber.h"
#include "recorder.h"
#include "sharer.h"

#include <KConfigGroup>
#include <KNotification>
#include <KSharedConfig>
#include <KSystemClipboard>

#include <QBuffer>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QStandardPaths>

namespace
{
QScreen *screenNamed(const QString &name)
{
    for (QScreen *screen : QGuiApplication::screens()) {
        if (screen->name() == name) {
            return screen;
        }
    }
    return QGuiApplication::primaryScreen();
}

// The settings file keeps the name it had as Screenshot, so the mode and
// delay people picked carry over.
KConfigGroup settings()
{
    return KSharedConfig::openConfig(QStringLiteral("wildosscreenshotrc"))->group(QStringLiteral("General"));
}

void putOnClipboard(const QImage &image)
{
    auto *mime = new QMimeData;
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    mime->setData(QStringLiteral("image/png"), png);
    mime->setImageData(image);
    KSystemClipboard::instance()->setMimeData(mime, QClipboard::Clipboard);
}

QImage cropped(const QVariant &value, const QRectF &crop)
{
    QImage image = value.value<QImage>();
    if (image.isNull() || crop.width() < 1 || crop.height() < 1) {
        return image;
    }
    const QRect area = crop.toAlignedRect().intersected(image.rect());
    return area.isEmpty() ? image : image.copy(area);
}
}

CaptureApp::CaptureApp(QObject *parent)
    : QObject(parent)
    , m_frozen(new FrozenImages)
    , m_grabber(new Grabber(this))
    , m_recorder(new Recorder(this))
    , m_sharer(new Sharer(this))
    , m_scanner(new Scanner(this))
{
    const KConfigGroup config = settings();
    m_delay = config.readEntry("Delay", 0);
    const QString mode = config.readEntry("Mode", QStringLiteral("rectangle"));
    if (mode == QLatin1String("window") || mode == QLatin1String("screen") || mode == QLatin1String("freehand")) {
        m_mode = mode;
    }
    m_openEditor = config.readEntry("OpenEditor", true);
    m_autoSave = config.readEntry("AutoSave", true);
    m_includePointer = config.readEntry("IncludePointer", false);

    connect(m_grabber, &Grabber::windowsArrived, this, [this](const QJsonArray &windows) {
        m_windows = windows.toVariantList();
        Q_EMIT windowsChanged();
    });

    m_countdownTimer.setInterval(1000);
    connect(&m_countdownTimer, &QTimer::timeout, this, [this] {
        m_countdown -= 1;
        Q_EMIT countdownChanged();
        if (m_countdown > 0) {
            return;
        }
        m_countdownTimer.stop();
        // One frame for the countdown to go before the screen is read again.
        QTimer::singleShot(150, this, [this] {
            auto action = std::move(m_pending);
            m_pending = nullptr;
            if (!m_recordMode) {
                const auto images = m_grabber->grabScreens(m_includePointer);
                if (!images.isEmpty()) {
                    m_frozen->set(images);
                }
            }
            if (action) {
                action();
            }
        });
    });

    connect(m_recorder, &Recorder::finished, this, &CaptureApp::recordingFinished);
    connect(m_recorder, &Recorder::failed, this, [this](const QString &message) {
        notifyFailure(QStringLiteral("Recording stopped"), message);
        quitWhenIdle();
    });
    connect(m_recorder, &Recorder::recordingChanged, this, &CaptureApp::quitWhenIdle);
    connect(m_sharer, &Sharer::finished, this, [this](const QString &message) {
        setMessage(message);
    });
    connect(m_sharer, &Sharer::failed, this, [this](const QString &message) {
        setMessage(message);
    });

    m_idleTimer.setSingleShot(true);
    m_idleTimer.setInterval(800);
    connect(&m_idleTimer, &QTimer::timeout, this, [this] {
        if (m_selecting || m_countdown > 0 || m_recorder->recording() || m_openWindows > 0 || m_scanner->scanning() || m_scanner->busy()) {
            return;
        }
        for (const auto &notification : std::as_const(m_notifications)) {
            if (notification) {
                return;
            }
        }
        QGuiApplication::quit();
    });
}

CaptureApp::~CaptureApp() = default;

QString CaptureApp::primaryScreen() const
{
    return QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->name() : QString();
}

bool CaptureApp::canRecord() const
{
    return m_recorder->available();
}

void CaptureApp::setMode(const QString &mode)
{
    if (m_mode == mode) {
        return;
    }
    m_mode = mode;
    clearPick();
    settings().writeEntry("Mode", mode);
    settings().sync();
    Q_EMIT modeChanged();
}

void CaptureApp::setRecordMode(bool on)
{
    if (m_recordMode == on) {
        return;
    }
    m_recordMode = on;
    // A freeform shape cannot be recorded, so it gives way to an area.
    if (on && m_mode == QLatin1String("freehand")) {
        m_mode = QStringLiteral("rectangle");
        clearPick();
    }
    Q_EMIT modeChanged();
}

void CaptureApp::setDelay(int seconds)
{
    if (m_delay == seconds) {
        return;
    }
    m_delay = seconds;
    settings().writeEntry("Delay", seconds);
    settings().sync();
    Q_EMIT delayChanged();
}

void CaptureApp::setOpenEditor(bool on)
{
    if (m_openEditor == on) {
        return;
    }
    m_openEditor = on;
    settings().writeEntry("OpenEditor", on);
    settings().sync();
    Q_EMIT optionsChanged();
}

void CaptureApp::setAutoSave(bool on)
{
    if (m_autoSave == on) {
        return;
    }
    m_autoSave = on;
    settings().writeEntry("AutoSave", on);
    settings().sync();
    Q_EMIT optionsChanged();
}

void CaptureApp::setIncludePointer(bool on)
{
    if (m_includePointer == on) {
        return;
    }
    m_includePointer = on;
    settings().writeEntry("IncludePointer", on);
    settings().sync();
    Q_EMIT optionsChanged();
}

void CaptureApp::setMessage(const QString &message)
{
    m_message = message;
    Q_EMIT messageChanged();
}

void CaptureApp::handle(const QStringList &arguments)
{
    const int edit = arguments.indexOf(QStringLiteral("--edit"));
    if (edit >= 0 && edit + 1 < arguments.size()) {
        openEditorFor(QUrl::fromUserInput(arguments.at(edit + 1), QDir::currentPath(), QUrl::AssumeLocalFile));
        return;
    }
    if (arguments.contains(QStringLiteral("--scan"))) {
        if (m_selecting) {
            cancel();
        }
        openScan();
        return;
    }
    if (m_recorder->recording()) {
        // The shortcut again while recording ends the recording, as Stop
        // does.
        m_recorder->stop();
        return;
    }
    if (m_selecting || m_countdown > 0) {
        return;
    }
    const int mode = arguments.indexOf(QStringLiteral("--mode"));
    if (mode >= 0 && mode + 1 < arguments.size()) {
        setMode(arguments.at(mode + 1));
    }
    const int to = arguments.indexOf(QStringLiteral("--to"));
    m_returnPath = to >= 0 && to + 1 < arguments.size() ? QFileInfo(arguments.at(to + 1)).absoluteFilePath() : QString();
    setRecordMode(m_returnPath.isEmpty() && arguments.contains(QStringLiteral("--record")) && canRecord());
    freeze();
}

void CaptureApp::freeze()
{
    const auto images = m_grabber->grabScreens(m_includePointer);
    if (images.isEmpty()) {
        notifyFailure(QStringLiteral("Nothing captured"), QStringLiteral("The screen could not be read. Sign out and back in, then try again."));
        quitWhenIdle();
        return;
    }
    m_frozen->set(images);
    m_windows.clear();
    Q_EMIT windowsChanged();
    clearPick();
    m_grabber->requestWindows();
    ++m_serial;
    m_selecting = true;
    Q_EMIT selectingChanged();
}

void CaptureApp::clearPick()
{
    m_pickedScreen.clear();
    m_pickedArea = QRectF();
    m_pickedWindow.clear();
    Q_EMIT pickChanged();
}

void CaptureApp::pickArea(const QString &screen, const QRectF &area)
{
    m_pickedScreen = screen;
    m_pickedArea = area;
    m_pickedWindow.clear();
    Q_EMIT pickChanged();
}

void CaptureApp::pickWindow(const QVariantMap &window)
{
    m_pickedScreen.clear();
    m_pickedArea = QRectF();
    m_pickedWindow = window;
    Q_EMIT pickChanged();
}

void CaptureApp::capture(const QString &screen)
{
    if (m_mode == QLatin1String("screen")) {
        takeScreen(screen);
    } else if (m_mode == QLatin1String("window") && !m_pickedWindow.isEmpty()) {
        const QVariantMap &w = m_pickedWindow;
        takeWindow(w.value(QStringLiteral("id")).toString(),
                   QRectF(w.value(QStringLiteral("x")).toReal(), w.value(QStringLiteral("y")).toReal(), w.value(QStringLiteral("width")).toReal(),
                          w.value(QStringLiteral("height")).toReal()));
    } else if (!m_pickedScreen.isEmpty() && m_pickedArea.width() >= 1 && m_pickedArea.height() >= 1) {
        takeArea(m_pickedScreen, m_pickedArea);
    }
}

void CaptureApp::cancel()
{
    if (!m_returnPath.isEmpty()) {
        // Tells the app that asked that nothing is coming.
        QFile cancelled(m_returnPath + QStringLiteral(".cancelled"));
        if (cancelled.open(QIODevice::WriteOnly)) {
            cancelled.close();
        }
        m_returnPath.clear();
    }
    m_countdownTimer.stop();
    m_pending = nullptr;
    m_countdown = 0;
    Q_EMIT countdownChanged();
    m_selecting = false;
    Q_EMIT selectingChanged();
    // Back to the Scan window the toolbar was opened from.
    if (m_fromScan && m_scanOpen) {
        Q_EMIT scanRequested();
    }
    m_fromScan = false;
    quitWhenIdle();
}

// With a delay set the overlay goes, the seconds count down, and the screen
// is read again just before the capture, so a menu opened meanwhile is in
// the picture. Without one it happens at once.
void CaptureApp::after(std::function<void()> action)
{
    m_fromScan = false;
    m_selecting = false;
    Q_EMIT selectingChanged();
    if (m_delay <= 0) {
        action();
        return;
    }
    m_pending = std::move(action);
    m_countdown = m_delay;
    Q_EMIT countdownChanged();
    m_countdownTimer.start();
}

QImage CaptureApp::crop(const QString &screenName, const QRectF &area) const
{
    const QImage image = m_frozen->image(screenName);
    QScreen *screen = screenNamed(screenName);
    if (image.isNull() || !screen) {
        return {};
    }
    const qreal scale = image.width() / qreal(screen->geometry().width());
    // Rounded as the size under the selection is, so the picture is the
    // size it said.
    const QRect pixels = QRect(qRound(area.x() * scale), qRound(area.y() * scale), qRound(area.width() * scale), qRound(area.height() * scale)).intersected(image.rect());
    QImage result = image.copy(pixels);
    result.setDevicePixelRatio(image.devicePixelRatio());
    return result;
}

void CaptureApp::takeArea(const QString &screenName, const QRectF &area)
{
    if (area.width() < 1 || area.height() < 1) {
        cancel();
        return;
    }
    after([this, screenName, area] {
        if (m_recordMode) {
            QScreen *screen = screenNamed(screenName);
            const QRect global = area.translated(screen->geometry().topLeft()).toAlignedRect();
            m_recorder->recordArea(global, screen->devicePixelRatio());
            return;
        }
        finish(crop(screenName, area));
    });
}

void CaptureApp::takeFreehand(const QString &screenName, const QVariantList &points)
{
    QPainterPath path;
    for (const QVariant &value : points) {
        const QPointF point = value.toPointF();
        if (path.elementCount() == 0) {
            path.moveTo(point);
        } else {
            path.lineTo(point);
        }
    }
    path.closeSubpath();
    const QRectF bounds = path.boundingRect();
    if (bounds.width() < 2 || bounds.height() < 2) {
        cancel();
        return;
    }
    after([this, screenName, path, bounds] {
        const QImage area = crop(screenName, bounds);
        if (area.isNull()) {
            finish({});
            return;
        }
        // Outside the drawn shape is see-through, as Snipping Tool leaves a
        // freeform snip.
        QImage shaped(area.size(), QImage::Format_ARGB32_Premultiplied);
        shaped.fill(Qt::transparent);
        {
            QPainter painter(&shaped);
            painter.setRenderHint(QPainter::Antialiasing);
            const qreal scale = area.width() / bounds.width();
            painter.scale(scale, scale);
            painter.translate(-bounds.topLeft());
            painter.setClipPath(path);
            painter.drawImage(bounds, area);
        }
        shaped.setDevicePixelRatio(area.devicePixelRatio());
        finish(shaped);
    });
}

void CaptureApp::takeWindow(const QString &id, const QRectF &globalArea)
{
    after([this, id, globalArea] {
        if (m_recordMode) {
            m_recorder->recordWindow(id);
            return;
        }
        QImage image = m_grabber->grabWindow(id, m_includePointer);
        if (image.isNull()) {
            // Without KWin's help, the window as it looked on screen.
            for (QScreen *screen : QGuiApplication::screens()) {
                const QRectF part = globalArea.intersected(screen->geometry());
                if (!part.isEmpty()) {
                    image = crop(screen->name(), part.translated(-screen->geometry().topLeft()));
                    break;
                }
            }
        }
        finish(image);
    });
}

void CaptureApp::takeScreen(const QString &screenName)
{
    after([this, screenName] {
        if (m_recordMode) {
            m_recorder->recordScreen(screenNamed(screenName));
            return;
        }
        finish(m_frozen->image(screenName));
    });
}

QString CaptureApp::nextPath() const
{
    QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (pictures.isEmpty()) {
        pictures = QDir::homePath() + QStringLiteral("/Pictures");
    }
    const QString folder = pictures + QStringLiteral("/Screenshots");
    const QString stem = folder + QStringLiteral("/Screenshot ") + QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HHmmss"));
    QString path = stem + QStringLiteral(".png");
    for (int n = 2; QFileInfo::exists(path); ++n) {
        path = stem + QStringLiteral(" (%1).png").arg(n);
    }
    return path;
}

void CaptureApp::finish(const QImage &image)
{
    if (image.isNull()) {
        notifyFailure(QStringLiteral("Nothing captured"), QStringLiteral("The screen could not be read. Try again."));
        quitWhenIdle();
        return;
    }
    if (!m_returnPath.isEmpty()) {
        const QString target = m_returnPath;
        m_returnPath.clear();
        // Written beside and renamed, so the app waiting for the file never
        // reads half a picture.
        const QString partial = target + QStringLiteral(".part");
        if (!image.save(partial, "PNG") || !QFile::rename(partial, target)) {
            QFile::remove(partial);
            QFile cancelled(target + QStringLiteral(".cancelled"));
            if (cancelled.open(QIODevice::WriteOnly)) {
                cancelled.close();
            }
        }
        quitWhenIdle();
        return;
    }
    putOnClipboard(image);
    const QString path = nextPath();
    bool saved = false;
    if (m_autoSave) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        saved = image.save(path, "PNG");
    }

    if (m_openEditor) {
        const QString key = m_frozen->keep(image);
        ++m_openWindows;
        Q_EMIT editRequested(QUrl(QStringLiteral("image://frozen/") + key), key, path, saved);
        return;
    }

    const QUrl file = QUrl::fromLocalFile(path);
    auto *notification = new KNotification(QStringLiteral("captured"));
    notification->setComponentName(QStringLiteral("wildos-screenshot"));
    notification->setTitle(QStringLiteral("Screenshot copied"));
    notification->setText(saved            ? QStringLiteral("Also saved to Pictures › Screenshots.")
                              : m_autoSave ? QStringLiteral("It could not be saved to Pictures › Screenshots, so paste it somewhere soon.")
                                           : QStringLiteral("Paste it wherever you need it."));
    notification->setIconName(QStringLiteral("wildos-capture"));
    // Kept in the list after the toast goes, with its buttons still working
    // while this program waits for them.
    notification->setHint(QStringLiteral("resident"), true);
    notification->setHint(QStringLiteral("desktop-entry"), QStringLiteral("org.wildos.screenshot"));
    if (saved) {
        notification->setUrls({file});
        auto *open = notification->addDefaultAction(QStringLiteral("Open"));
        connect(open, &KNotificationAction::activated, this, [this, file, notification] {
            openFile(file);
            notification->close();
        });
        auto *edit = notification->addAction(QStringLiteral("Edit"));
        connect(edit, &KNotificationAction::activated, this, [this, file, notification] {
            openEditorFor(file);
            notification->close();
        });
        auto *share = notification->addAction(QStringLiteral("Share"));
        connect(share, &KNotificationAction::activated, this, [this, file, notification] {
            openShare(file);
            notification->close();
        });
    }
    connect(notification, &KNotification::closed, this, &CaptureApp::quitWhenIdle);
    m_notifications.append(notification);
    notification->sendEvent();
}

void CaptureApp::recordingFinished(const QString &path)
{
    const QUrl file = QUrl::fromLocalFile(path);
    auto *notification = new KNotification(QStringLiteral("recorded"));
    notification->setComponentName(QStringLiteral("wildos-screenshot"));
    notification->setTitle(QStringLiteral("Screen recording saved"));
    notification->setText(QStringLiteral("In Videos › Screen recordings."));
    notification->setIconName(QStringLiteral("wildos-capture"));
    notification->setHint(QStringLiteral("resident"), true);
    notification->setHint(QStringLiteral("desktop-entry"), QStringLiteral("org.wildos.screenshot"));
    notification->setUrls({file});
    auto *play = notification->addDefaultAction(QStringLiteral("Play"));
    connect(play, &KNotificationAction::activated, this, [this, file, notification] {
        openFile(file);
        notification->close();
    });
    auto *folder = notification->addAction(QStringLiteral("Show in folder"));
    connect(folder, &KNotificationAction::activated, this, [this, file, notification] {
        showInFolder(file);
        notification->close();
    });
    auto *share = notification->addAction(QStringLiteral("Share"));
    connect(share, &KNotificationAction::activated, this, [this, file, notification] {
        openShare(file);
        notification->close();
    });
    connect(notification, &KNotification::closed, this, &CaptureApp::quitWhenIdle);
    m_notifications.append(notification);
    notification->sendEvent();
}

void CaptureApp::notifyFailure(const QString &title, const QString &text)
{
    auto *notification = new KNotification(QStringLiteral("failed"));
    notification->setComponentName(QStringLiteral("wildos-screenshot"));
    notification->setTitle(title);
    notification->setText(text);
    notification->setIconName(QStringLiteral("dialog-warning"));
    notification->setHint(QStringLiteral("desktop-entry"), QStringLiteral("org.wildos.screenshot"));
    connect(notification, &KNotification::closed, this, &CaptureApp::quitWhenIdle);
    m_notifications.append(notification);
    notification->sendEvent();
}

bool CaptureApp::copyImage(const QVariant &image, const QRectF &crop)
{
    const QImage picture = cropped(image, crop);
    if (picture.isNull()) {
        return false;
    }
    putOnClipboard(picture);
    return true;
}

bool CaptureApp::saveImage(const QVariant &image, const QRectF &crop, const QString &path)
{
    const QImage picture = cropped(image, crop);
    if (picture.isNull() || path.isEmpty()) {
        return false;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    return picture.save(path, "PNG");
}

void CaptureApp::openEditorFor(const QUrl &file)
{
    if (!file.isLocalFile() || !QFileInfo::exists(file.toLocalFile())) {
        quitWhenIdle();
        return;
    }
    ++m_openWindows;
    Q_EMIT editRequested(file, QString(), file.toLocalFile(), true);
}

void CaptureApp::openShare(const QUrl &file)
{
    ++m_openWindows;
    setMessage(QString());
    Q_EMIT shareRequested(file);
}

void CaptureApp::openFile(const QUrl &file)
{
    QDesktopServices::openUrl(file);
}

void CaptureApp::showInFolder(const QUrl &file)
{
    // Files answers the freedesktop file manager interface, so the folder
    // opens with the file selected.
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.FileManager1"), QStringLiteral("/org/freedesktop/FileManager1"),
                                                          QStringLiteral("org.freedesktop.FileManager1"), QStringLiteral("ShowItems"));
    message.setArguments({QStringList{file.toString()}, QString()});
    if (!QDBusConnection::sessionBus().send(message)) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(file.toLocalFile()).absolutePath()));
    }
}

void CaptureApp::editorClosed(const QString &key)
{
    if (!key.isEmpty()) {
        m_frozen->release(key);
    }
    windowClosed();
}

void CaptureApp::openScan()
{
    m_fromScan = false;
    if (!m_scanOpen) {
        m_scanOpen = true;
        ++m_openWindows;
    }
    Q_EMIT scanRequested();
}

void CaptureApp::scanClosed()
{
    if (m_scanOpen) {
        m_scanOpen = false;
        windowClosed();
    }
}

void CaptureApp::startCapture(bool record)
{
    if (m_selecting || m_countdown > 0 || m_recorder->recording()) {
        return;
    }
    m_returnPath.clear();
    m_fromScan = true;
    setRecordMode(record && canRecord());
    // The Scan window is minimised first; this gives KWin time to take it
    // off the screen before the screen is frozen.
    QTimer::singleShot(400, this, [this] {
        freeze();
    });
}

void CaptureApp::markUpScanPage(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    ++m_openWindows;
    Q_EMIT scanPageEditRequested(QUrl::fromLocalFile(path), path);
}

void CaptureApp::windowClosed()
{
    m_openWindows = std::max(0, m_openWindows - 1);
    quitWhenIdle();
}

void CaptureApp::quitWhenIdle()
{
    m_idleTimer.start();
}
