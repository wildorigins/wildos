#include "scanner.h"
#include "scanworker.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QBuffer>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>
#include <QThreadPool>
#include <QTransform>

#include <cmath>

namespace
{
KConfigGroup settings()
{
    return KSharedConfig::openConfig(QStringLiteral("wildosscreenshotrc"))->group(QStringLiteral("Scan"));
}

struct Paper {
    const char *id;
    const char *name;
    double width;
    double height;
};
// The sizes people scan; "glass" is all the glass can take.
const Paper papers[] = {
    {"a4", QT_TRANSLATE_NOOP("Scanner", "A4"), 210, 297},
    {"letter", QT_TRANSLATE_NOOP("Scanner", "Letter"), 215.9, 279.4},
    {"legal", QT_TRANSLATE_NOOP("Scanner", "Legal"), 215.9, 355.6},
    {"a5", QT_TRANSLATE_NOOP("Scanner", "A5"), 148, 210},
};

// Pages as they are written out: a copy, so a save can run on its own
// thread while the window goes on.
struct OutPage {
    QImage image;
    int dpi;
    bool bw;
};

bool isBlackAndWhite(const QImage &image)
{
    if (image.format() != QImage::Format_Grayscale8) {
        return false;
    }
    // A sample is enough: Black and white pages are made of 0 and 255 only.
    const int step = std::max(1, image.height() / 200);
    for (int y = 0; y < image.height(); y += step) {
        const uchar *line = image.constScanLine(y);
        for (int x = 0; x < image.width(); x += 3) {
            if (line[x] != 0 && line[x] != 255) {
                return false;
            }
        }
    }
    return true;
}

// Each page at its own size: the pixels over the resolution they were
// scanned at, so a printed copy comes out the size of the paper.
bool writePdfPages(QIODevice *device, const QList<OutPage> &pages, const QString &title)
{
    if (pages.isEmpty()) {
        return false;
    }
    QPdfWriter writer(device);
    writer.setCreator(QStringLiteral("Wild OS Capture"));
    writer.setTitle(title);
    QPainter painter;
    for (int i = 0; i < pages.size(); ++i) {
        const OutPage &page = pages.at(i);
        const QSizeF mm(page.image.width() * 25.4 / page.dpi, page.image.height() * 25.4 / page.dpi);
        writer.setPageSize(QPageSize(mm, QPageSize::Millimeter, QString(), QPageSize::ExactMatch));
        writer.setPageMargins(QMarginsF(0, 0, 0, 0));
        writer.setResolution(page.dpi);
        if (i == 0) {
            if (!painter.begin(&writer)) {
                return false;
            }
        } else if (!writer.newPage()) {
            return false;
        }
        // Black and white goes in without JPEG: Qt would otherwise store it
        // as a colour JPEG, seven times the size, with smudged edges.
        painter.setRenderHint(QPainter::LosslessImageRendering, page.bw);
        painter.drawImage(QRectF(QPointF(0, 0), QSizeF(page.image.size())), page.image);
    }
    return painter.end();
}

QString placeName(const QString &folder)
{
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (folder.startsWith(documents)) {
        return QCoreApplication::translate("Scanner", "Documents › Scans");
    }
    if (folder.startsWith(pictures)) {
        return QCoreApplication::translate("Scanner", "Pictures › Scans");
    }
    return QFileInfo(folder).fileName();
}

// Letter where it is the usual paper, A4 everywhere else.
QString defaultPaper()
{
    const QLocale::Territory place = QLocale::system().territory();
    const bool letter = place == QLocale::UnitedStates || place == QLocale::Canada || place == QLocale::Mexico || place == QLocale::Philippines
        || place == QLocale::Chile || place == QLocale::Colombia;
    return letter ? QStringLiteral("letter") : QStringLiteral("a4");
}
}

