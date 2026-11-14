#pragma once

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantMap>

class QQmlEngine;
class QQuickWindow;

// Opens the dialog windows from QML, each above the window of the app that
// asked for it, and quits the service once the last one has closed and
// nothing has been asked for a while (D-Bus starts it again when needed).
class Windows : public QObject
{
    Q_OBJECT

public:
    static Windows *instance();

    QQuickWindow *open(const QString &component, const QVariantMap &properties, const QString &parentWindow, bool modal = false);
    void hold();
    void release();

private:
    explicit Windows(QObject *parent = nullptr);
    bool attach(QQuickWindow *window, const QString &parentWindow);
    void scheduleQuit();

    QQmlEngine *m_engine;
    QTimer m_idle;
    int m_open = 0;
};
