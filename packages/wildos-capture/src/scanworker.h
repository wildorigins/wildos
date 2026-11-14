#pragma once

#include "scanner.h"

#include <QImage>
#include <QObject>
#include <QVariantMap>

#include <atomic>

// Every SANE call, on the Scan tab's own thread. SANE is not made for
// calls from several threads at once, apart from sane_cancel, which the
// window may call at any time to stop a page part way.
class ScanWorker : public QObject
{
    Q_OBJECT

public:
    ScanWorker();
    ~ScanWorker() override;

    // From the window's thread, while a scan runs.
    void cancelNow();

public Q_SLOTS:
    void start();
    void listDevices();
    void open(const QString &name);
    // { device, source, mode, dpi, widthMm, heightMm, feeder, straighten,
    //   lineart }: `lineart` asks for black and white made here from grey,
    //   for scanners without a mode of their own for it.
    void scan(const QVariantMap &settings);
    void stop();

Q_SIGNALS:
    void devicesFound(const QVariantList &devices);
    void opened(const QString &name, const ScannerCaps &caps);
    void openFailed(const QString &name, const QString &outcome);
    void pageStarted(int page);
    void progress(qreal fraction);
    void pageScanned(const QImage &image, int dpi, const QString &note);
    // done, cancelled, busy, offline, empty, jammed, cover or failed.
    void finished(const QString &outcome, int pages);

private:
    bool ensureOpen(const QString &name, QString *outcome);
    void closeDevice();
    ScannerCaps readCaps();
    int optionIndex(const char *name) const;
    bool setString(const char *name, const QString &value);
    bool setNumber(const char *name, double value);
    QImage readPage(QString *outcome);

    bool m_initialised = false;
    QString m_name;
    std::atomic<void *> m_handle{nullptr};
    std::atomic<bool> m_cancel{false};
};