ScanPages::ScanPages()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage ScanPages::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    // image://scan/<page>/<revision>: the revision only makes the address
    // new when the page changes.
    const QImage image = get(id.section(QLatin1Char('/'), 0, 0).toInt());
    if (size) {
        *size = image.size();
    }
    if (image.isNull() || (requestedSize.width() <= 0 && requestedSize.height() <= 0)) {
        return image;
    }
    const QSize want = image.size().scaled(QSize(requestedSize.width() > 0 ? requestedSize.width() : INT_MAX,
                                                 requestedSize.height() > 0 ? requestedSize.height() : INT_MAX),
                                           Qt::KeepAspectRatio);
    return want.width() < image.width() ? image.scaled(want, Qt::KeepAspectRatio, Qt::SmoothTransformation) : image;
}

void ScanPages::set(int id, const QImage &image)
{
    QMutexLocker lock(&m_mutex);
    m_images.insert(id, image);
}

QImage ScanPages::get(int id) const
{
    QMutexLocker lock(&m_mutex);
    return m_images.value(id);
}

void ScanPages::remove(int id)
{
    QMutexLocker lock(&m_mutex);
    m_images.remove(id);
}

Scanner::Scanner(QObject *parent)
    : QObject(parent)
    , m_images(new ScanPages)
{
    qRegisterMetaType<ScannerCaps>();
    const KConfigGroup config = settings();
    m_paper = config.readEntry("Paper", defaultPaper());
    m_source = config.readEntry("Source", QStringLiteral("glass"));
    m_colour = config.readEntry("Colour", QStringLiteral("colour"));
    m_dpi = config.readEntry("Resolution", 300);
    m_straighten = config.readEntry("Straighten", true);
    m_workDir = QDir::tempPath() + QStringLiteral("/wildos-scan-") + QString::number(QCoreApplication::applicationPid());

    m_measureTimer.setSingleShot(true);
    m_measureTimer.setInterval(400);
    connect(&m_measureTimer, &QTimer::timeout, this, &Scanner::measure);
}

Scanner::~Scanner()
{
    if (m_worker) {
        m_worker->cancelNow();
        QMetaObject::invokeMethod(m_worker, &ScanWorker::stop, Qt::BlockingQueuedConnection);
        m_thread.quit();
        m_thread.wait();
        delete m_worker;
    }
    QDir(m_workDir).removeRecursively();
}

