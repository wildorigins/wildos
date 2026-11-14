#pragma once

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QUrl>

class QQmlApplicationEngine;
class QQmlComponent;
class QQuickWindow;

// Opens Files windows. Like Explorer, every launch opens a new window in
// the one running process, so they share the clipboard, the Recycle Bin watcher
// and the thumbnail cache.
class App : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_CLASSINFO("D-Bus Interface", "org.wildos.Files")

public:
    static App *instance();
    static App *create(QQmlEngine *, QJSEngine *);

    void setEngine(QQmlApplicationEngine *engine) { m_engine = engine; }

public Q_SLOTS:
    Q_SCRIPTABLE void Open(const QStringList &urls);
    // Opens the Format dialog for a drive, for other apps (Backup offers it
    // for a drive it can't write to): a device such as /dev/sdb1 or a
    // UDisks2 block object path. A drive Files would not format gets a
    // message instead.
    Q_SCRIPTABLE void Format(const QString &device);
    // "Choose what is removed" for Remove personal details, which Settings'
    // Advanced protection page opens.
    Q_SCRIPTABLE void ChooseRemovedDetails();
    Q_INVOKABLE void openWindow(const QList<QUrl> &urls, const QString &formatUdi = QString());
    Q_INVOKABLE void newWindow() { openWindow({}); }

private:
    explicit App(QObject *parent = nullptr);

    QQmlApplicationEngine *m_engine = nullptr;
    QQmlComponent *m_component = nullptr;
    QPointer<QQuickWindow> m_choices;
};

// org.freedesktop.FileManager1, so "Show in folder" in browsers and other
// apps opens Files.
class FileManager1 : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.FileManager1")

public:
    explicit FileManager1(QObject *parent);

public Q_SLOTS:
    Q_SCRIPTABLE void ShowFolders(const QStringList &uriList, const QString &startUpId);
    Q_SCRIPTABLE void ShowItems(const QStringList &uriList, const QString &startUpId);
    Q_SCRIPTABLE void ShowItemProperties(const QStringList &uriList, const QString &startUpId);
};
