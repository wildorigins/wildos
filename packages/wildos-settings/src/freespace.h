#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QStringList>
#include <QVariantMap>

class QProcess;

// System > Storage > Free up space: what each item would give back, and
// clearing it. Measuring and clearing are settings-free-space's: its system
// half as root through pkexec, its home half as the person.
class FreeSpace : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool measuring READ measuring NOTIFY changed)
    Q_PROPERTY(bool cleaning READ cleaning NOTIFY changed)
    // Each half's JSON, as settings-free-space prints it. Empty until
    // measured, or when that half failed.
    Q_PROPERTY(QVariantMap system READ system NOTIFY changed)
    Q_PROPERTY(QVariantMap home READ home NOTIFY changed)
    Q_PROPERTY(bool systemFailed READ systemFailed NOTIFY changed)
    // The disk the system is on.
    Q_PROPERTY(double diskTotal READ diskTotal NOTIFY changed)
    Q_PROPERTY(double diskFree READ diskFree NOTIFY changed)
    // The person's ticks (never Downloads, which is chosen in Review each
    // time) and Tidy up automatically, from ~/.config/wildos/free-space.json.
    Q_PROPERTY(bool hasSavedItems READ hasSavedItems NOTIFY prefsChanged)
    Q_PROPERTY(QStringList savedItems READ savedItems NOTIFY prefsChanged)
    Q_PROPERTY(bool autoOn READ autoOn NOTIFY prefsChanged)

public:
    explicit FreeSpace(QObject *parent = nullptr);

    bool measuring() const { return m_pending > 0; }
    bool cleaning() const { return m_cleaning; }
    QVariantMap system() const { return m_system; }
    QVariantMap home() const { return m_home; }
    bool systemFailed() const { return m_systemFailed; }
    double diskTotal() const { return m_diskTotal; }
    double diskFree() const { return m_diskFree; }
    bool hasSavedItems() const { return m_hasSavedItems; }
    QStringList savedItems() const { return m_savedItems; }
    bool autoOn() const { return m_autoOn; }

    Q_INVOKABLE void measure();
    // Clears the ticked items; downloads are the paths ticked in Review.
    Q_INVOKABLE void clean(const QStringList &items, const QStringList &downloads);
    Q_INVOKABLE void saveItems(const QStringList &items);
    // Asks for the password to store which system items it may clear,
    // then switches the person's timer on or off.
    Q_INVOKABLE void setAuto(bool on, const QStringList &items);

Q_SIGNALS:
    void changed();
    void prefsChanged();
    // settings-free-space run's JSON: {items: {item: {freed, removed,
    // skipped}}, freed}, or {cancelled: true}.
    void cleaned(const QVariantMap &result);

private:
    void start(const QString &program, const QStringList &args, bool system);
    void readDisk();
    void readPrefs();
    void writePrefs();
    static QStringList systemPart(const QStringList &items);

    int m_pending = 0;
    bool m_cleaning = false;
    QVariantMap m_system;
    QVariantMap m_home;
    bool m_systemFailed = false;
    double m_diskTotal = 0;
    double m_diskFree = 0;
    bool m_hasSavedItems = false;
    QStringList m_savedItems;
    bool m_autoOn = false;
};
