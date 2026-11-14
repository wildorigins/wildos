// wildos-pdf: PDF, Wild OS's app for PDF files. Pages are drawn by Poppler;
// notes, form filling and signatures are written back with Poppler too.
// Nothing goes online.
//
//   wildos-pdf [file.pdf]
//   wildos-pdf --sign file.pdf   opens with Sign chosen, for Capture's
//                                Open in PDF to sign
//
// Each file opens in a PDF of its own.

#include "document.h"
#include "pageprovider.h"
#include "signatures.h"

#include <QApplication>
#include <QColor>
#include <QCommandLineParser>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlError>
#include <QQmlIncubationController>
#include <QQmlIncubator>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>

namespace
{
// Builds the window a little at a time between turns of the event loop,
// so the splash can come up and animate while it is made.
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

// The launch splash (SplashDaily.dc). Opening a file it waits a third of a
// second first, so a quick open never flashes; started on its own it shows
// at once and stays long enough to read, as Mail does.
QObject *makeSplash(QQmlEngine *engine, const QString &fileName)
{
    QQmlComponent component(engine, QStringLiteral("WildOS.UI"), QStringLiteral("LoadingSplash"));
    const bool opening = !fileName.isEmpty();
    const QVariantMap properties{
        {QStringLiteral("appName"), QStringLiteral("PDF")},
        {QStringLiteral("iconSource"), QUrl(QStringLiteral("file:///usr/share/icons/hicolor/scalable/apps/wildos-pdf.svg"))},
        {QStringLiteral("accent"), QColor(QStringLiteral("#ff6b6b"))},
        {QStringLiteral("accentDeep"), QColor(QStringLiteral("#c51f33"))},
        {QStringLiteral("status"),
         opening ? QCoreApplication::translate("main", "Opening %1").arg(fileName) : QCoreApplication::translate("main", "Getting ready")},
        {QStringLiteral("noteStart"), QCoreApplication::translate("main", "Read, fill in and sign")},
        {QStringLiteral("noteEnd"), QCoreApplication::translate("main", "Nothing leaves this PC")},
        {QStringLiteral("showDelay"), opening ? 333 : 0},
        {QStringLiteral("minimumTime"), opening ? 0 : 900},
    };
    QObject *splash = component.createWithInitialProperties(properties);
    if (!qobject_cast<QQuickWindow *>(splash)) {
        delete splash;
        return nullptr;
    }
    return splash;
}
}

int main(int argc, char *argv[])
{
    // A QApplication, not a QGuiApplication: only then does the KDE
    // platform theme hand open and save dialogs to the Wild OS file dialog,
    // and the print dialog is a widget.
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("wildos-pdf"));
    app.setOrganizationDomain(QStringLiteral("wildos.io"));
    app.setApplicationDisplayName(QStringLiteral("PDF"));
    app.setApplicationVersion(QStringLiteral(WILDOS_PDF_VERSION));
    app.setDesktopFileName(QStringLiteral("org.wildos.pdf"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("wildos-pdf")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Wild OS PDF"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("A PDF to open."));
    const QCommandLineOption sign(QStringLiteral("sign"), QStringLiteral("Open with Sign chosen."));
    parser.addOption(sign);
    parser.process(app);
    const QString arg = parser.positionalArguments().value(0);
    const QUrl url = arg.isEmpty() ? QUrl() : QUrl::fromUserInput(arg, QDir::currentPath(), QUrl::AssumeLocalFile);

    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QQuickWindow::setDefaultAlphaBuffer(true);

    // Declared before the engine, which only borrows it.
    Incubation incubation;
    QQmlApplicationEngine engine;
    engine.setIncubationController(&incubation);
    engine.addImageProvider(QStringLiteral("page"), new PageProvider);
    engine.addImageProvider(QStringLiteral("signature"), new SignatureProvider);

    QObject *splash = makeSplash(&engine, url.isLocalFile() ? QFileInfo(url.toLocalFile()).fileName() : QString());

    // Opened before the window is built, so its first frame already has the
    // pages laid out. Main.qml says why if it failed.
    QString openError;
    if (url.isValid() && !url.isEmpty()) {
        openError = PdfDocument::instance()->open(url);
    }
    Signatures::instance()->load();

    QQmlComponent home(&engine);
    QQmlIncubator incubator(QQmlIncubator::Asynchronous);
    QVariantMap initial{{QStringLiteral("openError"), openError}};
    if (parser.isSet(sign) && openError.isEmpty() && url.isValid() && !url.isEmpty()) {
        initial.insert(QStringLiteral("startTool"), QStringLiteral("sign"));
    }
    if (splash) {
        // Built hidden behind the splash and shown as it fades.
        initial.insert(QStringLiteral("visible"), false);
    }
    incubator.setInitialProperties(initial);

    QTimer poll;
    poll.setInterval(16);
    QObject::connect(&poll, &QTimer::timeout, &app, [&]() {
        if (incubator.isLoading()) {
            return;
        }
        poll.stop();
        if (incubator.isError()) {
            for (const QQmlError &e : incubator.errors()) {
                qWarning().noquote() << e.toString();
            }
            QCoreApplication::exit(1);
            return;
        }
        QObject *object = incubator.object();
        QQmlEngine::setObjectOwnership(object, QQmlEngine::CppOwnership);
        object->setParent(&engine);
        auto *window = qobject_cast<QQuickWindow *>(object);
        if (splash) {
            if (window) {
                QObject::connect(splash, SIGNAL(finished()), window, SLOT(show()));
                QObject::connect(splash, SIGNAL(finished()), window, SLOT(requestActivate()));
            }
            QMetaObject::invokeMethod(splash, "finish");
        }
    });
    bool started = false;
    auto create = [&]() {
        if (started) {
            return;
        }
        started = true;
        if (home.isError()) {
            qWarning().noquote() << home.errorString();
            QCoreApplication::exit(1);
            return;
        }
        home.create(incubator);
        poll.start();
    };
    QObject::connect(&home, &QQmlComponent::statusChanged, &app, [&](QQmlComponent::Status status) {
        if (status == QQmlComponent::Ready || status == QQmlComponent::Error) {
            create();
        }
    });
    home.loadFromModule("WildOS.PDF", "Main", QQmlComponent::Asynchronous);
    if (home.status() == QQmlComponent::Ready || home.status() == QQmlComponent::Error) {
        create();
    }
    return app.exec();
}
