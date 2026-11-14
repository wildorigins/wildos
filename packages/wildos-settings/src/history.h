#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QQmlEngine>
#include <QTimer>
#include <QUrl>
#include <QVariantList>

class QLocalSocket;

// Settings > System > History. Reads the records settings-history keeps
// as root (/var/lib/wildos/history: what anyone may see, and this
// account's own), plus this account's Backup runs, which Backup keeps in
// the home folder. Asks settings-history to catch up through its socket
// each time the page opens. The words for each kind of event are in
// HistoryWords.qml; nothing here leaves the PC.
class History : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // Newest first: {id, when (date), kind, data (map), tech (list of
    // {when, source, text})}. Only the last 30 days.
    Q_PROPERTY(QVariantList events READ events NOTIFY eventsChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    // "" when all is well; "stale" when the latest events could not be
    // gathered (what is shown may be missing some); "unreadable" when
    // nothing could be read at all.
    Q_PROPERTY(QString problem READ problem NOTIFY eventsChanged)
    // Why, in the words settings-history or the system gave.
    Q_PROPERTY(QString problemDetail READ problemDetail NOTIFY eventsChanged)

public:
    explicit History(QObject *parent = nullptr);

    QVariantList events() const { return m_events; }
    bool loading() const { return m_loading; }
    QString problem() const { return m_problem; }
    QString problemDetail() const { return m_problemDetail; }

    // Ask settings-history to catch up, then read the records again.
    Q_INVOKABLE void refresh();
    // Writes the text to the file chosen; false when it could not.
    Q_INVOKABLE bool saveTo(const QUrl &file, const QString &text);
    // Report a problem, with this event attached: title, when, the words
    // shown, the technical lines, and the program for a crash.
    Q_INVOKABLE bool report(const QVariantMap &event, const QString &text);
    // Report a problem on its own, from the summary at the top.
    Q_INVOKABLE bool openReport() const;
    Q_INVOKABLE QUrl defaultSaveFile() const;

Q_SIGNALS:
    void eventsChanged();
    void loadingChanged();

private:
    void load();
    void readFile(const QString &path, QVariantList &into, bool *readable);
    void readBackups(QVariantList &into);
    void watch();

    QFileSystemWatcher m_watcher;
    QTimer m_settle;
    QLocalSocket *m_socket = nullptr;
    QTimer m_timeout;
    QVariantList m_events;
    bool m_loading = false;
    QString m_problem;
    QString m_problemDetail;
    QString m_reply;
};