void Scanner::startWorker()
{
    if (m_worker) {
        return;
    }
    m_worker = new ScanWorker;
    m_worker->moveToThread(&m_thread);
    m_thread.setObjectName(QStringLiteral("scan"));
    m_thread.start();
    QMetaObject::invokeMethod(m_worker, &ScanWorker::start);

    connect(m_worker, &ScanWorker::devicesFound, this, [this](const QVariantList &found) {
        m_devices = found;
        m_looking = false;
        m_looked = true;
        Q_EMIT devicesChanged();
        // Found again by name: airscan numbers its scanners in the order
        // it hears them, so the same one can come back as e1 after e0.
        QString keep;
        const QString saved = settings().readEntry("Scanner", QString());
        for (const QString &want : {m_deviceName, saved}) {
            for (const QVariant &d : found) {
                const QVariantMap entry = d.toMap();
                if (keep.isEmpty() && !want.isEmpty() && entry.value(QStringLiteral("name")).toString() == want) {
                    keep = entry.value(QStringLiteral("id")).toString();
                }
            }
        }
        if (keep.isEmpty() && !found.isEmpty()) {
            keep = found.first().toMap().value(QStringLiteral("id")).toString();
        }
        m_deviceName = nameOf(keep);
        if (keep.isEmpty()) {
            m_device.clear();
            m_caps = {};
            m_opening = false;
            Q_EMIT deviceChanged();
            return;
        }
        // Opened again even when it is the same: listing let it go.
        m_device = keep;
        m_opening = true;
        Q_EMIT deviceChanged();
        QMetaObject::invokeMethod(m_worker, "open", Q_ARG(QString, keep));
    });
    connect(m_worker, &ScanWorker::opened, this, [this](const QString &name, const ScannerCaps &caps) {
        if (name != m_device) {
            return;
        }
        m_caps = caps;
        m_opening = false;
        pickDefaults();
        Q_EMIT deviceChanged();
    });
    connect(m_worker, &ScanWorker::openFailed, this, [this](const QString &name, const QString &outcome) {
        if (name != m_device) {
            return;
        }
        m_opening = false;
        m_caps = {};
        Q_EMIT deviceChanged();
        if (outcome == QLatin1String("busy")) {
            setProblem(tr("The scanner is busy"), tr("Another computer or app is using it. Try again when it has finished."));
        } else {
            setProblem(tr("Couldn't reach the scanner"), tr("Check it's switched on and connected, then choose it again."));
        }
    });
    connect(m_worker, &ScanWorker::pageStarted, this, [this](int) {
        m_scanningPage = int(m_pages.size()) + 1;
        Q_EMIT scanningChanged();
    });
    connect(m_worker, &ScanWorker::progress, this, [this](qreal fraction) {
        m_progress = fraction;
        Q_EMIT progressChanged();
    });
    connect(m_worker, &ScanWorker::pageScanned, this, [this](const QImage &image, int dpi, const QString &note) {
        ++m_pagesThisScan;
        addPage(image, dpi, note);
    });
    connect(m_worker, &ScanWorker::finished, this, [this](const QString &outcome, int pages) {
        m_scanning = false;
        m_cancelling = false;
        m_progress = -1;
        Q_EMIT scanningChanged();
        Q_EMIT progressChanged();
        const QString kept = pages > 0 ? tr(" The pages scanned before that are kept.") : QString();
        if (outcome == QLatin1String("cancelled")) {
            Q_EMIT said(pages == 1 ? tr("Scan stopped. The page scanned before that is kept.")
                            : pages > 1 ? tr("Scan stopped. The %1 pages scanned before that are kept.").arg(pages)
                                        : tr("Scan cancelled"),
                        QString());
        } else if (outcome == QLatin1String("busy")) {
            setProblem(tr("The scanner is busy"), tr("Another computer or app is using it. Try again when it has finished."));
        } else if (outcome == QLatin1String("offline")) {
            setProblem(tr("The scanner stopped answering"), tr("Check it's switched on and connected, then try again.") + kept);
            lookAgain();
        } else if (outcome == QLatin1String("empty")) {
            setProblem(tr("The feeder is empty"), tr("Put the pages in the feeder, then press Scan."));
        } else if (outcome == QLatin1String("jammed")) {
            setProblem(tr("Paper is stuck in the feeder"), tr("Clear it as the scanner shows, then scan the rest.") + kept);
        } else if (outcome == QLatin1String("cover")) {
            setProblem(tr("The scanner's lid is open"), tr("Close it, then try again."));
        } else if (outcome != QLatin1String("done")) {
            setProblem(tr("The scan didn't work"), tr("Try again. If it keeps happening, switch the scanner off and on.") + kept);
        }
    });
}

bool Scanner::canSign() const
{
    return !QStandardPaths::findExecutable(QStringLiteral("wildos-pdf")).isEmpty();
}

void Scanner::saveSetting(const char *key, const QVariant &value)
{
    KConfigGroup group = settings();
    group.writeEntry(key, value);
    group.sync();
}

void Scanner::setSource(const QString &source)
{
    if (m_source != source) {
        m_source = source;
        saveSetting("Source", source);
        Q_EMIT settingsChanged();
    }
}

void Scanner::setColour(const QString &colour)
{
    if (m_colour != colour) {
        m_colour = colour;
        saveSetting("Colour", colour);
        Q_EMIT settingsChanged();
        m_measureTimer.start();
    }
}

