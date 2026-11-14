#pragma once

#include <QHash>
#include <QObject>
#include <QStringList>

class QProcess;

// How much room the Wild OS section's apps take, read from dpkg and apt
// rather than any list of our own. An installed package counts with what
// only it needs, the parts apt would take away with it. A removed one
// counts with everything putting it back would install. Both are what an
// uninstall or a reinstall would really change on this PC.
class PackageSizes : public QObject
{
    Q_OBJECT

public:
    explicit PackageSizes(QObject *parent = nullptr);

    // Works the sizes out again for these packages, after anything was
    // installed or removed.
    void refresh(const QStringList &packages);
    // Bytes, or -1 while not known.
    qint64 size(const QString &package) const { return m_sizes.value(package, -1); }

Q_SIGNALS:
    void changed();

private:
    void readInstalled();
    void simulateNext();
    void readCandidates();
    void finish();
    QProcess *run(const QString &program, const QStringList &args);

    QStringList m_wanted;
    QHash<QString, qint64> m_sizes;
    QHash<QString, qint64> m_next;
    QStringList m_missing;
    QHash<QString, QStringList> m_brings;
    bool m_running = false;
    bool m_again = false;
};
