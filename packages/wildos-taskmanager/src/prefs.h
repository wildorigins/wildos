#pragma once

#include <QObject>
#include <QQmlEngine>

// Task Manager's own settings (~/.config/wildos-taskmanagerrc) and the few
// things it starts: "Run new task" and the Settings pages it links to.
class Prefs : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString startPage READ startPage WRITE setStartPage NOTIFY changed)
    // "high" (twice a second), "normal" (every second), "low" (every four
    // seconds) or "paused", as in Windows.
    Q_PROPERTY(QString updateSpeed READ updateSpeed WRITE setUpdateSpeed NOTIFY changed)
    Q_PROPERTY(int updateInterval READ updateInterval NOTIFY changed)
    Q_PROPERTY(bool showSystem READ showSystem WRITE setShowSystem NOTIFY changed)

public:
    explicit Prefs(QObject *parent = nullptr);

    QString startPage() const { return m_startPage; }
    void setStartPage(const QString &page);
    QString updateSpeed() const { return m_updateSpeed; }
    void setUpdateSpeed(const QString &speed);
    int updateInterval() const;
    bool showSystem() const { return m_showSystem; }
    void setShowSystem(bool show);

    // Returns an empty string when the command started, else why not.
    Q_INVOKABLE QString runTask(const QString &command);
    Q_INVOKABLE void openSettings(const QString &page);

    // How a table was last sorted: "processes", "startup", "windowsapps" or
    // "services", by a column's id.
    Q_INVOKABLE QString sortColumn(const QString &table, const QString &fallback) const;
    Q_INVOKABLE bool sortDescending(const QString &table) const;
    Q_INVOKABLE void setSort(const QString &table, const QString &column, bool descending);

Q_SIGNALS:
    void changed();

private:
    void save();

    QString m_startPage = QStringLiteral("processes");
    QString m_updateSpeed = QStringLiteral("normal");
    bool m_showSystem = true;
};
