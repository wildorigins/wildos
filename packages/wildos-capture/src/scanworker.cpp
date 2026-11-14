#include "scanworker.h"
#include "scanprocess.h"

#include <QHash>
#include <QRegularExpression>
#include <QThread>

#include <sane/sane.h>
#include <sane/saneopts.h>

#include <cmath>
#include <cstring>

namespace
{
SANE_Handle handleOf(const std::atomic<void *> &handle)
{
    return static_cast<SANE_Handle>(handle.load());
}

QString outcomeOf(SANE_Status status)
{
    switch (status) {
    case SANE_STATUS_GOOD:
    case SANE_STATUS_EOF:
        return QStringLiteral("done");
    case SANE_STATUS_CANCELLED:
        return QStringLiteral("cancelled");
    case SANE_STATUS_DEVICE_BUSY:
        return QStringLiteral("busy");
    case SANE_STATUS_NO_DOCS:
        return QStringLiteral("empty");
    case SANE_STATUS_JAMMED:
        return QStringLiteral("jammed");
    case SANE_STATUS_COVER_OPEN:
        return QStringLiteral("cover");
    case SANE_STATUS_IO_ERROR:
    case SANE_STATUS_INVAL:
    case SANE_STATUS_ACCESS_DENIED:
        return QStringLiteral("offline");
    default:
        return QStringLiteral("failed");
    }
}

// The scan modes SANE backends name differently: SANE's own Color, Gray
// and Lineart, and what airscan, escl and others call them.
bool isColour(const QString &mode)
{
    return mode.compare(QLatin1String(SANE_VALUE_SCAN_MODE_COLOR), Qt::CaseInsensitive) == 0 || mode.contains(QLatin1String("color"), Qt::CaseInsensitive)
        || mode.contains(QLatin1String("colour"), Qt::CaseInsensitive) || mode.contains(QLatin1String("rgb"), Qt::CaseInsensitive);
}
bool isGrey(const QString &mode)
{
    return mode.contains(QLatin1String("gray"), Qt::CaseInsensitive) || mode.contains(QLatin1String("grey"), Qt::CaseInsensitive);
}
bool isLineart(const QString &mode)
{
    return mode.contains(QLatin1String("lineart"), Qt::CaseInsensitive) || mode.contains(QLatin1String("binary"), Qt::CaseInsensitive)
        || mode.contains(QLatin1String("black"), Qt::CaseInsensitive);
}
bool isFeeder(const QString &source)
{
    return source.contains(QLatin1String("adf"), Qt::CaseInsensitive) || source.contains(QLatin1String("feeder"), Qt::CaseInsensitive);
}
// A duplex feeder or the back of the page is a choice for later; the
// first plain feeder is the one offered.
bool isPlainFeeder(const QString &source)
{
    return isFeeder(source) && !source.contains(QLatin1String("duplex"), Qt::CaseInsensitive) && !source.contains(QLatin1String("back"), Qt::CaseInsensitive)
        && !source.contains(QLatin1String("rear"), Qt::CaseInsensitive);
}
bool isGlass(const QString &source)
{
    return source.contains(QLatin1String("flatbed"), Qt::CaseInsensitive) || source.contains(QLatin1String("platen"), Qt::CaseInsensitive)
        || source.contains(QLatin1String("glass"), Qt::CaseInsensitive) || source.contains(QLatin1String("normal"), Qt::CaseInsensitive);
}

double numberOf(const SANE_Option_Descriptor *d, SANE_Word word)
{
    return d->type == SANE_TYPE_FIXED ? SANE_UNFIX(word) : double(word);
}
}

ScanWorker::ScanWorker() = default;

ScanWorker::~ScanWorker()
{
    stop();
}

void ScanWorker::start()
{
    SANE_Int version = 0;
    m_initialised = sane_init(&version, nullptr) == SANE_STATUS_GOOD;
}

void ScanWorker::stop()
{
    closeDevice();
    if (m_initialised) {
        sane_exit();
        m_initialised = false;
    }
}