void Scanner::setDpi(int dpi)
{
    if (m_dpi != dpi) {
        m_dpi = dpi;
        saveSetting("Resolution", dpi);
        Q_EMIT settingsChanged();
    }
}

void Scanner::setPaper(const QString &paper)
{
    if (m_paper != paper) {
        m_paper = paper;
        saveSetting("Paper", paper);
        Q_EMIT settingsChanged();
    }
}

void Scanner::setStraighten(bool on)
{
    if (m_straighten != on) {
        m_straighten = on;
        saveSetting("Straighten", on);
        Q_EMIT settingsChanged();
    }
}

QVariantList Scanner::qualities() const
{
    const QList<int> offered = m_caps.resolutions.isEmpty() ? QList<int>{150, 300, 600} : m_caps.resolutions;
    struct Preset {
        int dpi;
        const char *name;
    };
    const Preset presets[] = {{150, QT_TR_NOOP("Draft")}, {300, QT_TR_NOOP("Document")}, {600, QT_TR_NOOP("Photo")}};
    QVariantList out;
    QList<int> used;
    for (const Preset &p : presets) {
        int nearest = offered.first();
        for (int r : offered) {
            if (std::abs(r - p.dpi) < std::abs(nearest - p.dpi)) {
                nearest = r;
            }
        }
        if (used.contains(nearest)) {
            continue;
        }
        used << nearest;
        out << QVariantMap{{QStringLiteral("value"), nearest}, {QStringLiteral("text"), tr("%1, %2 dpi").arg(tr(p.name)).arg(nearest)}};
    }
    return out;
}

QVariantList Scanner::paperSizes() const
{
    QVariantList out;
    for (const Paper &p : papers) {
        // A size the glass can't take isn't offered, with a little room
        // for glass that is a hair short of its paper.
        if (m_caps.maxWidthMm > 0 && (p.width > m_caps.maxWidthMm + 3 || p.height > m_caps.maxHeightMm + 3)) {
            continue;
        }
        out << QVariantMap{{QStringLiteral("value"), QString::fromLatin1(p.id)}, {QStringLiteral("text"), tr(p.name)}};
    }
    out << QVariantMap{{QStringLiteral("value"), QStringLiteral("glass")}, {QStringLiteral("text"), tr("All of the glass")}};
    return out;
}

void Scanner::pickDefaults()
{
    // From what was last chosen, not from what the previous scanner had
    // to fall back to: A4 picked on one scanner stays A4 on the next one
    // that can take it.
    const KConfigGroup config = settings();
    m_source = config.readEntry("Source", QStringLiteral("glass"));
    m_colour = config.readEntry("Colour", QStringLiteral("colour"));
    m_dpi = config.readEntry("Resolution", 300);
    m_paper = config.readEntry("Paper", defaultPaper());
    if (m_source == QLatin1String("feeder") && !hasFeeder()) {
        m_source = QStringLiteral("glass");
    } else if (m_source == QLatin1String("glass") && !hasGlass()) {
        m_source = QStringLiteral("feeder");
    }
    if (m_colour == QLatin1String("grey") && m_caps.greyMode.isEmpty() && !m_caps.colourMode.isEmpty()) {
        m_colour = QStringLiteral("colour");
    }
    const QVariantList q = qualities();
    bool found = false;
    int best = 0;
    for (const QVariant &v : q) {
        const int dpi = v.toMap().value(QStringLiteral("value")).toInt();
        found = found || dpi == m_dpi;
        if (best == 0 || std::abs(dpi - 300) < std::abs(best - 300)) {
            best = dpi;
        }
    }
    if (!found) {
        m_dpi = best;
    }
    bool paperFound = false;
    for (const QVariant &v : paperSizes()) {
        paperFound = paperFound || v.toMap().value(QStringLiteral("value")).toString() == m_paper;
    }
    if (!paperFound) {
        m_paper = QStringLiteral("glass");
    }
    Q_EMIT settingsChanged();
}

