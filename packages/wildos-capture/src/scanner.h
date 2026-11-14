#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QQuickImageProvider>
#include <QThread>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>

class ScanWorker;

// What a scanner can do, read from its SANE options when it is opened.
struct ScannerCaps {
    QString glassSource;
    QString feederSource;
    // SANE's own mode names for colour, grey and black and white; empty
    // when the scanner has no such mode.
    QString colourMode;
    QString greyMode;
    QString lineartMode;
    QList<int> resolutions;
    double maxWidthMm = 0;
    double maxHeightMm = 0;
};
Q_DECLARE_METATYPE(ScannerCaps)

// The pages scanned so far, kept for the image provider, which Qt calls
// from its own threads.
class ScanPages : public QQuickImageProvider
{
public:
    ScanPages();
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

    void set(int id, const QImage &image);
    QImage get(int id) const;
    void remove(int id);

private:
    mutable QMutex m_mutex;
    QHash<int, QImage> m_images;
};

// The Scan tab (CaptureScan.dc, board 15.30): finds scanners through SANE,
// driverless network ones first (eSCL and WSD through sane-airscan), then
// USB ones through SANE's backends; scans from the glass or the feeder;
// keeps the pages for rotating, cropping, marking up and removing; and
// writes them out as a PDF or as pictures. Every SANE call happens on one
// worker thread, so a scanner that takes its time never holds up the
// window.
class Scanner : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(bool looking READ looking NOTIFY devicesChanged)
    Q_PROPERTY(bool looked READ looked NOTIFY devicesChanged)
    Q_PROPERTY(QString device READ device NOTIFY deviceChanged)
    Q_PROPERTY(bool opening READ opening NOTIFY deviceChanged)
    Q_PROPERTY(bool hasGlass READ hasGlass NOTIFY deviceChanged)
    Q_PROPERTY(bool hasFeeder READ hasFeeder NOTIFY deviceChanged)
    Q_PROPERTY(QVariantList qualities READ qualities NOTIFY deviceChanged)
    Q_PROPERTY(QVariantList paperSizes READ paperSizes NOTIFY deviceChanged)

    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY settingsChanged)
    Q_PROPERTY(QString colour READ colour WRITE setColour NOTIFY settingsChanged)
    Q_PROPERTY(int dpi READ dpi WRITE setDpi NOTIFY settingsChanged)
    Q_PROPERTY(QString paper READ paper WRITE setPaper NOTIFY settingsChanged)
    Q_PROPERTY(bool straighten READ straighten WRITE setStraighten NOTIFY settingsChanged)

    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(bool cancelling READ cancelling NOTIFY scanningChanged)
    Q_PROPERTY(int scanningPage READ scanningPage NOTIFY scanningChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

    Q_PROPERTY(QVariantList pages READ pages NOTIFY pagesChanged)
    Q_PROPERTY(int current READ current WRITE setCurrent NOTIFY currentChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY pagesChanged)
    Q_PROPERTY(qint64 pdfSize READ pdfSize NOTIFY pdfSizeChanged)
    Q_PROPERTY(QVariantMap problem READ problem NOTIFY problemChanged)
    Q_PROPERTY(bool canSign READ canSign CONSTANT)

public:
    explicit Scanner(QObject *parent = nullptr);
    ~Scanner() override;

    ScanPages *images() const { return m_images; }

    QVariantList devices() const { return m_devices; }
    bool looking() const { return m_looking; }
    bool looked() const { return m_looked; }
    QString device() const { return m_device; }
    bool opening() const { return m_opening; }
    bool hasGlass() const { return !m_caps.glassSource.isEmpty() || m_caps.feederSource.isEmpty(); }
    bool hasFeeder() const { return !m_caps.feederSource.isEmpty(); }
    QVariantList qualities() const;
    QVariantList paperSizes() const;

    QString source() const { return m_source; }
    void setSource(const QString &source);
    QString colour() const { return m_colour; }
    void setColour(const QString &colour);
    int dpi() const { return m_dpi; }
    void setDpi(int dpi);
    QString paper() const { return m_paper; }
    void setPaper(const QString &paper);
    bool straighten() const { return m_straighten; }
    void setStraighten(bool on);

    bool scanning() const { return m_scanning; }
    bool cancelling() const { return m_cancelling; }
    int scanningPage() const { return m_scanningPage; }
    qreal progress() const { return m_progress; }
    bool busy() const { return m_busy; }

    QVariantList pages() const;
    int current() const { return m_current; }
    void setCurrent(int index);
    bool canUndo() const { return !m_history.isEmpty(); }
    qint64 pdfSize() const { return m_pdfSize; }
    QVariantMap problem() const { return m_problem; }
    bool canSign() const;

    Q_INVOKABLE void lookAgain();
    Q_INVOKABLE void choose(const QString &device);
    Q_INVOKABLE void scan();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void rotate();
    Q_INVOKABLE void crop(const QRectF &fraction);
    Q_INVOKABLE void removePage();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void clearProblem();
    // Mark up: the page goes to the Capture editor as a picture, and comes
    // back when the editor saves it.
    Q_INVOKABLE QString pageFile();
    Q_INVOKABLE void markedUp(const QString &path);
    Q_INVOKABLE void savePdf();
    Q_INVOKABLE void savePictures();
    Q_INVOKABLE void openToSign();
    Q_INVOKABLE void forget();

Q_SIGNALS:
    void devicesChanged();
    void deviceChanged();
    void settingsChanged();
    void scanningChanged();
    void progressChanged();
    void busyChanged();
    void pagesChanged();
    void currentChanged();
    void pdfSizeChanged();
    void problemChanged();
    // A short note for the window's toast. `file` is what Show in folder
    // shows, or "undo" when the note offers Undo instead.
    void said(const QString &message, const QString &file);

private:
    struct Page {
        int id;
        int rev;
        int dpi;
        QString note;
        // Size as a one-page PDF, measured in the background; -1 until then.
        qint64 pdfBytes = -1;
    };
    struct Undo {
        QString kind;
        int index;
        Page page;
        QImage image;
    };

    void startWorker();
    void setProblem(const QString &title, const QString &text);
    QString nameOf(const QString &id) const;
    void setBusy(bool on);
    void addPage(const QImage &image, int dpi, const QString &note);
    void changed(int index, const QImage &image, const QString &kind);
    void remember(const QString &kind, int index);
    void measure();
    void pickDefaults();
    QString nextName(const QString &folder, const QString &suffix) const;
    void writePdf(bool sign);
    void saveSetting(const char *key, const QVariant &value);

    QThread m_thread;
    ScanWorker *m_worker = nullptr;
    ScanPages *m_images;
    QVariantList m_devices;
    bool m_looking = false;
    bool m_looked = false;
    QString m_device;
    QString m_deviceName;
    bool m_opening = false;
    ScannerCaps m_caps;
    QString m_source = QStringLiteral("glass");
    QString m_colour = QStringLiteral("colour");
    int m_dpi = 300;
    QString m_paper;
    bool m_straighten = true;
    bool m_scanning = false;
    bool m_cancelling = false;
    int m_scanningPage = 0;
    int m_pagesThisScan = 0;
    qreal m_progress = -1;
    bool m_busy = false;
    QList<Page> m_pages;
    QList<Undo> m_history;
    int m_current = -1;
    int m_nextId = 1;
    qint64 m_pdfSize = 0;
    QVariantMap m_problem;
    QTimer m_measureTimer;
    QString m_workDir;
};
