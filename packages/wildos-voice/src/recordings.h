#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QList>
#include <QObject>
#include <QQmlEngine>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <utility>

// The recordings in the Recordings folder, newest first, each with its
// title, date, length, markers and transcript. The folder is watched, so a
// recording copied in or deleted in Files shows up here.
class Recordings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // [{ path, title, created (Date), duration (ms), markers [ms],
    //    hasTranscript, transcript }]
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
    Q_PROPERTY(bool loaded READ loaded NOTIFY itemsChanged)
    Q_PROPERTY(QString folder READ folder CONSTANT)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit Recordings(QObject *parent);
    static Recordings *instance();
    static Recordings *create(QQmlEngine *, QJSEngine *);

    QVariantList items() const { return m_items; }
    bool loaded() const { return m_loaded; }
    QString folder() const;
    QString lastError() const { return m_lastError; }

    // Everything known about one recording, in the Recordings folder or
    // opened from somewhere else.
    Q_INVOKABLE QVariantMap info(const QString &path) const;
    Q_INVOKABLE bool rename(const QString &path, const QString &title);
    // Moves the recording, its markers and its transcript to the bin.
    Q_INVOKABLE bool remove(const QString &path);
    // Undoes the last remove(): the recording, its markers and its
    // transcript come back out of the bin.
    Q_INVOKABLE bool restore(const QString &path);
    Q_INVOKABLE void showInFiles(const QString &path) const;
    // Reads the waveform in the background; waveformReady brings it.
    Q_INVOKABLE void loadWaveform(const QString &path);
    Q_INVOKABLE void refresh();
    // "Recording 5" when 1 to 4 are taken.
    QString nextTitle() const;
    // A new file in the Recordings folder, made if need be; empty if the
    // folder can't be made.
    QString newRecordingPath(QString *title);
    Q_INVOKABLE void forgetWaveform(const QString &path);

Q_SIGNALS:
    void itemsChanged();
    void lastErrorChanged();
    void waveformReady(const QString &path, const QVariantList &bars, double duration);
    void renamed(const QString &from, const QString &to);
    void removed(const QString &path);

private:
    void setError(const QString &message);
    void scheduleRefresh();

    QVariantList m_items;
    bool m_loaded = false;
    bool m_scanning = false;
    bool m_rescan = false;
    QString m_lastError;
    QFileSystemWatcher m_watcher;
    QTimer m_refreshTimer;
    QHash<QString, QVariantList> m_waveforms;
    // What remove() moved last, the recording first: where each file was
    // and where it went in the bin.
    QList<std::pair<QString, QString>> m_binned;
};