void ScanWorker::listDevices()
{
    QVariantList network;
    QVariantList local;
    if (m_initialised) {
        // A device that is open is not always listed again by its backend,
        // so it is let go first and opened again when it is needed.
        closeDevice();
        const SANE_Device **list = nullptr;
        if (sane_get_devices(&list, SANE_FALSE) == SANE_STATUS_GOOD && list) {
            QHash<QString, bool> seen;
            for (int i = 0; list[i]; ++i) {
                const SANE_Device *d = list[i];
                const QString name = QString::fromUtf8(d->name);
                const QString type = QString::fromUtf8(d->type ? d->type : "");
                // SANE's v4l and gphoto backends list webcams and cameras,
                // which are no use for paper.
                if (type.contains(QLatin1String("camera"), Qt::CaseInsensitive) || type.contains(QLatin1String("video"), Qt::CaseInsensitive)
                    || name.startsWith(QLatin1String("v4l:")) || name.startsWith(QLatin1String("gphoto2:"))) {
                    continue;
                }
                const QString vendor = QString::fromUtf8(d->vendor ? d->vendor : "").trimmed();
                const QString model = QString::fromUtf8(d->model ? d->model : "").trimmed();
                const bool driverless = name.startsWith(QLatin1String("airscan:")) || name.startsWith(QLatin1String("escl:"));
                // airscan names a network scanner as it announces itself
                // ("Office printer"), the name Settings shows too.
                QString title = driverless || vendor.isEmpty() || model.startsWith(vendor, Qt::CaseInsensitive) ? model : vendor + QLatin1Char(' ') + model;
                if (title.isEmpty()) {
                    title = name;
                }
                const QString key = title.toLower();
                if (driverless && seen.contains(key)) {
                    continue;
                }
                const QVariantMap entry{{QStringLiteral("id"), name}, {QStringLiteral("name"), title}, {QStringLiteral("network"), driverless}};
                if (name.startsWith(QLatin1String("airscan:"))) {
                    seen.insert(key, true);
                    network.prepend(entry);
                } else if (driverless) {
                    network.append(entry);
                } else {
                    local.append(entry);
                }
            }
            // escl finds the same scanners airscan does; airscan's copy is
            // kept, being listed first.
            QVariantList kept;
            QHash<QString, bool> names;
            for (const QVariant &v : network) {
                const QString key = v.toMap().value(QStringLiteral("name")).toString().toLower();
                if (!names.contains(key)) {
                    names.insert(key, true);
                    kept << v;
                }
            }
            // A USB scanner that ipp-usb also offers over eSCL is listed
            // once, as the driverless one. Two of the same model on USB are
            // both kept, numbered.
            QHash<QString, int> count;
            for (const QVariant &v : local) {
                QVariantMap entry = v.toMap();
                const QString name = entry.value(QStringLiteral("name")).toString();
                if (names.contains(name.toLower())) {
                    continue;
                }
                const int n = ++count[name.toLower()];
                if (n > 1) {
                    entry.insert(QStringLiteral("name"), QStringLiteral("%1 (%2)").arg(name).arg(n));
                }
                kept << entry;
            }
            Q_EMIT devicesFound(kept);
            return;
        }
    }
    Q_EMIT devicesFound({});
}

void ScanWorker::closeDevice()
{
    if (SANE_Handle h = handleOf(m_handle)) {
        m_handle = nullptr;
        sane_close(h);
    }
    m_name.clear();
}

bool ScanWorker::ensureOpen(const QString &name, QString *outcome)
{
    if (m_handle.load() && m_name == name) {
        return true;
    }
    closeDevice();
    if (!m_initialised) {
        *outcome = QStringLiteral("failed");
        return false;
    }
    SANE_Handle h = nullptr;
    const SANE_Status status = sane_open(name.toUtf8().constData(), &h);
    if (status != SANE_STATUS_GOOD || !h) {
        *outcome = outcomeOf(status == SANE_STATUS_GOOD ? SANE_STATUS_IO_ERROR : status);
        return false;
    }
    m_handle = h;
    m_name = name;
    return true;
}

void ScanWorker::open(const QString &name)
{
    QString outcome;
    if (!ensureOpen(name, &outcome)) {
        Q_EMIT openFailed(name, outcome);
        return;
    }
    // The glass's limits: a feeder can be narrower, and a few backends
    // only show the glass's area while it is the source.
    ScannerCaps caps = readCaps();
    if (!caps.glassSource.isEmpty()) {
        setString(SANE_NAME_SCAN_SOURCE, caps.glassSource);
        const ScannerCaps glass = readCaps();
        caps.maxWidthMm = glass.maxWidthMm;
        caps.maxHeightMm = glass.maxHeightMm;
    }
    Q_EMIT opened(name, caps);
}

