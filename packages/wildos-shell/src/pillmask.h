#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QRegion>
#include <QVariantList>

// Builds the region the panel's blur and popups follow from the glass pills
// the panel draws. QML has no way to make or combine a QRegion, and the
// panel view only accepts one region for the whole window.
class PillMask : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    using QObject::QObject;

    Q_INVOKABLE QRegion region(const QVariantList &rects, qreal radius) const;

    // Shares one screen's pills, relative to the panel window, with KWin's
    // underglow script, which cannot see inside the panel: a floating
    // taskbar is one full width window with the pills drawn in it.
    Q_INVOKABLE void publish(const QString &screen, const QVariantList &rects, qreal radius) const;

    // The file publish() writes, in the session's runtime directory.
    static QString pillsPath();
};
