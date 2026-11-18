#pragma once

#include <QImage>
#include <QObject>
#include <QVariant>

#include <memory>

class QOffscreenSurface;
class QOpenGLContext;
class QOpenGLFramebufferObject;
class QQmlEngine;
class QQuickItem;
class QQuickRenderControl;
class QQuickWindow;

// Draws a face from WildOS.Cooler.Faces into a picture without ever showing
// a window. Nothing in here runs on a clock: each call to render() draws the
// face as it is at the time it is given, so a caller can step a face at the
// cooler's own frame rate, or ask for the same frame twice and get the same
// picture.
class FaceRenderer : public QObject
{
    Q_OBJECT

public:
    explicit FaceRenderer(int size, QObject *parent = nullptr);
    ~FaceRenderer() override;

    bool isValid() const { return m_error.isEmpty(); }
    QString errorString() const { return m_error; }

    bool setFace(const QString &id);
    void setReadings(const QVariantMap &readings);
    void setThemeColours(const QVariant &colours);
    void setShowMark(bool show);
    // Off for a screen that is round itself: the corners are then left drawn
    // and the face costs one pass less.
    void setCircular(bool circular);

    QImage render(qreal seconds);

    // For a caller that wants to bind something of its own to the face, as
    // the cooler's screen does with the theme: the engine the face lives in
    // and the FaceView itself. Both are null when isValid() was false from
    // the start.
    QQmlEngine *engine() const { return m_engine.get(); }
    QQuickItem *view() const { return m_view; }

private:
    int m_size;
    QString m_error;
    std::unique_ptr<QOpenGLContext> m_context;
    std::unique_ptr<QOffscreenSurface> m_surface;
    std::unique_ptr<QQuickRenderControl> m_control;
    std::unique_ptr<QQuickWindow> m_window;
    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<QOpenGLFramebufferObject> m_target;
    QQuickItem *m_view = nullptr;
};