int ScanWorker::optionIndex(const char *name) const
{
    SANE_Handle h = handleOf(m_handle);
    if (!h) {
        return -1;
    }
    for (SANE_Int i = 1;; ++i) {
        const SANE_Option_Descriptor *d = sane_get_option_descriptor(h, i);
        if (!d) {
            return -1;
        }
        if (d->name && std::strcmp(d->name, name) == 0) {
            return SANE_OPTION_IS_ACTIVE(d->cap) ? i : -1;
        }
    }
}

ScannerCaps ScanWorker::readCaps()
{
    ScannerCaps caps;
    SANE_Handle h = handleOf(m_handle);
    if (!h) {
        return caps;
    }
    auto strings = [h](int index) {
        QStringList out;
        const SANE_Option_Descriptor *d = index > 0 ? sane_get_option_descriptor(h, index) : nullptr;
        if (d && d->constraint_type == SANE_CONSTRAINT_STRING_LIST) {
            for (int i = 0; d->constraint.string_list[i]; ++i) {
                out << QString::fromUtf8(d->constraint.string_list[i]);
            }
        }
        return out;
    };
    for (const QString &s : strings(optionIndex(SANE_NAME_SCAN_SOURCE))) {
        if (caps.feederSource.isEmpty() && isPlainFeeder(s)) {
            caps.feederSource = s;
        } else if (caps.glassSource.isEmpty() && isGlass(s)) {
            caps.glassSource = s;
        }
    }
    for (const QString &m : strings(optionIndex(SANE_NAME_SCAN_MODE))) {
        if (caps.colourMode.isEmpty() && isColour(m)) {
            caps.colourMode = m;
        } else if (caps.greyMode.isEmpty() && isGrey(m)) {
            caps.greyMode = m;
        } else if (caps.lineartMode.isEmpty() && isLineart(m) && !m.contains(QLatin1String("halftone"), Qt::CaseInsensitive)) {
            caps.lineartMode = m;
        }
    }
    const int res = optionIndex(SANE_NAME_SCAN_RESOLUTION);
    if (const SANE_Option_Descriptor *d = res > 0 ? sane_get_option_descriptor(h, res) : nullptr) {
        if (d->constraint_type == SANE_CONSTRAINT_WORD_LIST) {
            for (int i = 1; i <= d->constraint.word_list[0]; ++i) {
                caps.resolutions << int(std::lround(numberOf(d, d->constraint.word_list[i])));
            }
        } else if (d->constraint_type == SANE_CONSTRAINT_RANGE) {
            const double lo = numberOf(d, d->constraint.range->min);
            const double hi = numberOf(d, d->constraint.range->max);
            for (int v : {75, 100, 150, 200, 300, 400, 600, 1200}) {
                if (v >= lo && v <= hi) {
                    caps.resolutions << v;
                }
            }
        }
    }
    auto maxOf = [h, this](const char *name) {
        const int index = optionIndex(name);
        const SANE_Option_Descriptor *d = index > 0 ? sane_get_option_descriptor(h, index) : nullptr;
        if (!d || d->unit != SANE_UNIT_MM || d->constraint_type != SANE_CONSTRAINT_RANGE) {
            return 0.0;
        }
        return numberOf(d, d->constraint.range->max);
    };
    caps.maxWidthMm = maxOf(SANE_NAME_SCAN_BR_X);
    caps.maxHeightMm = maxOf(SANE_NAME_SCAN_BR_Y);
    return caps;
}

bool ScanWorker::setString(const char *name, const QString &value)
{
    SANE_Handle h = handleOf(m_handle);
    const int index = optionIndex(name);
    const SANE_Option_Descriptor *d = index > 0 ? sane_get_option_descriptor(h, index) : nullptr;
    if (!d || d->type != SANE_TYPE_STRING || !SANE_OPTION_IS_SETTABLE(d->cap)) {
        return false;
    }
    QByteArray buffer = value.toUtf8();
    buffer.resize(std::max<qsizetype>(d->size, buffer.size() + 1), '\0');
    SANE_Int info = 0;
    return sane_control_option(h, index, SANE_ACTION_SET_VALUE, buffer.data(), &info) == SANE_STATUS_GOOD;
}

