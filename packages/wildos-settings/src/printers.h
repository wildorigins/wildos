#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QVariantList>

#include <atomic>
#include <functional>
#include <memory>

// Printers through CUPS: the queues on this computer with their state, the
// printers CUPS finds on the network (DNS-SD, the way IPP Everywhere
// printers announce themselves), adding one driverless with lpadmin's
// "everywhere" model, the account's default printer (lpoptions, as Windows
// keeps the default per person), a test page, and removing a queue.
//
// More printer settings adds the rest of what KDE's print manager did: each
// printer's queue with a Cancel per document, pausing a printer, printing
// preferences kept per person in lpoptions like the default printer, and
// adding a printer that needs a driver, chosen from the PPDs CUPS knows
// (CUPS-Get-PPDs) for a device CUPS finds (CUPS-Get-Devices, lpinfo -v).
//
// Printers and scanners also shows ink and toner levels, from the queue
// (marker-levels, which CUPS copies from the printer while it prints) or,
// for a driverless printer CUPS has not printed to yet, asked of the
// printer itself. Scanners come from sane-airscan's discovery of eSCL and
// WSD scanners on the network: one that is part of a printer marks that
// printer as one that scans; one on its own can be added to the list,
// which only remembers it for this account, since sane-airscan finds it
// without being told.
//
// Adding and removing are CUPS administration. Members of lpadmin (every
// administrator Wild OS creates) are let in by CUPS itself over the local
// socket; anyone else is asked for an administrator's password through
// pkexec.
class Printers : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(QVariantList printers READ printers NOTIFY changed)
    Q_PROPERTY(QVariantList found READ found NOTIFY foundChanged)
    Q_PROPERTY(bool searching READ searching NOTIFY foundChanged)
    Q_PROPERTY(QString busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(bool findingDevices READ findingDevices NOTIFY devicesChanged)
    Q_PROPERTY(bool driversReady READ driversReady NOTIFY driversChanged)
    Q_PROPERTY(bool loadingDrivers READ loadingDrivers NOTIFY driversChanged)
    // Network scanners added to the list: { name, url, seen }, where seen
    // says the last look found it.
    Q_PROPERTY(QVariantList scanners READ scanners NOTIFY scannersChanged)
    // Network scanners found that are neither added nor part of a printer
    // in the list or in `found`: { name, url }.
    Q_PROPERTY(QVariantList foundScanners READ foundScanners NOTIFY scannersChanged)
    Q_PROPERTY(bool findingScanners READ findingScanners NOTIFY scannersChanged)
    // At least one look for scanners has finished, so a scanner not seen is
    // really missing.
    Q_PROPERTY(bool scannersLooked READ scannersLooked NOTIFY scannersChanged)

public:
    explicit Printers(QObject *parent = nullptr);
    ~Printers() override;

    bool available() const { return m_available; }
    QVariantList printers() const { return m_printers; }
    QVariantList found() const { return m_found; }
    bool searching() const { return m_searching; }
    QString busy() const { return m_busy; }
    QVariantList devices() const { return m_devices; }
    bool findingDevices() const { return m_findingDevices; }
    bool driversReady() const { return !m_drivers.isEmpty(); }
    bool loadingDrivers() const { return m_loadingDrivers; }
    QVariantList scanners() const;
    QVariantList foundScanners() const;
    bool findingScanners() const { return m_findingScanners; }
    bool scannersLooked() const { return m_scannersLooked; }

    Q_INVOKABLE void reload();
    // Looks for printers (CUPS's DNS-SD browse) and scanners at once.
    Q_INVOKABLE void search();
    Q_INVOKABLE void stopSearch();
    // uri is a device URI from `found` (dnssd://...) or ipp://host/...
    Q_INVOKABLE void add(const QString &uri, const QString &name);
    // A printer by its network name or IP address, for one that does not
    // announce itself.
    Q_INVOKABLE void addAddress(const QString &address);
    Q_INVOKABLE void setDefault(const QString &name, bool announce = true);
    Q_INVOKABLE void printTestPage(const QString &name);
    Q_INVOKABLE void remove(const QString &name);
    Q_INVOKABLE void resume(const QString &name);
    Q_INVOKABLE void cancelJobs(const QString &name);
    Q_INVOKABLE void cancelJob(const QString &name, int job);
    Q_INVOKABLE void pause(const QString &name);

    // Paper size, two-sided and colour for one printer: { media, sides,
    // color } each { value, choices: [{ value, text }] }, a choice missing
    // when the printer has only one.
    Q_INVOKABLE QVariantMap preferences(const QString &name) const;
    // option is media, sides or print-color-mode.
    Q_INVOKABLE void setPreference(const QString &name, const QString &option, const QString &value);

    // Devices CUPS can reach, USB and network, { uri, name, model, kind }.
    Q_INVOKABLE void findDevices();
    Q_INVOKABLE void loadDrivers();
    Q_INVOKABLE QStringList makes() const;
    // { value: ppd-name, text: make and model } for one make.
    Q_INVOKABLE QVariantList models(const QString &make) const;
    // The make CUPS's driver list uses for a model string from a device,
    // or nothing.
    Q_INVOKABLE QString makeFor(const QString &model) const;
    // uri from `devices`, or built from an address with socket, lpd or ipp.
    Q_INVOKABLE void addWithDriver(const QString &uri, const QString &driver, const QString &name);
    Q_INVOKABLE QString addressUri(const QString &address, const QString &protocol) const;

    Q_INVOKABLE void addScanner(const QString &name);
    Q_INVOKABLE void removeScanner(const QString &name);
    // Capture, on its Scan tab.
    Q_INVOKABLE bool openCapture() const;

Q_SIGNALS:
    void changed();
    void foundChanged();
    void busyChanged();
    void devicesChanged();
    void driversChanged();
    void scannersChanged();
    void failed(const QString &title, const QString &reason);
    void done(const QString &title, const QString &message);

private:
    void setBusy(const QString &busy);
    void admin(const QString &busy, const QString &program, const QStringList &arguments, const QString &successTitle, const QString &success,
               const QString &failure, const std::function<void()> &then = {}, const std::function<void()> &otherwise = {});
    QString queueName(const QString &name) const;
    void finishDevices(const QVariantList &devices);
    void findScanners();
    // Sets `scans` on each printer and found printer from the scanners seen.
    void markScanners();
    bool partOfPrinter(const QVariantMap &scanner) const;
    QStringList addedScanners() const;

    struct Driver {
        QString ppd;
        QString make;
        QString model;
    };

    bool m_available = false;
    QVariantList m_printers;
    QVariantList m_found;
    bool m_searching = false;
    QString m_busy;
    std::shared_ptr<std::atomic<int>> m_cancelSearch;
    int m_searchGeneration = 0;
    QVariantList m_devices;
    bool m_findingDevices = false;
    QList<Driver> m_drivers;
    bool m_loadingDrivers = false;
    QVariantList m_networkScanners;
    bool m_findingScanners = false;
    bool m_scannersLooked = false;
};
