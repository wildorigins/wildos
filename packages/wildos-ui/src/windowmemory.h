#pragma once

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QQmlParserStatus>
#include <QQuickWindow>
#include <QSize>
#include <QTimer>

class KConfigGroup;

// Remembers how large an app window was and whether it was maximised, and
// gives it that size back the next time the app starts. Kept in
// ~/.config/wildoswindowsrc under the app's desktop file name and `key`,
// so each kind of window an app has keeps its own size.
//
// Only the size: the compositor places windows on Wayland. A window whose
// width or height is fixed (smallest equals largest) is a dialog and is
// left alone, and so is a window with no `key` unless it is the first one
// the app made, so a preview or properties window never takes the main
// window's size.
class WindowMemory : public QObject, public QQmlParserStatus
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    QML_ELEMENT

    Q_PROPERTY(QQuickWindow *window READ window WRITE setWindow NOTIFY windowChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(QString key READ key WRITE setKey NOTIFY keyChanged)

public:
    explicit WindowMemory(QObject *parent = nullptr);

    QQuickWindow *window() const { return m_window; }
    void setWindow(QQuickWindow *window);
    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    QString key() const { return m_key; }
    void setKey(const QString &key);

    void classBegin() override {}
    void componentComplete() override;

Q_SIGNALS:
    void windowChanged();
    void enabledChanged();
    void keyChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    bool active() const;
    KConfigGroup entry() const;
    QSize fitted(const QSize &size) const;
    void restore();
    void shown(bool visible);
    void save();

    QPointer<QQuickWindow> m_window;
    bool m_enabled = true;
    QString m_key;
    const bool m_first;
    bool m_complete = false;
    bool m_maximiseOnShow = false;
    // What was last read or written, so nothing is saved until it changes.
    QSize m_normal;
    bool m_maximised = false;
    QTimer m_settle;
};
