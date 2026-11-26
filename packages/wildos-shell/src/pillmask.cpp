#include "pillmask.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainterPath>
#include <QSaveFile>
#include <QStandardPaths>
#include <QPolygon>
#include <QRectF>

QRegion PillMask::region(const QVariantList &rects, qreal radius) const
{
    QRegion result;
    for (const QVariant &value : rects) {
        const QRectF rect = value.toRectF();
        if (rect.isEmpty()) {
            continue;
        }
        QPainterPath path;
        path.addRoundedRect(rect, radius, radius);
        result += QRegion(path.toFillPolygon().toPolygon());
    }
    return result;
}

QString PillMask::pillsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QStringLiteral("/wildos-taskbar-pills.json");
}

void PillMask::publish(const QString &screen, const QVariantList &rects, qreal radius) const
{
    if (screen.isEmpty()) {
        return;
    }
    QJsonArray pills;
    for (const QVariant &value : rects) {
        const QRectF rect = value.toRectF();
        if (!rect.isEmpty()) {
            pills.append(QJsonArray{rect.x(), rect.y(), rect.width(), rect.height()});
        }
    }
    const QJsonObject entry{{QStringLiteral("pills"), pills}, {QStringLiteral("radius"), radius}};

    QJsonObject all;
    QFile current(pillsPath());
    if (current.open(QIODevice::ReadOnly)) {
        all = QJsonDocument::fromJson(current.readAll()).object();
        current.close();
    }
    if (all.value(screen).toObject() == entry) {
        return;
    }
    all.insert(screen, entry);

    // Replaced whole, so KWin never reads half a file.
    QSaveFile file(pillsPath());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(all).toJson(QJsonDocument::Compact));
        file.commit();
    }
}
