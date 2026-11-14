#pragma once

#include <QDateTime>
#include <QHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QObject>
#include <QTimer>

class QSocketNotifier;

// "What the firewall blocked". The firewall's last rule hands a few new
// connections a minute to netlink log group 47 (settings-firewall); this
// reads them there rather than from the kernel log, so they never reach
// the journal, which would keep them longer than the seven days Settings
// promises. Each is written to a file Settings reads, with who sent it put
// into words from the networks the firewall knew at that moment.
class BlockedLog : public QObject
{
    Q_OBJECT

public:
    explicit BlockedLog(QObject *parent = nullptr);
    ~BlockedLog() override;

    void clear();

private:
    void open();
    void read();
    void record(const QByteArray &packet, QString device);
    void prune();
    QString kindOf(const QHostAddress &source, QString *device);

    int m_fd = -1;
    QSocketNotifier *m_notifier = nullptr;
    QTimer m_prune;
    QTimer m_retry;
    // A refused connection is tried a few times over; one attempt is one
    // line.
    QHash<QString, qint64> m_recent;
    QJsonArray m_networks;
    QDateTime m_stateRead;
};
