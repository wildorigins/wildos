#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QVariant>

// The PDFs opened lately, and where each was left: the page, the zoom and
// which way up. Kept in PDF's settings; every window reads and writes the
// same list, so nothing is held in memory between calls.
class History : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // The newest first, files that are still there only: { path, url,
    // name, folder }.
    Q_PROPERTY(QVariantList recent READ recent NOTIFY recentChanged)

public:
    static History *instance();
    static History *create(QQmlEngine *, QJSEngine *);

    QVariantList recent() const;

    void opened(const QString &path);
    Q_INVOKABLE void removeRecent(const QString &path);

    // { page, offset, zoom, turns }, or nothing for a file not seen before.
    // The offset is how far down the page the top of the view was, as a
    // fraction of the page's height.
    Q_INVOKABLE QVariantMap place(const QString &path) const;
    Q_INVOKABLE void setPlace(const QString &path, int page, double offset, double zoom, int turns);

Q_SIGNALS:
    void recentChanged();

private:
    explicit History(QObject *parent = nullptr);
};
