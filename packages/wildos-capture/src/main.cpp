// wildos-capture: Capture, Wild OS's screenshot and screen recording app.
//
//   wildos-capture               the toolbar over a frozen screen
//   wildos-capture --record      the same, ready to record
//   wildos-capture --edit FILE   a picture in the editor
//   wildos-capture --scan        the Scan tab, for paper on a scanner
//   wildos-capture --to FILE     a screenshot written to FILE and nothing
//                                else, for Report a problem; cancelling
//                                writes FILE.cancelled instead
//
// One instance: a second launch hands its arguments to the running one.

#include "captureapp.h"
#include "frozenimage.h"

#include <KDBusService>

#include <QApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlIncubationController>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

namespace
{
// Builds an editor window a little at a time between turns of the event
// loop, so the splash can come up and animate if it takes a while.
class Incubation : public QQmlIncubationController
{
public:
    Incubation()
    {
        m_timer.setInterval(16);
        QObject::connect(&m_timer, &QTimer::timeout, &m_timer, [this]() {
            if (incubatingObjectCount() > 0) {
                incubateFor(12);
            }
        });
        m_timer.start();
    }

private:
    QTimer m_timer;
};
}

int main(int argc, char *argv[])
{
    // Widgets only for Purpose's share plugins, some of which show a dialog.
    QApplication app(argc, argv);
    // The D-Bus name KDBusService takes is org.wildos.screenshot, from this
    // domain and name; KWin's window list script calls back on it.
    app.setOrganizationDomain(QStringLiteral("wildos.org"));
    app.setApplicationName(QStringLiteral("screenshot"));
    app.setApplicationDisplayName(QStringLiteral("Capture"));
    app.setApplicationVersion(QStringLiteral(WILDOS_CAPTURE_VERSION));
    app.setDesktopFileName(QStringLiteral("org.wildos.screenshot"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("wildos-capture")));
    app.setQuitOnLastWindowClosed(false);
    // It decides itself when to end; finished background jobs (previews,
    // share plugins) must not end it while it waits hidden.
    app.setQuitLockEnabled(false);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QQuickWindow::setDefaultAlphaBuffer(true);

    KDBusService service(KDBusService::Unique);

    CaptureApp capture;
    QObject::connect(&service, &KDBusService::activateRequested, &capture, [&capture](const QStringList &arguments, const QString &) {
        capture.handle(arguments);
    });

    // Declared before the engine, which only borrows it.
    Incubation incubation;
    QQmlApplicationEngine engine;
    engine.setIncubationController(&incubation);
    engine.rootContext()->setContextProperty(QStringLiteral("CaptureApp"), &capture);
    engine.addImageProvider(QStringLiteral("frozen"), capture.frozen());
    engine.addImageProvider(QStringLiteral("scan"), capture.scanner()->images());
    engine.loadFromModule("WildOS.Capture", "Main");
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }
    capture.handle(app.arguments());
    return app.exec();
}
