#pragma once

#include <QHash>
#include <QObject>
#include <QRectF>
#include <QString>

// The part of each screen that windows maximise into: the screen less the
// taskbar and any other panel. Windows needs it to keep its apps clear of
// the taskbar, and Wayland does not say (QScreen::availableGeometry() is the
// whole screen there), so a small KWin script reports it, at once and every
// time a screen or panel changes. One script per process, named after it,
// however many windows the process shows.
class WorkAreas : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.wildos.RemoteApp.WorkArea")

public:
    explicit WorkAreas(QObject *parent = nullptr);
    ~WorkAreas() override;

    // Loads the script into KWin; without KWin nothing is ever reported.
    void start();
    // Unloads it again, waiting for KWin to hear so, as the process may be
    // about to end without returning from main().
    void stop();

    // By screen (connector) name, in logical units of the whole desktop.
    const QHash<QString, QRectF> &areas() const { return m_areas; }

public Q_SLOTS:
    // From the script: [{"name": s, "x": n, "y": n, "width": n, "height": n}].
    void Report(const QString &json);

Q_SIGNALS:
    void changed();

private:
    QHash<QString, QRectF> m_areas;
    QString m_plugin;
    QString m_file;
    bool m_registered = false;
    bool m_loaded = false;
};