bool ScanWorker::setNumber(const char *name, double value)
{
    SANE_Handle h = handleOf(m_handle);
    const int index = optionIndex(name);
    const SANE_Option_Descriptor *d = index > 0 ? sane_get_option_descriptor(h, index) : nullptr;
    if (!d || (d->type != SANE_TYPE_INT && d->type != SANE_TYPE_FIXED) || !SANE_OPTION_IS_SETTABLE(d->cap)) {
        return false;
    }
    if (d->constraint_type == SANE_CONSTRAINT_RANGE) {
        value = std::clamp(value, numberOf(d, d->constraint.range->min), numberOf(d, d->constraint.range->max));
    }
    SANE_Word word = d->type == SANE_TYPE_FIXED ? SANE_FIX(value) : SANE_Word(std::lround(value));
    SANE_Int info = 0;
    return sane_control_option(h, index, SANE_ACTION_SET_VALUE, &word, &info) == SANE_STATUS_GOOD;
}

void ScanWorker::cancelNow()
{
    m_cancel = true;
    if (SANE_Handle h = handleOf(m_handle)) {
        sane_cancel(h);
    }
}

void ScanWorker::scan(const QVariantMap &settings)
{
    m_cancel = false;
    QString outcome;
    const QString name = settings.value(QStringLiteral("device")).toString();
    const bool fresh = !m_handle.load() || m_name != name;
    if (!ensureOpen(name, &outcome)) {
        Q_EMIT finished(outcome, 0);
        return;
    }
    // Opened again after it went away: what it can do may have been
    // unknown since, so the window hears it again.
    if (fresh) {
        Q_EMIT opened(name, readCaps());
    }
    const bool feeder = settings.value(QStringLiteral("feeder")).toBool();
    // Source first: some backends change the other options' limits with it.
    if (const QString source = settings.value(QStringLiteral("source")).toString(); !source.isEmpty()) {
        setString(SANE_NAME_SCAN_SOURCE, source);
    }
    if (const QString mode = settings.value(QStringLiteral("mode")).toString(); !mode.isEmpty()) {
        setString(SANE_NAME_SCAN_MODE, mode);
    }
    setNumber(SANE_NAME_BIT_DEPTH, 8);
    const int dpi = settings.value(QStringLiteral("dpi")).toInt();
    setNumber(SANE_NAME_SCAN_RESOLUTION, dpi);
    setNumber(SANE_NAME_SCAN_TL_X, 0);
    setNumber(SANE_NAME_SCAN_TL_Y, 0);
    setNumber(SANE_NAME_SCAN_BR_X, settings.value(QStringLiteral("widthMm")).toDouble());
    setNumber(SANE_NAME_SCAN_BR_Y, settings.value(QStringLiteral("heightMm")).toDouble());
    const bool straighten = settings.value(QStringLiteral("straighten")).toBool();
    const bool lineart = settings.value(QStringLiteral("lineart")).toBool();

    int pages = 0;
    for (;;) {
        Q_EMIT pageStarted(pages + 1);
        Q_EMIT progress(-1);
        QImage image = readPage(&outcome);
        if (image.isNull()) {
            // The feeder running out after the first page is the end of
            // the pile, not a problem.
            if (outcome == QLatin1String("empty") && pages > 0) {
                outcome = QStringLiteral("done");
            }
            break;
        }
        ++pages;
        QString note;
        if (straighten) {
            image = ScanProcess::straightenAndCrop(image, &note);
        }
        if (lineart) {
            image = ScanProcess::blackAndWhite(image);
        }
        Q_EMIT pageScanned(image, dpi, note);
        if (!feeder || m_cancel) {
            outcome = m_cancel ? QStringLiteral("cancelled") : QStringLiteral("done");
            break;
        }
    }
    if (SANE_Handle h = handleOf(m_handle)) {
        sane_cancel(h);
    }
    // After the scanner went away, a fresh handle next time.
    if (outcome == QLatin1String("offline") || outcome == QLatin1String("failed")) {
        closeDevice();
    }
    Q_EMIT finished(outcome, pages);
}

