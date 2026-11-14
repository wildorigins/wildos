#include "windows.h"

#include "thumbnails.h"

#include <KWindowSystem>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QTimer>

namespace
{
// Long enough that a second dialog straight after the first opens at once.
constexpr int idleQuitMs = 5 * 60 * 1000;
}

Windows *Windows::instance()
{
    static Windows *windows = new Windows;
    return windows;
}

Windows::Windows(QObject *parent)
    : QObject(parent)
    , m_engine(new QQmlApplicationEngine(this))
{
    m_engine->addImageProvider(QStringLiteral("thumb"), new ThumbnailProvider);
    m_idle.setSingleShot(true);
    m_idle.setInterval(idleQuitMs);
    connect(&m_idle, &QTimer::timeout, this, [this] {
        if (m_open == 0) {
            QCoreApplication::quit();
        }
    });
    scheduleQuit();
}

QQuickWindow *Windows::open(const QString &component, const QVariantMap &properties, const QString &parentWindow, bool modal)
{
    QQmlComponent factory(m_engine, QStringLiteral("WildOS.Portal"), component);
    QObject *object = factory.createWithInitialProperties(properties);
    auto window = qobject_cast<QQuickWindow *>(object);
    if (!window) {
        qWarning("wildos-portal: %s did not load: %s", qPrintable(component), qPrintable(factory.errorString()));
        delete object;
        return nullptr;
    }
    hold();
    connect(window, &QObject::destroyed, this, &Windows::release);
    // Attached to the app's window, the dialog has no taskbar button of its
    // own and stays in front of that window. Modal when the app asks (an
    // Open or Save dialog does by default), as on Windows: that window can't
    // be used until the dialog closes.
    if (attach(window, parentWindow) && modal) {
        window->setModality(Qt::WindowModal);
    }
    window->show();
    window->requestActivate();
    return window;
}

bool Windows::attach(QQuickWindow *window, const QString &parentWindow)
{
    // The portal names the app's window "x11:<id>" or "wayland:<exported
    // handle>". An X11 id means nothing on Wayland (an app on XWayland, or
    // a Qt app before Wild OS's plasma-integration patch, sends one), so
    // only the matching kind is used.
    if (parentWindow.startsWith(QStringLiteral("x11:")) && KWindowSystem::isPlatformX11()) {
        bool ok = false;
        const WId id = parentWindow.mid(4).toULongLong(&ok, 16);
        if (ok && id) {
            KWindowSystem::setMainWindow(window, id);
            return true;
        }
    } else if (parentWindow.startsWith(QStringLiteral("wayland:")) && KWindowSystem::isPlatformWayland()) {
        KWindowSystem::setMainWindow(window, parentWindow.mid(8));
        return true;
    }
    return false;
}

void Windows::hold()
{
    ++m_open;
    m_idle.stop();
}

void Windows::release()
{
    if (--m_open <= 0) {
        m_open = 0;
        scheduleQuit();
    }
}

void Windows::scheduleQuit()
{
    m_idle.start();
}
