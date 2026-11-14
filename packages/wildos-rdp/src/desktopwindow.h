#pragma once

#include <QBackingStore>
#include <QImage>
#include <QList>
#include <QTimer>
#include <QWindow>

#include <memory>

class DesktopSession;

// The whole Windows desktop in one window, for "Open Windows desktop" in
// Settings: the engine's desktop session drawn at the window's size, with
// the keyboard and pointer going to Windows. Windows lays its desktop out
// again for each new window size.
//
// Windows mode's own VM is trusted the way its app windows trust it: a
// certificate seen for the first time is kept, and a changed one is refused
// (Windows mode forgets the old one itself when Windows is set up again).
class DesktopWindow : public QWindow
{
    Q_OBJECT

public:
    explicit DesktopWindow(const QString &title);
    ~DesktopWindow() override;

    // FreeRDP's arguments as xfreerdp3 takes them, the password among them.
    bool start(const QList<QByteArray> &args, QString *error);

Q_SIGNALS:
    // The session ended; `code` is FreeRDP's reason, empty when it ended
    // normally.
    void ended(const QString &code);

protected:
    bool event(QEvent *event) override;
    void exposeEvent(QExposeEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    void damaged(const QRegion &region);
    void render();
    QRectF pictureRect() const;
    QPoint toRemote(const QPointF &local) const;
    QSize physicalSize() const;

    DesktopSession *m_session;
    std::unique_ptr<QBackingStore> m_store;
    QImage m_image;
    QTimer m_resize;
    QList<quint32> m_keysDown;
    int m_wheelX = 0;
    int m_wheelY = 0;
    bool m_connected = false;
};