// One page, all its frames, as an image: grey or colour, 8 or 16 bits,
// or 1-bit line art.
QImage ScanWorker::readPage(QString *outcome)
{
    SANE_Handle h = handleOf(m_handle);
    if (!h || m_cancel) {
        *outcome = QStringLiteral("cancelled");
        return {};
    }
    QImage result;
    QByteArray channels[3];
    SANE_Parameters p{};
    for (;;) {
        SANE_Status started = sane_start(h);
        // Straight after a cancelled scan, airscan can still be closing the
        // old job and turns the next one away as cancelled too; it takes a
        // few seconds at most.
        for (int i = 0; started == SANE_STATUS_CANCELLED && !m_cancel && i < 20; ++i) {
            QThread::msleep(500);
            started = sane_start(h);
        }
        if (started != SANE_STATUS_GOOD) {
            *outcome = m_cancel ? QStringLiteral("cancelled") : outcomeOf(started);
            return {};
        }
        if (sane_get_parameters(h, &p) != SANE_STATUS_GOOD || p.bytes_per_line <= 0) {
            *outcome = QStringLiteral("failed");
            return {};
        }
        QByteArray data;
        if (p.lines > 0) {
            data.reserve(qsizetype(p.bytes_per_line) * p.lines);
        }
        const qint64 expected = p.lines > 0 ? qint64(p.bytes_per_line) * p.lines : 0;
        QByteArray chunk(256 * 1024, Qt::Uninitialized);
        qint64 lastReported = 0;
        for (;;) {
            SANE_Int got = 0;
            const SANE_Status status = sane_read(h, reinterpret_cast<SANE_Byte *>(chunk.data()), SANE_Int(chunk.size()), &got);
            if (got > 0) {
                data.append(chunk.constData(), got);
            }
            if (status == SANE_STATUS_EOF) {
                break;
            }
            if (status != SANE_STATUS_GOOD || m_cancel) {
                *outcome = m_cancel ? QStringLiteral("cancelled") : outcomeOf(status);
                return {};
            }
            if (expected > 0 && data.size() - lastReported > expected / 100) {
                lastReported = data.size();
                Q_EMIT progress(qreal(data.size()) / expected);
            }
        }
        const int lines = int(data.size() / p.bytes_per_line);
        if (lines <= 0) {
            *outcome = QStringLiteral("failed");
            return {};
        }
        const int w = p.pixels_per_line;
        if (p.format == SANE_FRAME_GRAY || p.format == SANE_FRAME_RGB) {
            const bool rgb = p.format == SANE_FRAME_RGB;
            result = QImage(w, lines, rgb ? QImage::Format_RGB888 : QImage::Format_Grayscale8);
            for (int y = 0; y < lines; ++y) {
                const uchar *in = reinterpret_cast<const uchar *>(data.constData()) + qsizetype(y) * p.bytes_per_line;
                uchar *out = result.scanLine(y);
                if (p.depth == 8) {
                    std::memcpy(out, in, size_t(w) * (rgb ? 3 : 1));
                } else if (p.depth == 16) {
                    // Host byte order: the high byte of each sample.
                    const quint16 *samples = reinterpret_cast<const quint16 *>(in);
                    for (int i = 0; i < w * (rgb ? 3 : 1); ++i) {
                        out[i] = uchar(samples[i] >> 8);
                    }
                } else if (p.depth == 1) {
                    // In SANE's line art a set bit is black.
                    const int samples = w * (rgb ? 3 : 1);
                    for (int i = 0; i < samples; ++i) {
                        out[i] = (in[i / 8] & (0x80 >> (i % 8))) ? 0 : 255;
                    }
                }
            }
            break;
        }
        // Three-pass scanners send red, green and blue one after another.
        const int c = p.format == SANE_FRAME_RED ? 0 : p.format == SANE_FRAME_GREEN ? 1 : 2;
        channels[c] = data;
        if (p.last_frame) {
            result = QImage(w, lines, QImage::Format_RGB888);
            for (int y = 0; y < lines; ++y) {
                uchar *out = result.scanLine(y);
                for (int x = 0; x < w; ++x) {
                    for (int k = 0; k < 3; ++k) {
                        const qsizetype at = qsizetype(y) * p.bytes_per_line + (p.depth == 16 ? x * 2 + 1 : x);
                        out[x * 3 + k] = at < channels[k].size() ? uchar(channels[k].at(at)) : 255;
                    }
                }
            }
            break;
        }
    }
    *outcome = QStringLiteral("done");
    return result;
}
