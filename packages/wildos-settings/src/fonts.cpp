#include "fonts.h"

#include <QCollator>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>

namespace
{
QString userFontDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/fonts");
}

// ~/.fonts is the older place for one account's fonts and fontconfig
// still reads it, so fonts there count as the user's too.
bool isUserFile(const QString &path)
{
    const QString legacy = QDir::homePath() + QStringLiteral("/.fonts/");
    return path.startsWith(userFontDir() + QLatin1Char('/')) || path.startsWith(legacy);
}

const QStringList fontSuffixes = {QStringLiteral("ttf"), QStringLiteral("otf"), QStringLiteral("ttc"), QStringLiteral("otc")};

// The family fontconfig reads from a file, or nothing when it is not a
// font it can use.
QString scanFamily(const QString &path)
{
    QProcess process;
    process.start(QStringLiteral("fc-scan"), {QStringLiteral("--format"), QStringLiteral("%{family[0]}\\n"), path});
    if (!process.waitForFinished(10000)) {
        process.kill();
        return {};
    }
    return QString::fromUtf8(process.readAllStandardOutput()).section(QLatin1Char('\n'), 0, 0).trimmed();
}
}

Fonts::Fonts(QObject *parent)
    : QObject(parent)
{
    reload();
}

void Fonts::setBusy(bool busy)
{
    if (m_busy != busy) {
        m_busy = busy;
        Q_EMIT busyChanged();
    }
}

void Fonts::reload()
{
    // Three lines a font, so a family name with any punctuation in it
    // cannot be mistaken for a separator.
    auto process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process] {
        const QStringList lines = QString::fromUtf8(process->readAllStandardOutput()).split(QLatin1Char('\n'));
        process->deleteLater();

        struct Family {
            QSet<QString> styles;
            QStringList files;
            bool system = false;
        };
        QMap<QString, Family> byName;
        for (qsizetype i = 0; i + 2 < lines.size(); i += 3) {
            const QString file = lines.at(i);
            const QString name = lines.at(i + 1).trimmed();
            // Names starting with a dot are fontconfig's way of hiding a
            // font from lists like this one.
            if (name.isEmpty() || name.startsWith(QLatin1Char('.'))) {
                continue;
            }
            Family &family = byName[name];
            family.styles.insert(lines.at(i + 2).trimmed());
            if (isUserFile(file)) {
                if (!family.files.contains(file)) {
                    family.files << file;
                }
            } else {
                family.system = true;
            }
        }

        QVariantList list;
        m_userFiles.clear();
        for (auto it = byName.cbegin(); it != byName.cend(); ++it) {
            // A family with any file from the system stays: removing only
            // your copy would leave it installed anyway.
            const bool removable = !it->system && !it->files.isEmpty();
            if (removable) {
                m_userFiles.insert(it.key(), it->files);
            }
            list << QVariantMap{
                {QStringLiteral("family"), it.key()},
                {QStringLiteral("styles"), int(it->styles.size())},
                {QStringLiteral("removable"), removable},
            };
        }
        QCollator collator;
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        collator.setNumericMode(true);
        std::sort(list.begin(), list.end(), [&collator](const QVariant &a, const QVariant &b) {
            return collator.compare(a.toMap().value(QStringLiteral("family")).toString(), b.toMap().value(QStringLiteral("family")).toString()) < 0;
        });
        m_families = list;
        Q_EMIT changed();
    });
    process->start(QStringLiteral("fc-list"), {QStringLiteral("--format"), QStringLiteral("%{file}\\n%{family[0]}\\n%{style[0]}\\n")});
}

void Fonts::refreshCache(const QString &title, const QString &message, bool failed)
{
    setBusy(true);
    auto process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process, title, message, failed] {
        process->deleteLater();
        setBusy(false);
        reload();
        Q_EMIT finished(title, message, failed);
    });
    process->start(QStringLiteral("fc-cache"), {QStringLiteral("-f"), userFontDir()});
}

void Fonts::install(const QList<QUrl> &files)
{
    if (m_busy || files.isEmpty()) {
        return;
    }
    const QString dir = userFontDir();
    QDir().mkpath(dir);

    QStringList installed;
    QStringList refused;
    for (const QUrl &url : files) {
        const QString path = url.toLocalFile();
        const QFileInfo info(path);
        if (!fontSuffixes.contains(info.suffix().toLower())) {
            refused << info.fileName();
            continue;
        }
        const QString family = scanFamily(path);
        if (family.isEmpty()) {
            refused << info.fileName();
            continue;
        }
        const QString target = dir + QLatin1Char('/') + info.fileName();
        if (QFileInfo(target).canonicalFilePath() == info.canonicalFilePath()) {
            installed << family;
            continue;
        }
        // A newer copy of a font you installed before replaces it.
        QFile::remove(target);
        if (QFile::copy(path, target)) {
            installed << family;
        } else {
            refused << info.fileName();
        }
    }
    installed.removeDuplicates();

    if (installed.isEmpty()) {
        Q_EMIT finished(tr("No fonts were installed"),
                        (refused.size() == 1 ? tr("%1 is not a font file this computer can use.")
                                             : tr("%1 are not font files this computer can use."))
                            .arg(refused.join(QStringLiteral(", "))),
                        true);
        return;
    }
    QString message = installed.size() == 1 ? tr("%1 is ready in apps you open from now on.").arg(installed.constFirst())
                                            : tr("%1 are ready in apps you open from now on.").arg(installed.join(QStringLiteral(", ")));
    if (!refused.isEmpty()) {
        message = tr("%1 %2 could not be installed.").arg(message, refused.join(QStringLiteral(", ")));
    }
    refreshCache(installed.size() == 1 ? tr("Font installed") : tr("Fonts installed"), message, false);
}

void Fonts::remove(const QString &family)
{
    if (m_busy || !m_userFiles.contains(family)) {
        return;
    }
    bool ok = true;
    for (const QString &file : m_userFiles.value(family)) {
        // Only ever a file in your own font folders.
        if (isUserFile(file) && QFile::exists(file) && !QFile::remove(file)) {
            ok = false;
        }
    }
    if (!ok) {
        Q_EMIT finished(tr("The font was not removed"), tr("Some of %1's files could not be deleted.").arg(family), true);
        reload();
        return;
    }
    refreshCache(tr("Font removed"), tr("%1 is gone from apps you open from now on.").arg(family), false);
}