void Scanner::lookAgain()
{
    startWorker();
    if (m_looking || m_scanning) {
        return;
    }
    m_looking = true;
    Q_EMIT devicesChanged();
    QMetaObject::invokeMethod(m_worker, &ScanWorker::listDevices);
}

void Scanner::choose(const QString &device)
{
    startWorker();
    if (device == m_device || m_scanning) {
        return;
    }
    clearProblem();
    m_device = device;
    m_deviceName = nameOf(device);
    m_opening = true;
    m_caps = {};
    saveSetting("Scanner", m_deviceName);
    Q_EMIT deviceChanged();
    QMetaObject::invokeMethod(m_worker, "open", Q_ARG(QString, device));
}

QString Scanner::nameOf(const QString &id) const
{
    for (const QVariant &d : m_devices) {
        if (d.toMap().value(QStringLiteral("id")).toString() == id) {
            return d.toMap().value(QStringLiteral("name")).toString();
        }
    }
    return {};
}

void Scanner::setProblem(const QString &title, const QString &text)
{
    m_problem = {{QStringLiteral("title"), title}, {QStringLiteral("text"), text}};
    Q_EMIT problemChanged();
}

void Scanner::clearProblem()
{
    if (!m_problem.isEmpty()) {
        m_problem.clear();
        Q_EMIT problemChanged();
    }
}

void Scanner::setBusy(bool on)
{
    if (m_busy != on) {
        m_busy = on;
        Q_EMIT busyChanged();
    }
}

void Scanner::scan()
{
    if (m_scanning || m_device.isEmpty() || !m_worker) {
        return;
    }
    clearProblem();
    const bool feeder = m_source == QLatin1String("feeder") && hasFeeder();
    QString mode;
    bool lineart = false;
    if (m_colour == QLatin1String("colour")) {
        mode = m_caps.colourMode;
    } else if (m_colour == QLatin1String("grey")) {
        mode = m_caps.greyMode;
    } else {
        // Scanners' own black and white is often a coarse halftone, and
        // some have none; a grey scan split here reads better.
        mode = m_caps.greyMode.isEmpty() ? m_caps.lineartMode : m_caps.greyMode;
        lineart = !m_caps.greyMode.isEmpty() || m_caps.lineartMode.isEmpty();
    }
    double width = m_caps.maxWidthMm > 0 ? m_caps.maxWidthMm : 215.9;
    double height = m_caps.maxHeightMm > 0 ? m_caps.maxHeightMm : 297;
    for (const Paper &p : papers) {
        if (m_paper == QLatin1String(p.id)) {
            width = m_caps.maxWidthMm > 0 ? std::min(p.width, m_caps.maxWidthMm) : p.width;
            height = m_caps.maxHeightMm > 0 ? std::min(p.height, m_caps.maxHeightMm) : p.height;
        }
    }
    const QVariantMap request{{QStringLiteral("device"), m_device},
                              {QStringLiteral("feeder"), feeder},
                              {QStringLiteral("source"), feeder ? m_caps.feederSource : m_caps.glassSource},
                              {QStringLiteral("mode"), mode},
                              {QStringLiteral("lineart"), lineart},
                              {QStringLiteral("dpi"), m_dpi},
                              {QStringLiteral("widthMm"), width},
                              {QStringLiteral("heightMm"), height},
                              {QStringLiteral("straighten"), m_straighten}};
    m_scanning = true;
    m_cancelling = false;
    m_pagesThisScan = 0;
    m_scanningPage = int(m_pages.size()) + 1;
    m_progress = -1;
    Q_EMIT scanningChanged();
    Q_EMIT progressChanged();
    QMetaObject::invokeMethod(m_worker, "scan", Q_ARG(QVariantMap, request));
}

void Scanner::cancel()
{
    if (!m_scanning || !m_worker) {
        return;
    }
    m_cancelling = true;
    Q_EMIT scanningChanged();
    m_worker->cancelNow();
}

