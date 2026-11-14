#include "pins.h"

#include <KSystemClipboard>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeData>
#include <QSaveFile>
#include <QStandardPaths>

Pins::Pins(QObject *parent)
    : QObject(parent)
{
    load();
}

QString Pins::folder() const
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/wildos/clipboard");
}

void Pins::load()
{
    QFile file(folder() + QStringLiteral("/pins.json"));
    if (file.open(QIODevice::ReadOnly)) {
        m_items = QJsonDocument::fromJson(file.readAll()).array().toVariantList();
    }
}

void Pins::save()
{
    QDir().mkpath(folder());
    QSaveFile file(folder() + QStringLiteral("/pins.json"));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(QJsonArray::fromVariantList(m_items)).toJson(QJsonDocument::Compact));
        file.commit();
    }
    Q_EMIT changed();
}

QString Pins::keyFor(const QString &kind, const QString &text, const QUrl &image) const
{
    if (kind == QLatin1String("image")) {
        QFile file(image.toLocalFile());
        if (file.open(QIODevice::ReadOnly)) {
            return QStringLiteral("image:") + QString::fromLatin1(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha1).toHex());
        }
        return QString();
    }
    return kind + QLatin1Char(':') + text;
}

bool Pins::isPinned(const QString &key) const
{
    for (const QVariant &item : m_items) {
        if (item.toMap().value(QStringLiteral("key")).toString() == key) {
            return true;
        }
    }
    return false;
}

void Pins::pin(const QString &kind, const QString &text, const QUrl &image, double time, const QString &source)
{
    const QString key = keyFor(kind, text, image);
    if (key.isEmpty() || isPinned(key)) {
        return;
    }
    QVariantMap item{
        {QStringLiteral("key"), key},
        {QStringLiteral("kind"), kind},
        {QStringLiteral("text"), text},
        {QStringLiteral("time"), time},
        {QStringLiteral("source"), source},
    };
    if (kind == QLatin1String("image")) {
        QDir().mkpath(folder());
        const QString copy = folder() + QLatin1Char('/') + key.mid(6) + QStringLiteral(".png");
        if (!QFile::exists(copy)) {
            QImage(image.toLocalFile()).save(copy, "PNG");
        }
        item.insert(QStringLiteral("image"), QUrl::fromLocalFile(copy).toString());
    }
    m_items.prepend(item);
    save();
}

void Pins::unpin(const QString &key)
{
    for (int i = 0; i < m_items.size(); ++i) {
        const QVariantMap item = m_items.at(i).toMap();
        if (item.value(QStringLiteral("key")).toString() == key) {
            const QString image = QUrl(item.value(QStringLiteral("image")).toString()).toLocalFile();
            if (!image.isEmpty()) {
                QFile::remove(image);
            }
            m_items.removeAt(i);
            save();
            return;
        }
    }
}

void Pins::clear()
{
    for (const QVariant &value : std::as_const(m_items)) {
        const QString image = QUrl(value.toMap().value(QStringLiteral("image")).toString()).toLocalFile();
        if (!image.isEmpty()) {
            QFile::remove(image);
        }
    }
    m_items.clear();
    save();
}

void Pins::restore(const QString &key)
{
    for (const QVariant &value : std::as_const(m_items)) {
        const QVariantMap item = value.toMap();
        if (item.value(QStringLiteral("key")).toString() != key) {
            continue;
        }
        auto *mime = new QMimeData;
        const QString kind = item.value(QStringLiteral("kind")).toString();
        const QString text = item.value(QStringLiteral("text")).toString();
        if (kind == QLatin1String("image")) {
            const QString path = QUrl(item.value(QStringLiteral("image")).toString()).toLocalFile();
            QFile file(path);
            if (file.open(QIODevice::ReadOnly)) {
                mime->setData(QStringLiteral("image/png"), file.readAll());
            }
            mime->setImageData(QImage(path));
        } else if (kind == QLatin1String("url")) {
            QList<QUrl> urls;
            for (const QString &line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                urls.append(QUrl(line.trimmed()));
            }
            mime->setUrls(urls);
            mime->setText(text);
        } else {
            mime->setText(text);
        }
        KSystemClipboard::instance()->setMimeData(mime, QClipboard::Clipboard);
        return;
    }
}
