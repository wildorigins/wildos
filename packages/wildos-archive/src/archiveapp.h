#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QUrl>
#include <QVariantList>
#include <QCoreApplication>

// The one Archive process: its windows, the recent archives and what Files
// asks of it. A second start hands its command line to the running one.
//
//   wildos-archive [ARCHIVE...]           opens each archive in a window
//   wildos-archive --extract-here FILE...  extracts beside each archive
//   wildos-archive --compress FILE...      New archive with these files
class ArchiveApp : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QVariantList recent READ recent NOTIFY recentChanged)
    Q_PROPERTY(QString formatsText READ formatsText CONSTANT)

public:
    explicit ArchiveApp(QObject *parent);
    static ArchiveApp *instance();
    static ArchiveApp *create(QQmlEngine *, QJSEngine *);

    void setEngine(QQmlEngine *engine);
    // The first window, built behind the splash by main.cpp.
    void adopt(QQuickWindow *window);
    // Acts on a command line, from this start or a later one.
    void handle(const QStringList &arguments, const QString &workingDirectory, bool first);
    // What the first window should open, from this start's command line.
    static QVariantMap firstWindow(const QStringList &arguments, const QString &workingDirectory);

    QVariantList recent() const;
    QString formatsText() const
    {
        return QCoreApplication::translate("ArchiveApp", "Opens ZIP, 7z, RAR, TAR and GZ");
    }
    void addRecent(const QString &path);
    // A fresh private folder for a file opened from an archive.
    QString openingFolder();

    Q_INVOKABLE void removeRecent(const QString &path);
    Q_INVOKABLE void showFolder(const QString &path);
    // A window of its own for another archive.
    Q_INVOKABLE void openWindow(const QString &path);
    Q_INVOKABLE void windowClosed(QObject *window);

Q_SIGNALS:
    void recentChanged();

private:
    QQuickWindow *makeWindow(const QString &type, const QVariantMap &properties);
    void activate(QQuickWindow *window);
    void quitIfDone();

    QQmlEngine *m_engine = nullptr;
    QList<QPointer<QQuickWindow>> m_windows;
};