QVariantList Scanner::pages() const
{
    QVariantList out;
    for (const Page &p : m_pages) {
        const QImage image = m_images->get(p.id);
        out << QVariantMap{{QStringLiteral("id"), p.id},
                           {QStringLiteral("source"), QStringLiteral("image://scan/%1/%2").arg(p.id).arg(p.rev)},
                           {QStringLiteral("width"), image.width()},
                           {QStringLiteral("height"), image.height()},
                           {QStringLiteral("note"), p.note}};
    }
    return out;
}

void Scanner::setCurrent(int index)
{
    index = m_pages.isEmpty() ? -1 : std::clamp(index, 0, int(m_pages.size()) - 1);
    if (m_current != index) {
        m_current = index;
        Q_EMIT currentChanged();
    }
}

void Scanner::addPage(const QImage &image, int dpi, const QString &note)
{
    Page page{m_nextId++, 1, dpi, note};
    m_images->set(page.id, image);
    m_pages << page;
    Q_EMIT pagesChanged();
    setCurrent(int(m_pages.size()) - 1);
    m_measureTimer.start();
}

void Scanner::remember(const QString &kind, int index)
{
    const Page &page = m_pages.at(index);
    m_history << Undo{kind, index, page, m_images->get(page.id)};
    // Each copy is a whole page; ten steps back is plenty.
    while (m_history.size() > 10) {
        m_history.removeFirst();
    }
}

void Scanner::changed(int index, const QImage &image, const QString &kind)
{
    if (index < 0 || index >= m_pages.size() || image.isNull()) {
        return;
    }
    remember(kind, index);
    Page &page = m_pages[index];
    m_images->set(page.id, image);
    ++page.rev;
    page.pdfBytes = -1;
    Q_EMIT pagesChanged();
    m_measureTimer.start();
}

void Scanner::rotate()
{
    if (m_current < 0) {
        return;
    }
    changed(m_current, m_images->get(m_pages.at(m_current).id).transformed(QTransform().rotate(90)), QStringLiteral("rotate"));
}

void Scanner::crop(const QRectF &fraction)
{
    if (m_current < 0) {
        return;
    }
    const QImage image = m_images->get(m_pages.at(m_current).id);
    const QRect area = QRect(int(std::lround(fraction.x() * image.width())), int(std::lround(fraction.y() * image.height())),
                             int(std::lround(fraction.width() * image.width())), int(std::lround(fraction.height() * image.height())))
                           .intersected(image.rect());
    if (area.width() < 8 || area.height() < 8 || area == image.rect()) {
        return;
    }
    changed(m_current, image.copy(area), QStringLiteral("crop"));
}

void Scanner::removePage()
{
    if (m_current < 0) {
        return;
    }
    const int index = m_current;
    remember(QStringLiteral("remove"), index);
    m_images->remove(m_pages.at(index).id);
    m_pages.removeAt(index);
    Q_EMIT pagesChanged();
    m_current = -1;
    setCurrent(std::min(index, int(m_pages.size()) - 1));
    Q_EMIT currentChanged();
    m_measureTimer.start();
    Q_EMIT said(tr("Page %1 removed").arg(index + 1), QStringLiteral("undo"));
}

void Scanner::undo()
{
    if (m_history.isEmpty()) {
        return;
    }
    const Undo step = m_history.takeLast();
    if (step.kind == QLatin1String("remove")) {
        const int at = std::clamp(step.index, 0, int(m_pages.size()));
        m_pages.insert(at, step.page);
        m_images->set(step.page.id, step.image);
        Q_EMIT pagesChanged();
        m_current = -1;
        setCurrent(at);
    } else {
        for (int i = 0; i < m_pages.size(); ++i) {
            if (m_pages.at(i).id == step.page.id) {
                m_images->set(step.page.id, step.image);
                ++m_pages[i].rev;
                m_pages[i].pdfBytes = -1;
                Q_EMIT pagesChanged();
                setCurrent(i);
            }
        }
    }
    m_measureTimer.start();
}

