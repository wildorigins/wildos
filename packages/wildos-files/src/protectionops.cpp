// The file menu's Advanced protection entries: Remove personal details and
// Open in safe box. Both only show while their switch is on (Protection).

#include "fileops.h"
#include "protection.h"
#include "safeboxplan.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSet>

namespace
{
const QString removeDetailsTool = QStringLiteral("/usr/libexec/wildos/remove-details");
const QString safeBoxTool = QStringLiteral("/usr/bin/wildos-safebox");

// The kinds remove-details can clean; the script has the same lists.
const QSet<QString> &cleanable()
{
    static const QSet<QString> extensions{
        QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("jpe"), QStringLiteral("png"), QStringLiteral("heic"), QStringLiteral("heif"),
        QStringLiteral("avif"), QStringLiteral("webp"), QStringLiteral("tif"), QStringLiteral("tiff"), QStringLiteral("gif"), QStringLiteral("dng"),
        QStringLiteral("mp4"), QStringLiteral("m4v"), QStringLiteral("mov"), QStringLiteral("3gp"), QStringLiteral("3g2"), QStringLiteral("m4a"),
        QStringLiteral("mp3"), QStringLiteral("flac"), QStringLiteral("ogg"), QStringLiteral("oga"), QStringLiteral("opus"), QStringLiteral("pdf"),
        QStringLiteral("docx"), QStringLiteral("docm"), QStringLiteral("dotx"), QStringLiteral("dotm"), QStringLiteral("xlsx"), QStringLiteral("xlsm"),
        QStringLiteral("xltx"), QStringLiteral("xltm"), QStringLiteral("pptx"), QStringLiteral("pptm"), QStringLiteral("potx"), QStringLiteral("potm"),
        QStringLiteral("ppsx"), QStringLiteral("ppsm"), QStringLiteral("odt"), QStringLiteral("ott"), QStringLiteral("ods"), QStringLiteral("ots"),
        QStringLiteral("odp"), QStringLiteral("otp"), QStringLiteral("odg"), QStringLiteral("otg"),
    };
    return extensions;
}
}

bool FileOps::canRemoveDetails(const QList<QUrl> &urls) const
{
    const Protection *protection = Protection::instance();
    if (!protection->scrubFiles() || urls.isEmpty() || !QFileInfo::exists(removeDetailsTool)
        || !(protection->removeLocation() || protection->removeDevice() || protection->removeAuthors())) {
        return false;
    }
    for (const QUrl &url : urls) {
        const QFileInfo info(url.toLocalFile());
        if (!url.isLocalFile() || !info.isFile() || !cleanable().contains(info.suffix().toLower())) {
            return false;
        }
    }
    return true;
}

void FileOps::removeDetails(const QList<QUrl> &urls)
{
    if (!canRemoveDetails(urls)) {
        return;
    }
    const Protection *protection = Protection::instance();
    QStringList args;
    if (protection->removeLocation()) {
        args << QStringLiteral("--location");
    }
    if (protection->removeDevice()) {
        args << QStringLiteral("--device");
    }
    if (protection->removeAuthors()) {
        args << QStringLiteral("--authors");
    }
    args << QStringLiteral("--");
    for (const QUrl &url : urls) {
        args << url.toLocalFile();
    }
    auto process = new QProcess(this);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(process, &QProcess::finished, this, [this, process] {
        process->deleteLater();
        QHash<QUrl, QList<QUrl>> made;
        QStringList problems;
        int copies = 0;
        const QList<QByteArray> lines = process->readAllStandardOutput().split('\n');
        for (const QByteArray &line : lines) {
            const QJsonObject result = QJsonDocument::fromJson(line).object();
            if (result.isEmpty()) {
                continue;
            }
            const QString error = result.value(QStringLiteral("error")).toString();
            if (!error.isEmpty()) {
                problems << QStringLiteral("%1: %2").arg(QFileInfo(result.value(QStringLiteral("file")).toString()).fileName(), error);
                continue;
            }
            const QUrl copy = QUrl::fromLocalFile(result.value(QStringLiteral("copy")).toString());
            made[copy.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash)].append(copy);
            ++copies;
        }
        for (auto it = made.cbegin(); it != made.cend(); ++it) {
            Q_EMIT created(it.key(), it.value(), false);
        }
        if (!problems.isEmpty()) {
            // One file: its sentence alone. Several: each one named.
            Q_EMIT failed(problems.size() == 1 && copies == 0 ? problems.first().section(QStringLiteral(": "), 1) : problems.join(QLatin1Char('\n')));
        } else if (copies == 0) {
            Q_EMIT failed(QStringLiteral("The personal details couldn't be removed."));
        }
        if (copies > 0) {
            Q_EMIT notice(copies == 1 ? QStringLiteral("Saved a copy without personal details")
                                      : QStringLiteral("Saved %1 copies without personal details").arg(copies));
        }
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            process->deleteLater();
            Q_EMIT failed(QStringLiteral("The personal details couldn't be removed."));
        }
    });
    process->start(removeDetailsTool, args);
}

QVariantMap FileOps::safeBoxPlan(const QUrl &url) const
{
    if (!Protection::instance()->safeBox() || !url.isLocalFile() || !QFileInfo::exists(safeBoxTool)) {
        return {{QStringLiteral("ok"), false}};
    }
    const SafeBoxPlan plan = ::safeBoxPlan(url.toLocalFile());
    return {
        {QStringLiteral("ok"), plan.ok},
        {QStringLiteral("program"), plan.program},
        {QStringLiteral("app"), plan.appName},
        {QStringLiteral("message"), plan.message},
    };
}

void FileOps::openInSafeBox(const QUrl &url)
{
    const QVariantMap plan = safeBoxPlan(url);
    if (!plan.value(QStringLiteral("ok")).toBool()) {
        const QString message = plan.value(QStringLiteral("message")).toString();
        if (!message.isEmpty()) {
            Q_EMIT failed(message);
        }
        return;
    }
    const QString path = url.toLocalFile();
    const auto launch = [this, path] {
        if (!QProcess::startDetached(safeBoxTool, {path}, QFileInfo(path).absolutePath())) {
            Q_EMIT failed(QStringLiteral("The safe box couldn't open."));
        }
    };
    Protection *protection = Protection::instance();
    if (protection->safeBoxExplained()) {
        launch();
        return;
    }
    // The first time, what the box does and does not do, in plain words.
    const QString name = url.fileName();
    const QString lead = plan.value(QStringLiteral("program")).toBool()
        ? QStringLiteral("%1 runs").arg(name)
        : QStringLiteral("%1 opens in %2,").arg(name, plan.value(QStringLiteral("app")).toString());
    QVariantMap prompt;
    prompt[QStringLiteral("kind")] = QStringLiteral("confirm");
    prompt[QStringLiteral("title")] = QStringLiteral("Open in safe box?");
    prompt[QStringLiteral("text")] = QStringLiteral("%1 sealed off from your files and the network, with no sound or camera. "
                                                    "Anything it saves is gone when it closes. This stops most harm, but a program "
                                                    "made to attack Wild OS itself could still break out.")
                                         .arg(lead);
    prompt[QStringLiteral("primary")] = QStringLiteral("Open in safe box");
    ask(prompt, [protection, launch](const QString &choice, bool) {
        if (choice == QLatin1String("primary")) {
            protection->setSafeBoxExplained(true);
            launch();
        }
    });
}
