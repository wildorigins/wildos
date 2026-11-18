#include "facerenderer.h"

#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlExtensionPlugin>
#include <QQuickWindow>
#include <QTextStream>

Q_IMPORT_QML_PLUGIN(WildOS_Cooler_FacesPlugin)

// wildos-cooler-face-render: one face from WildOS.Cooler.Faces to PNG.
//
//   --face liquid --time 2.5 --size 320 --out liquid.png
//   --face warp --frames 39 --fps 13 --size 320 --out-dir frames/
//
// It needs a display with OpenGL; under Xvfb, Mesa's software GL will do.

static int fail(const QString &message)
{
    QTextStream(stderr) << message << Qt::endl;
    return 1;
}

int main(int argc, char **argv)
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("wildos-cooler-face-render"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Renders a face of the cooler's screen to PNG."));
    parser.addHelpOption();
    parser.addOptions({
        { QStringLiteral("face"), QStringLiteral("The face to draw, by id (liquid, clock, last-minute, both, cores, warp, tunnel, reactor, hive, matrix, neon, rgb-ring, limit)."), QStringLiteral("id") },
        { QStringLiteral("time"), QStringLiteral("Draw the face as it is this many seconds in."), QStringLiteral("seconds"), QStringLiteral("0") },
        { QStringLiteral("size"), QStringLiteral("Width and height of the picture."), QStringLiteral("pixels"), QStringLiteral("640") },
        { QStringLiteral("out"), QStringLiteral("Write one picture here."), QStringLiteral("file") },
        { QStringLiteral("frames"), QStringLiteral("Write this many frames in a row, starting at --time."), QStringLiteral("count") },
        { QStringLiteral("fps"), QStringLiteral("Frames a second for --frames."), QStringLiteral("rate"), QStringLiteral("13") },
        { QStringLiteral("out-dir"), QStringLiteral("Folder for --frames, written as 0000.png onwards."), QStringLiteral("folder") },
        { QStringLiteral("readings"), QStringLiteral("Readings as JSON, such as {\"cpuTemp\": 100}. \"now\" takes an ISO date and time."), QStringLiteral("json") },
        { QStringLiteral("theme"), QStringLiteral("Theme colours as accent,accentDeep,ink."), QStringLiteral("colours") },
        { QStringLiteral("no-mark"), QStringLiteral("Leave the Wild OS mark off.") },
        { QStringLiteral("square"), QStringLiteral("Do not cut the face to a circle.") },
    });
    parser.process(app);

    const QString face = parser.value(QStringLiteral("face"));
    const int size = parser.value(QStringLiteral("size")).toInt();
    const bool sequence = parser.isSet(QStringLiteral("frames"));
    if (face.isEmpty() || size <= 0
            || (sequence ? !parser.isSet(QStringLiteral("out-dir")) : !parser.isSet(QStringLiteral("out"))))
        parser.showHelp(1);

    FaceRenderer renderer(size);
    if (!renderer.isValid() || !renderer.setFace(face))
        return fail(renderer.errorString());

    if (parser.isSet(QStringLiteral("readings"))) {
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(parser.value(QStringLiteral("readings")).toUtf8(), &error);
        if (!document.isObject())
            return fail(QStringLiteral("--readings: %1").arg(error.errorString()));
        QVariantMap readings = document.object().toVariantMap();
        if (readings.contains(QStringLiteral("now")))
            readings[QStringLiteral("now")] = QDateTime::fromString(readings.value(QStringLiteral("now")).toString(), Qt::ISODate);
        renderer.setReadings(readings);
    }
    if (parser.isSet(QStringLiteral("theme"))) {
        const QStringList colours = parser.value(QStringLiteral("theme")).split(QLatin1Char(','));
        if (colours.size() != 3)
            return fail(QStringLiteral("--theme takes three colours: accent,accentDeep,ink"));
        renderer.setThemeColours(QVariantMap {
            { QStringLiteral("accent"), colours.at(0) },
            { QStringLiteral("accentDeep"), colours.at(1) },
            { QStringLiteral("ink"), colours.at(2) },
        });
    }
    renderer.setShowMark(!parser.isSet(QStringLiteral("no-mark")));
    renderer.setCircular(!parser.isSet(QStringLiteral("square")));

    const qreal start = parser.value(QStringLiteral("time")).toDouble();
    if (!sequence) {
        const QString out = parser.value(QStringLiteral("out"));
        if (!renderer.render(start).save(out))
            return fail(QStringLiteral("Could not write %1").arg(out));
        return 0;
    }

    const int frames = parser.value(QStringLiteral("frames")).toInt();
    const qreal fps = parser.value(QStringLiteral("fps")).toDouble();
    const QDir folder(parser.value(QStringLiteral("out-dir")));
    if (frames <= 0 || fps <= 0 || !folder.mkpath(QStringLiteral(".")))
        parser.showHelp(1);
    for (int n = 0; n < frames; ++n) {
        const QString out = folder.filePath(QStringLiteral("%1.png").arg(n, 4, 10, QLatin1Char('0')));
        if (!renderer.render(start + n / fps).save(out))
            return fail(QStringLiteral("Could not write %1").arg(out));
    }
    return 0;
}