QString Scanner::pageFile()
{
    if (m_current < 0) {
        return {};
    }
    QDir().mkpath(m_workDir);
    const Page &page = m_pages.at(m_current);
    // Named for the page, which is what the editor's title bar shows.
    const QString path = m_workDir + QStringLiteral("/") + tr("Scanned page %1").arg(m_current + 1) + QStringLiteral(" #%1.png").arg(page.id);
    return m_images->get(page.id).save(path, "PNG") ? path : QString();
}

void Scanner::markedUp(const QString &path)
{
    const int hash = int(path.lastIndexOf(QLatin1Char('#')));
    const int id = hash < 0 ? -1 : path.mid(hash + 1).section(QLatin1Char('.'), 0, 0).toInt();
    QImage image(path);
    for (int i = 0; i < m_pages.size(); ++i) {
        if (m_pages.at(i).id == id && !image.isNull()) {
            // The editor writes ARGB; a scan has no see-through parts.
            const QImage old = m_images->get(id);
            image = image.convertToFormat(old.format() == QImage::Format_Grayscale8 && image.allGray() ? QImage::Format_Grayscale8 : QImage::Format_RGB888);
            changed(i, image, QStringLiteral("markup"));
            setCurrent(i);
        }
    }
}

void Scanner::measure()
{
    // The PDF's size, page by page: each one written to memory on a spare
    // thread and added up, so the figure under the pages is real.
    QPointer<Scanner> self(this);
    qint64 total = 0;
    bool waiting = false;
    for (const Page &page : std::as_const(m_pages)) {
        if (page.pdfBytes >= 0) {
            total += page.pdfBytes;
            continue;
        }
        waiting = true;
        const int id = page.id;
        const int rev = page.rev;
        const QImage image = m_images->get(id);
        const int dpi = page.dpi;
        QThreadPool::globalInstance()->start([self, id, rev, image, dpi] {
            QByteArray bytes;
            QBuffer buffer(&bytes);
            buffer.open(QIODevice::WriteOnly);
            writePdfPages(&buffer, {OutPage{image, dpi, isBlackAndWhite(image)}}, QString());
            const qint64 size = bytes.size();
            QMetaObject::invokeMethod(QCoreApplication::instance(), [self, id, rev, size] {
                if (!self) {
                    return;
                }
                for (Page &p : self->m_pages) {
                    if (p.id == id && p.rev == rev) {
                        p.pdfBytes = size;
                    }
                }
                self->m_measureTimer.start();
            });
        });
    }
    if (!waiting && total != m_pdfSize) {
        m_pdfSize = total;
        Q_EMIT pdfSizeChanged();
    }
    if (m_pages.isEmpty() && m_pdfSize != 0) {
        m_pdfSize = 0;
        Q_EMIT pdfSizeChanged();
    }
}

QString Scanner::nextName(const QString &folder, const QString &suffix) const
{
    const QString stem = folder + QStringLiteral("/") + tr("Scan") + QLatin1Char(' ') + QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HHmmss"));
    QString path = stem + suffix;
    for (int n = 2; QFileInfo::exists(path); ++n) {
        path = stem + QStringLiteral(" (%1)").arg(n) + suffix;
    }
    return path;
}

void Scanner::savePdf()
{
    writePdf(false);
}

