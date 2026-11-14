#pragma once

#include <QObject>
#include <QQmlEngine>

class QFileSystemWatcher;

// The processor's thread and flaw protection choices. Both are boot options
// that /etc/default/grub.d/40_wildos.cfg builds from files settings-cpu-threads
// writes, so what was chosen and what the running kernel does can differ
// until the next start-up.
//
// "Use every processor thread" (hyper-threading), in System > Power and
// battery, with shortcuts from Windows mode. Until it is touched the kernel
// decides: every thread, except on processors it lists as unsafe for sharing
// a core.
//
// "Extra processor protection", in Privacy and security: Kicksecure's full
// set of processor flaw protections, forced on whether or not the processor
// needs them, with the second thread of every core off. Off by default;
// Strict and Maximum protection always have it.
class Processor : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // Whether the processor has two threads per core at all.
    Q_PROPERTY(bool threadsAvailable READ threadsAvailable CONSTANT)
    // What the switch shows: the choice, or what the kernel does until one
    // is made.
    Q_PROPERTY(bool everyThread READ everyThread NOTIFY changed)
    // What the running kernel does.
    Q_PROPERTY(bool everyThreadActive READ everyThreadActive NOTIFY changed)
    // A restart is needed for the thread choice to take effect.
    Q_PROPERTY(bool threadsPending READ threadsPending NOTIFY changed)
    // Extra processor protection, Strict or Maximum, which keep it off.
    Q_PROPERTY(bool everyThreadLocked READ everyThreadLocked NOTIFY changed)
    // Extra processor protection is chosen, or comes with Strict or Maximum.
    Q_PROPERTY(bool extraProtection READ extraProtection NOTIFY changed)
    // Strict or Maximum protection, which keep extra protection on.
    Q_PROPERTY(bool extraProtectionLocked READ extraProtectionLocked NOTIFY changed)
    // A restart is needed for extra protection to take effect.
    Q_PROPERTY(bool extraProtectionPending READ extraProtectionPending NOTIFY changed)
    // A change is waiting for the password or being written.
    Q_PROPERTY(bool busy READ busy NOTIFY changed)

public:
    explicit Processor(QObject *parent = nullptr);

    bool threadsAvailable() const;
    bool everyThread() const;
    bool everyThreadActive() const;
    bool threadsPending() const;
    bool everyThreadLocked() const;
    bool extraProtection() const;
    bool extraProtectionLocked() const;
    bool extraProtectionPending() const;
    bool busy() const { return m_busy; }

    Q_INVOKABLE void setEveryThread(bool on);
    Q_INVOKABLE void setExtraProtection(bool on);

Q_SIGNALS:
    void changed();
    void failed(const QString &message);

private:
    void run(const QStringList &args);

    QFileSystemWatcher *m_watcher = nullptr;
    bool m_busy = false;
};
