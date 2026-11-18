#pragma once

#include "backend.h"

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

// Everything the Cooler window shows and changes. The cooler itself is
// behind Cooler::Backend; the user's choices are kept in
// ~/.config/wildoscoolerrc and handed to the backend as they change.
// Nothing else is written, and nothing goes online.
class CoolerApp : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool present READ present NOTIFY presentChanged)
    Q_PROPERTY(QString model READ model NOTIFY deviceChanged)
    Q_PROPERTY(int screenSize READ screenSize NOTIFY deviceChanged)
    Q_PROPERTY(QString firmware READ firmware NOTIFY deviceChanged)
    Q_PROPERTY(int fanCount READ fanCount NOTIFY deviceChanged)
    Q_PROPERTY(bool fansConnected READ fansConnected NOTIFY deviceChanged)
    Q_PROPERTY(bool hasLighting READ hasLighting NOTIFY deviceChanged)
    // The WildOS.Cooler.Faces module is installed, so previews can draw
    // the real faces.
    Q_PROPERTY(bool facesAvailable READ facesAvailable CONSTANT)

    Q_PROPERTY(double cpuTemperature READ cpuTemperature NOTIFY readingsChanged)
    Q_PROPERTY(double cpuHighestToday READ cpuHighestToday NOTIFY readingsChanged)
    Q_PROPERTY(double liquidTemperature READ liquidTemperature NOTIFY readingsChanged)
    // Negative when there is no estimate.
    Q_PROPERTY(double roomTemperature READ roomTemperature NOTIFY readingsChanged)
    Q_PROPERTY(int pumpRpm READ pumpRpm NOTIFY readingsChanged)
    Q_PROPERTY(int pumpPercent READ pumpPercent NOTIFY readingsChanged)
    Q_PROPERTY(int fanRpm READ fanRpm NOTIFY readingsChanged)
    Q_PROPERTY(int fanPercent READ fanPercent NOTIFY readingsChanged)
    Q_PROPERTY(double cpuLoad READ cpuLoad NOTIFY readingsChanged)
    Q_PROPERTY(double gpuTemperature READ gpuTemperature NOTIFY readingsChanged)
    // The same readings by name, as the faces take them.
    Q_PROPERTY(QVariantMap readings READ readings NOTIFY readingsChanged)

    Q_PROPERTY(QString face READ face WRITE setFace NOTIFY faceChanged)
    Q_PROPERTY(int brightness READ brightness WRITE setBrightness NOTIFY brightnessChanged)
    Q_PROPERTY(int rotation READ rotation WRITE setRotation NOTIFY rotationChanged)
    Q_PROPERTY(bool showMark READ showMark WRITE setShowMark NOTIFY showMarkChanged)
    Q_PROPERTY(bool themeColours READ themeColours WRITE setThemeColours NOTIFY themeColoursChanged)
    // "cpu", "gpu" or "both": whose temperature the faces lead with.
    Q_PROPERTY(QString temperature READ temperature WRITE setTemperature NOTIFY temperatureChanged)
    Q_PROPERTY(bool dimWhenIdle READ dimWhenIdle WRITE setDimWhenIdle NOTIFY dimWhenIdleChanged)
    Q_PROPERTY(int warnLiquidAbove READ warnLiquidAbove WRITE setWarnLiquidAbove NOTIFY warningChanged)
    Q_PROPERTY(int warnCpuLimitSeconds READ warnCpuLimitSeconds WRITE setWarnCpuLimitSeconds NOTIFY warningChanged)

    // quiet, balanced, performance or custom.
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)
    // The curves the chosen mode runs, as lists of { temperature, percent }.
    Q_PROPERTY(QVariantList fanCurve READ fanCurve NOTIFY curvesChanged)
    Q_PROPERTY(QVariantList pumpCurve READ pumpCurve NOTIFY curvesChanged)
    Q_PROPERTY(int fanStopBelow READ fanStopBelow CONSTANT)
    Q_PROPERTY(bool stopFansWhenCool READ stopFansWhenCool WRITE setStopFansWhenCool NOTIFY optionsChanged)
    Q_PROPERTY(int pumpMinimum READ pumpMinimum WRITE setPumpMinimum NOTIFY optionsChanged)
    Q_PROPERTY(bool fullSpeedInGames READ fullSpeedInGames WRITE setFullSpeedInGames NOTIFY optionsChanged)

    // Where the report's save dialog opens.
    Q_PROPERTY(QUrl reportFolder READ reportFolder CONSTANT)