void Scanner::writePdf(bool sign)
{
    if (m_pages.isEmpty() || m_busy) {
        return;
    }
    setBusy(true);
    QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (documents.isEmpty()) {
        documents = QDir::homePath() + QStringLiteral("/Documents");
    }
    const QString folder = documents + QStringLiteral("/") + tr("Scans");
    QDir().mkpath(folder);
    const QString path = nextName(folder, QStringLiteral(".pdf"));
    QList<OutPage> out;
    for (const Page &p : std::as_const(m_pages)) {
        const QImage image = m_images->get(p.id);
        out << OutPage{image, p.dpi, isBlackAndWhite(image)};
    }
    QPointer<Scanner> self(this);
    QThreadPool::globalInstance()->start([self, path, out, sign] {
        QFile file(path);
        const bool ok = file.open(QIODevice::WriteOnly) && writePdfPages(&file, out, QFileInfo(path).completeBaseName());
        if (!ok) {
            file.remove();
        }
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, path, ok, sign] {
            if (!self) {
                return;
            }
            self->setBusy(false);
            if (!ok) {
                Q_EMIT self->said(tr("Couldn't save the PDF to %1").arg(placeName(QFileInfo(path).absolutePath())), QString());
                return;
            }
            if (sign) {
                const QString pdf = QStandardPaths::findExecutable(QStringLiteral("wildos-pdf"));
                QProcess::startDetached(pdf, {QStringLiteral("--sign"), path});
                Q_EMIT self->said(tr("Saved to %1 and opened in PDF").arg(placeName(QFileInfo(path).absolutePath())), path);
                return;
            }
            Q_EMIT self->said(tr("Saved to %1").arg(placeName(QFileInfo(path).absolutePath())), path);
        });
    });
}

void Scanner::openToSign()
{
    if (!canSign()) {
        return;
    }
    writePdf(true);
}

void Scanner::savePictures()
{
    if (m_pages.isEmpty() || m_busy) {
        return;
    }
    setBusy(true);
    QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (pictures.isEmpty()) {
        pictures = QDir::homePath() + QStringLiteral("/Pictures");
    }
    const QString folder = pictures + QStringLiteral("/") + tr("Scans");
    QDir().mkpath(folder);
    const QString stem = nextName(folder, QString());
    QList<OutPage> out;
    for (const Page &p : std::as_const(m_pages)) {
        const QImage image = m_images->get(p.id);
        out << OutPage{image, p.dpi, isBlackAndWhite(image)};
    }
    QPointer<Scanner> self(this);
    QThreadPool::globalInstance()->start([self, stem, out, folder] {
        bool ok = true;
        QString first;
        for (int i = 0; i < out.size(); ++i) {
            const OutPage &page = out.at(i);
            // Black and white keeps its sharp edges as PNG; anything with
            // shades is a JPEG a tenth the size.
            const QString path = stem + (out.size() > 1 ? QStringLiteral(" ") + tr("page %1").arg(i + 1) : QString())
                + (page.bw ? QStringLiteral(".png") : QStringLiteral(".jpg"));
            QImage image = page.bw ? page.image.convertToFormat(QImage::Format_Mono, Qt::ThresholdDither) : page.image;
            image.setDotsPerMeterX(int(std::lround(page.dpi / 0.0254)));
            image.setDotsPerMeterY(int(std::lround(page.dpi / 0.0254)));
            ok = ok && (page.bw ? image.save(path, "PNG") : image.save(path, "JPEG", 92));
            if (first.isEmpty()) {
                first = path;
            }
        }
        const int count = int(out.size());
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, ok, first, folder, count] {
            if (!self) {
                return;
            }
            self->setBusy(false);
            Q_EMIT self->said(ok ? (count == 1 ? tr("Saved to %1").arg(placeName(folder)) : tr("%1 pictures saved to %2").arg(count).arg(placeName(folder)))
                                 : tr("Couldn't save the pictures to %1").arg(placeName(folder)),
                              ok ? first : QString());
        });
    });
}

void Scanner::forget()
{
    for (const Page &p : std::as_const(m_pages)) {
        m_images->remove(p.id);
    }
    m_pages.clear();
    m_history.clear();
    m_current = -1;
    Q_EMIT pagesChanged();
    Q_EMIT currentChanged();
    clearProblem();
    m_measureTimer.start();
}