public:
    // No default constructor: QML would make a second one instead of
    // calling create().
    explicit CoolerApp(Cooler::Backend *backend, QObject *parent);
    static CoolerApp *instance();
    static CoolerApp *create(QQmlEngine *, QJSEngine *);

    void setEngine(QQmlEngine *engine);
    void setMainWindow(QQuickWindow *window);
    // A second start brings the open window forward.
    void activate();

    bool present() const { return m_backend->present(); }
    QString model() const { return m_backend->model(); }
    int screenSize() const { return m_backend->screenSize(); }
    QString firmware() const { return m_backend->firmware(); }
    int fanCount() const { return m_backend->fanCount(); }
    bool fansConnected() const { return m_backend->fanCount() > 0; }
    bool hasLighting() const { return m_backend->hasLighting(); }
    bool facesAvailable() const { return m_facesAvailable; }

    double cpuTemperature() const { return m_readings.cpuTemperature; }
    double cpuHighestToday() const { return m_readings.cpuHighestToday; }
    double liquidTemperature() const { return m_readings.liquidTemperature; }
    double roomTemperature() const { return m_readings.roomTemperature; }
    int pumpRpm() const { return m_readings.pumpRpm; }
    int pumpPercent() const { return m_readings.pumpPercent; }
    int fanRpm() const { return m_readings.fanRpm; }
    int fanPercent() const { return m_readings.fanPercent; }
    double cpuLoad() const { return m_readings.cpuLoad; }
    double gpuTemperature() const { return m_readings.gpuTemperature; }
    QVariantMap readings() const;

    QString face() const { return m_face; }
    void setFace(const QString &face);
    int brightness() const { return m_brightness; }
    void setBrightness(int percent);
    int rotation() const { return m_rotation; }
    void setRotation(int degrees);
    bool showMark() const { return m_showMark; }
    void setShowMark(bool show);
    bool themeColours() const { return m_themeColours; }
    QString temperature() const { return m_temperature; }
    void setTemperature(const QString &temperature);
    void setThemeColours(bool use);
    bool dimWhenIdle() const { return m_dimWhenIdle; }
    void setDimWhenIdle(bool dim);
    int warnLiquidAbove() const { return m_warnLiquidAbove; }
    void setWarnLiquidAbove(int temperature);
    int warnCpuLimitSeconds() const { return m_warnCpuLimitSeconds; }
    void setWarnCpuLimitSeconds(int seconds);

    QString mode() const { return m_mode; }
    void setMode(const QString &mode);
    QVariantList fanCurve() const;
    QVariantList pumpCurve() const;
    int fanStopBelow() const { return Cooler::fanStopBelow; }
    bool stopFansWhenCool() const { return m_stopFansWhenCool; }
    void setStopFansWhenCool(bool stop);
    int pumpMinimum() const { return m_pumpMinimum; }
    void setPumpMinimum(int percent);
    bool fullSpeedInGames() const { return m_fullSpeedInGames; }
    void setFullSpeedInGames(bool full);

    QUrl reportFolder() const;

    // The user moved a point: channel is "fans" or "pump", points a list of
    // { temperature, percent }. The mode becomes "custom".
    Q_INVOKABLE void setCurve(const QString &channel, const QVariantList &points);
    Q_INVOKABLE void lookAgain();
    Q_INVOKABLE void putEverythingBack();
    // Writes the report to the file the user chose; false if it could not.
    Q_INVOKABLE bool saveReport(const QUrl &file);

Q_SIGNALS:
    void presentChanged();
    void deviceChanged();
    void readingsChanged();
    void faceChanged();
    void brightnessChanged();
    void rotationChanged();
    void showMarkChanged();
    void themeColoursChanged();
    void temperatureChanged();
    void dimWhenIdleChanged();
    void warningChanged();
    void modeChanged();
    void curvesChanged();
    void optionsChanged();

private:
    // The user's choice where there is one, else what the cooler reports.
    void load();
    void handOver();
    void store(const char *group, const char *key, const QVariant &value);
    Cooler::Curve shownCurve(Cooler::Channel channel) const;
    QString reportText() const;

    Cooler::Backend *m_backend;
    QQmlEngine *m_engine = nullptr;
    QPointer<QQuickWindow> m_window;
    bool m_facesAvailable = false;
    Cooler::Readings m_readings;

    QString m_face;
    int m_brightness = 0;
    int m_rotation = 0;
    bool m_showMark = false;
    bool m_themeColours = false;
    QString m_temperature;
    bool m_dimWhenIdle = false;
    int m_warnLiquidAbove = 0;
    int m_warnCpuLimitSeconds = 0;
    QString m_mode;
    Cooler::Curve m_customFans;
    Cooler::Curve m_customPump;
    bool m_stopFansWhenCool = false;
    int m_pumpMinimum = 0;
    bool m_fullSpeedInGames = false;
};
