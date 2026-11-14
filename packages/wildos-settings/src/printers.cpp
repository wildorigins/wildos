#include "printers.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <KConfigGroup>
#include <KSharedConfig>

#include <cups/adminutil.h>
#include <cups/cups.h>
#include <cups/pwg.h>

#include <unistd.h>

namespace
{
const QString testPage = QStringLiteral("/usr/share/cups/data/testprint");

QString text(ipp_attribute_t *attr)
{
    return attr ? QString::fromUtf8(ippGetString(attr, 0, nullptr)) : QString();
}

QStringList texts(ipp_attribute_t *attr)
{
    QStringList list;
    for (int i = 0; attr && i < ippGetCount(attr); ++i) {
        list << QString::fromUtf8(ippGetString(attr, i, nullptr));
    }
    return list;
}

// printer-state-reasons as a person would say them, most serious first.
// "-report" and "-warning" are CUPS's softer grades of the same reason.
QString problem(const QStringList &reasons)
{
    static const QList<std::pair<const char *, const char *>> words = {
        {"offline", "Offline"},
        {"media-jam", "Paper jam"},
        {"door-open", "A cover is open"},
        {"cover-open", "A cover is open"},
        {"media-empty", "Out of paper"},
        {"media-needed", "Out of paper"},
        {"toner-empty", "Out of toner"},
        {"marker-supply-empty", "Out of ink or toner"},
        {"input-tray-missing", "A paper tray is missing"},
        {"connecting-to-device", "Connecting"},
        {"toner-low", "Toner low"},
        {"marker-supply-low", "Ink or toner low"},
        {"media-low", "Paper low"},
    };
    for (const auto &[key, word] : words) {
        for (QString reason : reasons) {
            reason.remove(QRegularExpression(QStringLiteral("-(report|warning|error)$")));
            if (reason == QLatin1String(key)) {
                return QString::fromLatin1(word);
            }
        }
    }
    return {};
}

// One ink or toner supply, from marker-* (what CUPS and AirPrint printers
// report) or a printer-supply entry (what IPP Everywhere requires).
struct Marker {
    QString name;
    QString color;
    QString type;
    int level = -1;
};

bool consumable(const Marker &m)
{
    const QString type = m.type.toLower();
    return m.level >= 0 && !type.contains(QLatin1String("waste")) && (type.isEmpty() || type.contains(QLatin1String("ink")) || type.contains(QLatin1String("toner")));
}

bool isBlack(const Marker &m)
{
    return m.color.compare(QLatin1String("#000000"), Qt::CaseInsensitive) == 0
        || (m.color.isEmpty() && m.name.contains(QLatin1String("black"), Qt::CaseInsensitive));
}

// "Black ink 62%, colour 40%": the colours as the one that runs out first,
// since that is the one to buy.
QString inkSummary(const QList<Marker> &markers)
{
    int black = -1;
    int colour = -1;
    bool toner = false;
    bool ink = false;
    for (const Marker &m : markers) {
        if (!consumable(m)) {
            continue;
        }
        toner = toner || m.type.contains(QLatin1String("toner"), Qt::CaseInsensitive);
        ink = ink || m.type.contains(QLatin1String("ink"), Qt::CaseInsensitive);
        int &slot = isBlack(m) ? black : colour;
        slot = slot < 0 ? m.level : std::min(slot, m.level);
    }
    const QString substance = toner && !ink ? QStringLiteral("toner") : QStringLiteral("ink");
    QStringList parts;
    if (black >= 0) {
        parts << QStringLiteral("Black %1 %2%").arg(substance).arg(black);
    }
    if (colour >= 0) {
        parts << (black >= 0 ? QStringLiteral("colour %1%").arg(colour) : QStringLiteral("Colour %1 %2%").arg(substance).arg(colour));
    }
    return parts.join(QStringLiteral(", "));
}

// Every supply by the name the printer gives it: "Black Cartridge 62%,
// Cyan Cartridge 40%".
QString inkDetail(const QList<Marker> &markers)
{
    QStringList parts;
    for (const Marker &m : markers) {
        if (consumable(m)) {
            parts << QStringLiteral("%1 %2%").arg(m.name.isEmpty() ? QStringLiteral("Ink") : m.name).arg(m.level);
        }
    }
    return parts.join(QStringLiteral(", "));
}

// marker-* lists kept while one printer's attributes are read.
struct MarkerLists {
    QStringList names;
    QStringList colors;
    QStringList types;
    QList<int> levels;
    QList<QByteArray> supplies;
    QStringList supplyNames;

    bool take(const QByteArray &key, ipp_attribute_t *attr)
    {
        if (key == "marker-names") {
            names = texts(attr);
        } else if (key == "marker-colors") {
            colors = texts(attr);
        } else if (key == "marker-types") {
            types = texts(attr);
        } else if (key == "marker-levels") {
            levels.clear();
            for (int i = 0; i < ippGetCount(attr); ++i) {
                levels << ippGetInteger(attr, i);
            }
        } else if (key == "printer-supply") {
            supplies.clear();
            for (int i = 0; i < ippGetCount(attr); ++i) {
                int length = 0;
                const auto *data = static_cast<const char *>(ippGetOctetString(attr, i, &length));
                supplies << QByteArray(data, data ? length : 0);
            }
        } else if (key == "printer-supply-description") {
            supplyNames = texts(attr);
        } else {
            return false;
        }
        return true;
    }

    QList<Marker> markers() const
    {
        QList<Marker> out;
        for (int i = 0; i < levels.size(); ++i) {
            out << Marker{names.value(i), colors.value(i), types.value(i), levels.at(i)};
        }
        if (!out.isEmpty()) {
            return out;
        }
        // "type=inkCartridge;maxcapacity=100;level=62;colorantname=black;"
        for (int i = 0; i < supplies.size(); ++i) {
            QHash<QString, QString> fields;
            for (const QByteArray &pair : supplies.at(i).split(';')) {
                const int eq = pair.indexOf('=');
                if (eq > 0) {
                    fields.insert(QString::fromLatin1(pair.left(eq)), QString::fromUtf8(pair.mid(eq + 1)));
                }
            }
            const int capacity = fields.value(QStringLiteral("maxcapacity")).toInt();
            const int level = fields.value(QStringLiteral("level"), QStringLiteral("-1")).toInt();
            const QString colorant = fields.value(QStringLiteral("colorantname"));
            out << Marker{supplyNames.value(i, colorant), colorant.compare(QLatin1String("black"), Qt::CaseInsensitive) == 0 ? QStringLiteral("#000000") : QString(),
                          fields.value(QStringLiteral("type")), capacity > 0 && level >= 0 ? level * 100 / capacity : -1};
        }
        return out;
    }
};

const char *const markerAttributes[] = {"marker-names", "marker-colors", "marker-types", "marker-levels", "printer-supply", "printer-supply-description"};

// A driverless printer's own supply levels, for a queue CUPS has not
// printed through yet (the backend copies them into the queue only while
// it prints). Asked at most once a minute per printer, and never twice at
// once, so the page's four-second refresh does not wait on a printer that
// is off.
QList<Marker> askPrinter(const QString &uri)
{
    struct Entry {
        qint64 at = 0;
        QList<Marker> markers;
    };
    static QMutex mutex;
    static QHash<QString, Entry> cache;
    static QSet<QString> asking;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    {
        QMutexLocker lock(&mutex);
        const Entry entry = cache.value(uri);
        if (asking.contains(uri) || (entry.at > 0 && now - entry.at < 60)) {
            return entry.markers;
        }
        asking.insert(uri);
    }

    QList<Marker> markers;
    const QByteArray raw = uri.toUtf8();
    if (cups_dest_t *dest = cupsGetDestWithURI(nullptr, raw.constData())) {
        char resource[1024];
        int cancel = 0;
        // CUPS_DEST_FLAGS_DEVICE talks to the printer, resolving a dnssd://
        // address first, rather than to the queue.
        if (http_t *http = cupsConnectDest(dest, CUPS_DEST_FLAGS_DEVICE, 2000, &cancel, resource, sizeof resource, nullptr, nullptr)) {
            httpSetTimeout(http, 3.0, nullptr, nullptr);
            const char *resolved = cupsGetOption("resolved_device_uri", dest->num_options, dest->options);
            ipp_t *request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);
            ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", nullptr, resolved ? resolved : raw.constData());
            ippAddStrings(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD, "requested-attributes", int(std::size(markerAttributes)), nullptr, markerAttributes);
            if (ipp_t *response = cupsDoRequest(http, request, resource)) {
                MarkerLists lists;
                for (ipp_attribute_t *attr = ippFirstAttribute(response); attr; attr = ippNextAttribute(response)) {
                    if (const char *name = ippGetName(attr)) {
                        lists.take(QByteArray(name), attr);
                    }
                }
                markers = lists.markers();
                ippDelete(response);
            }
            httpClose(http);
        }
        cupsFreeDests(1, dest);
    }

    // A printer that is off keeps the levels it last reported.
    QMutexLocker lock(&mutex);
    asking.remove(uri);
    if (markers.isEmpty()) {
        markers = cache.value(uri).markers;
    }
    cache.insert(uri, Entry{now, markers});
    return markers;
}

// Printers that answer IPP themselves (IPP Everywhere, AirPrint, ipp-usb).
bool speaksIpp(const QString &uri)
{
    return uri.startsWith(QLatin1String("ipp://")) || uri.startsWith(QLatin1String("ipps://"))
        || (uri.startsWith(QLatin1String("dnssd://")) && uri.contains(QLatin1String("._ipp")));
}

bool cupsRunning()
{
    http_t *http = httpConnect2(cupsServer(), ippPort(), nullptr, AF_UNSPEC, cupsEncryption(), 1, 3000, nullptr);
    if (!http) {
        return false;
    }
    httpClose(http);
    return true;
}

// The queues on this computer, straight from cupsd, so printers CUPS only
// sees on the network are not mistaken for added ones.
QVariantList readQueues()
{
    QVariantList list;
    QString userDefault;
    if (cups_dest_t *dest = cupsGetNamedDest(CUPS_HTTP_DEFAULT, nullptr, nullptr)) {
        userDefault = QString::fromUtf8(dest->name);
        cupsFreeDests(1, dest);
    }

    ipp_t *request = ippNewRequest(IPP_OP_CUPS_GET_PRINTERS);
    static const char *const wanted[] = {"printer-name", "printer-info", "printer-location", "printer-make-and-model", "printer-state",
                                         "printer-state-reasons", "printer-is-accepting-jobs", "device-uri", "queued-job-count",
                                         "printer-state-message", "marker-names", "marker-colors", "marker-types", "marker-levels"};
    ippAddStrings(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD, "requested-attributes", int(std::size(wanted)), nullptr, wanted);
    ipp_t *response = cupsDoRequest(CUPS_HTTP_DEFAULT, request, "/");
    if (!response) {
        return list;
    }

    QVariantMap current;
    MarkerLists markers;
    auto finish = [&] {
        const QString name = current.value(QStringLiteral("name")).toString();
        if (!name.isEmpty()) {
            QList<Marker> supplies = markers.markers();
            const QString uri = current.value(QStringLiteral("uri")).toString();
            if (supplies.isEmpty() && speaksIpp(uri)) {
                supplies = askPrinter(uri);
            }
            current.insert(QStringLiteral("ink"), inkSummary(supplies));
            current.insert(QStringLiteral("inkDetail"), inkDetail(supplies));
            current.insert(QStringLiteral("scans"), false);
            if (current.value(QStringLiteral("info")).toString().isEmpty()) {
                current.insert(QStringLiteral("info"), name);
            }
            const int state = current.value(QStringLiteral("stateCode")).toInt();
            QString trouble = problem(current.value(QStringLiteral("reasons")).toStringList());
            // The usb and ipp backends wait for an unplugged or switched off
            // printer saying so only in the message, with no reason set.
            static const QStringList waiting = {QStringLiteral("Waiting for printer to become available"),
                                                QStringLiteral("The printer may not exist or is unavailable"), QStringLiteral("Unable to locate printer"),
                                                QStringLiteral("The printer is not responding")};
            const QString message = current.value(QStringLiteral("message")).toString();
            if (trouble.isEmpty() && current.value(QStringLiteral("stateCode")).toInt() == IPP_PSTATE_PROCESSING
                && std::any_of(waiting.cbegin(), waiting.cend(), [&message](const QString &w) {
                       return message.startsWith(w);
                   })) {
                trouble = QStringLiteral("Offline");
            }
            QString status = state == IPP_PSTATE_PROCESSING ? QStringLiteral("Printing")
                : state == IPP_PSTATE_STOPPED                ? QStringLiteral("Paused")
                                                             : QStringLiteral("Ready");
            if (!trouble.isEmpty()) {
                status = trouble;
            }
            current.insert(QStringLiteral("status"), status);
            current.insert(QStringLiteral("paused"), state == IPP_PSTATE_STOPPED);
            current.insert(QStringLiteral("problem"), !trouble.isEmpty() && trouble != QLatin1String("Connecting"));
            current.insert(QStringLiteral("isDefault"), name == userDefault);
            list << current;
        }
        current.clear();
        markers = MarkerLists();
    };
    for (ipp_attribute_t *attr = ippFirstAttribute(response); attr; attr = ippNextAttribute(response)) {
        const char *name = ippGetName(attr);
        if (!name) {
            finish();
            continue;
        }
        if (ippGetGroupTag(attr) != IPP_TAG_PRINTER) {
            continue;
        }
        const QByteArray key(name);
        if (key == "printer-name") {
            current.insert(QStringLiteral("name"), text(attr));
        } else if (key == "printer-info") {
            current.insert(QStringLiteral("info"), text(attr));
        } else if (key == "printer-location") {
            current.insert(QStringLiteral("location"), text(attr));
        } else if (key == "printer-make-and-model") {
            current.insert(QStringLiteral("model"), text(attr));
        } else if (key == "device-uri") {
            current.insert(QStringLiteral("uri"), text(attr));
        } else if (key == "printer-state") {
            current.insert(QStringLiteral("stateCode"), ippGetInteger(attr, 0));
        } else if (key == "printer-state-message") {
            current.insert(QStringLiteral("message"), text(attr));
        } else if (key == "printer-state-reasons") {
            current.insert(QStringLiteral("reasons"), texts(attr));
        } else if (key == "printer-is-accepting-jobs") {
            current.insert(QStringLiteral("accepting"), ippGetBoolean(attr, 0) != 0);
        } else if (key == "queued-job-count") {
            current.insert(QStringLiteral("jobs"), ippGetInteger(attr, 0));
        } else {
            markers.take(key, attr);
        }
    }
    finish();
    ippDelete(response);

    // Every person's documents, as Windows' print queue shows them; CUPS
    // hides other people's titles itself (JobPrivateValues).
    QHash<QString, QVariantList> queues;
    cups_job_t *jobs = nullptr;
    const int count = cupsGetJobs2(CUPS_HTTP_DEFAULT, &jobs, nullptr, 0, CUPS_WHICHJOBS_ACTIVE);
    for (int i = 0; i < count; ++i) {
        const cups_job_t &job = jobs[i];
        const QString state = job.state == IPP_JSTATE_PROCESSING ? QStringLiteral("Printing")
            : job.state == IPP_JSTATE_HELD                       ? QStringLiteral("Held")
            : job.state == IPP_JSTATE_STOPPED                    ? QStringLiteral("Stopped")
                                                                 : QStringLiteral("Waiting");
        queues[QString::fromUtf8(job.dest)] << QVariantMap{
            {QStringLiteral("id"), job.id},
            {QStringLiteral("title"), QString::fromUtf8(job.title)},
            {QStringLiteral("user"), QString::fromUtf8(job.user)},
            {QStringLiteral("state"), state},
            {QStringLiteral("size"), job.size},
            {QStringLiteral("created"), QDateTime::fromSecsSinceEpoch(job.creation_time)},
        };
    }
    cupsFreeJobs(count, jobs);
    for (QVariant &printer : list) {
        QVariantMap map = printer.toMap();
        map.insert(QStringLiteral("queue"), queues.value(map.value(QStringLiteral("name")).toString()));
        printer = map;
    }
    return list;
}

struct SearchState {
    QSet<QString> known;
    QSet<QString> seen;
    QVariantList found;
    std::function<void(const QVariantList &)> publish;
};

int onDest(void *data, unsigned flags, cups_dest_t *dest)
{
    auto *state = static_cast<SearchState *>(data);
    if (dest->instance) {
        return 1;
    }
    const QString uri = QString::fromUtf8(cupsGetOption("device-uri", dest->num_options, dest->options));
    const QString name = QString::fromUtf8(dest->name);
    if (flags & CUPS_DEST_FLAGS_REMOVED) {
        for (int i = 0; i < state->found.size(); ++i) {
            if (state->found.at(i).toMap().value(QStringLiteral("uri")).toString() == uri) {
                state->found.removeAt(i);
                state->publish(state->found);
                break;
            }
        }
        return 1;
    }
    if (uri.isEmpty() || state->known.contains(uri) || state->known.contains(name) || state->seen.contains(uri)) {
        return 1;
    }
    state->seen.insert(uri);
    QString info = QString::fromUtf8(cupsGetOption("printer-info", dest->num_options, dest->options));
    if (info.isEmpty()) {
        info = name;
        info.replace(QLatin1Char('_'), QLatin1Char(' '));
    }
    state->found << QVariantMap{
        {QStringLiteral("name"), info},
        {QStringLiteral("model"), QString::fromUtf8(cupsGetOption("printer-make-and-model", dest->num_options, dest->options))},
        {QStringLiteral("location"), QString::fromUtf8(cupsGetOption("printer-location", dest->num_options, dest->options))},
        {QStringLiteral("uri"), uri},
        {QStringLiteral("scans"), false},
    };
    state->publish(state->found);
    return 1;
}

// lpadmin asks for a password on the terminal when CUPS wants one. Settings
// started from a terminal would hang there, so the helper gets none and
// fails at once instead, and pkexec takes over.
void detach(QProcess *process)
{
    process->setChildProcessModifier([] {
        ::setsid();
    });
    process->setStandardInputFile(QProcess::nullDevice());
}

// lpadmin's last line, with the two failures people meet most put in
// plain words.
QString plainReason(const QString &output)
{
    static const QRegularExpression unreachable(QStringLiteral("Unable to connect|not known|No route to host|Connection refused|timed out"),
                                                QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression needsDriver(QStringLiteral("Unable to create PPD|does not support required IPP"), QRegularExpression::CaseInsensitiveOption);
    if (unreachable.match(output).hasMatch()) {
        return QStringLiteral("Nothing answered at that address. Check the address, and that the printer is switched on and on the same network.");
    }
    if (needsDriver.match(output).hasMatch()) {
        return QStringLiteral("This printer doesn't work without a driver. Choose one in More printer settings.");
    }
    return output.section(QLatin1Char('\n'), -1).remove(QRegularExpression(QStringLiteral("^\\S+: ")));
}

bool refused(const QString &output)
{
    static const QRegularExpression words(QStringLiteral("forbidden|unauthori[sz]ed|not authori[sz]ed|password|permission"),
                                          QRegularExpression::CaseInsensitiveOption);
    return words.match(output).hasMatch();
}
}

Printers::Printers(QObject *parent)
    : QObject(parent)
{
    reload();
}

Printers::~Printers()
{
    if (m_cancelSearch) {
        *m_cancelSearch = 1;
    }
}

void Printers::reload()
{
    QPointer<Printers> self(this);
    QThread *thread = QThread::create([self] {
        const bool running = cupsRunning();
        const QVariantList queues = running ? readQueues() : QVariantList();
        QMetaObject::invokeMethod(qApp, [self, running, queues] {
            if (!self) {
                return;
            }
            self->m_available = running;
            self->m_printers = queues;
            self->markScanners();
            Q_EMIT self->changed();
        });
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void Printers::search()
{
    stopSearch();
    m_cancelSearch = std::make_shared<std::atomic<int>>(0);
    m_found.clear();
    m_searching = true;
    const int generation = ++m_searchGeneration;
    Q_EMIT foundChanged();

    QSet<QString> known;
    for (const QVariant &p : std::as_const(m_printers)) {
        known.insert(p.toMap().value(QStringLiteral("uri")).toString());
        known.insert(p.toMap().value(QStringLiteral("name")).toString());
    }
    QPointer<Printers> self(this);
    QThread *thread = QThread::create([self, known, generation, cancel = m_cancelSearch] {
        SearchState state;
        state.known = known;
        state.publish = [self, generation](const QVariantList &found) {
            QMetaObject::invokeMethod(qApp, [self, generation, found] {
                if (self && self->m_searchGeneration == generation) {
                    self->m_found = found;
                    self->markScanners();
                    Q_EMIT self->foundChanged();
                }
            });
        };
        // cupsEnumDests polls a plain int; this thread copies the shared
        // flag into it, so Settings can stop a search it no longer shows.
        int stop = 0;
        QThread *watcher = QThread::create([&stop, cancel] {
            while (!stop && !*cancel) {
                QThread::msleep(100);
            }
            stop = 1;
        });
        watcher->start();
        cupsEnumDests(CUPS_DEST_FLAGS_NONE, 8000, &stop, 0, 0, onDest, &state);
        stop = 1;
        watcher->wait();
        delete watcher;
        QMetaObject::invokeMethod(qApp, [self, generation] {
            if (self && self->m_searchGeneration == generation) {
                self->m_searching = false;
                Q_EMIT self->foundChanged();
            }
        });
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
    findScanners();
}

void Printers::stopSearch()
{
    if (m_cancelSearch) {
        *m_cancelSearch = 1;
    }
    if (m_searching) {
        m_searching = false;
        ++m_searchGeneration;
        Q_EMIT foundChanged();
    }
}

void Printers::setBusy(const QString &busy)
{
    if (m_busy != busy) {
        m_busy = busy;
        Q_EMIT busyChanged();
    }
}

// A CUPS queue name: letters, digits, dashes and underscores, unique.
QString Printers::queueName(const QString &name) const
{
    QString base = name.normalized(QString::NormalizationForm_KD);
    base.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]+")), QStringLiteral("_"));
    base.remove(QRegularExpression(QStringLiteral("^_+|_+$")));
    if (base.isEmpty()) {
        base = QStringLiteral("Printer");
    }
    base.truncate(100);
    QSet<QString> taken;
    for (const QVariant &p : m_printers) {
        taken.insert(p.toMap().value(QStringLiteral("name")).toString().toLower());
    }
    QString candidate = base;
    for (int i = 2; taken.contains(candidate.toLower()); ++i) {
        candidate = base + QLatin1Char('_') + QString::number(i);
    }
    return candidate;
}

void Printers::admin(const QString &busy, const QString &program, const QStringList &arguments, const QString &successTitle, const QString &success,
                     const QString &failure, const std::function<void()> &then, const std::function<void()> &otherwise)
{
    setBusy(busy);
    auto process = new QProcess(this);
    detach(process);
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, &QProcess::finished, this, [=, this](int code, QProcess::ExitStatus status) {
        const QString output = QString::fromUtf8(process->readAll()).trimmed();
        process->deleteLater();
        if (status == QProcess::NormalExit && code == 0) {
            setBusy({});
            reload();
            Q_EMIT done(successTitle, success);
            if (then) {
                then();
            }
            return;
        }
        if (!refused(output)) {
            setBusy({});
            reload();
            Q_EMIT failed(failure, plainReason(output));
            if (otherwise) {
                otherwise();
            }
            return;
        }
        // CUPS wants an administrator: ask through polkit.
        auto elevated = new QProcess(this);
        detach(elevated);
        elevated->setProcessChannelMode(QProcess::MergedChannels);
        connect(elevated, &QProcess::finished, this, [=, this](int code, QProcess::ExitStatus status) {
            const QString more = QString::fromUtf8(elevated->readAll()).trimmed();
            elevated->deleteLater();
            setBusy({});
            reload();
            if (status == QProcess::NormalExit && code == 0) {
                Q_EMIT done(successTitle, success);
                if (then) {
                    then();
                }
            } else {
                if (code == 126 || code == 127) {
                    Q_EMIT failed(failure, QStringLiteral("An administrator's password is needed for this."));
                } else {
                    Q_EMIT failed(failure, plainReason(more));
                }
                if (otherwise) {
                    otherwise();
                }
            }
        });
        elevated->start(QStringLiteral("pkexec"), QStringList{program} + arguments);
    });
    process->start(program, arguments);
}

void Printers::add(const QString &uri, const QString &name)
{
    stopSearch();
    const QString queue = queueName(name);
    // "everywhere" builds the queue from what the printer says about itself
    // (IPP Everywhere, AirPrint and Mopria printers), so no driver is needed.
    // The first printer added becomes the default, as on Windows.
    const bool first = m_printers.isEmpty();
    admin(QStringLiteral("add"), QStringLiteral("/usr/sbin/lpadmin"),
          {QStringLiteral("-p"), queue, QStringLiteral("-E"), QStringLiteral("-v"), uri, QStringLiteral("-m"), QStringLiteral("everywhere"),
           QStringLiteral("-D"), name},
          QStringLiteral("Printer added"), name + QStringLiteral(" is ready to use."), QStringLiteral("The printer was not added"), [this, queue, first, uri] {
              for (int i = 0; i < m_found.size(); ++i) {
                  if (m_found.at(i).toMap().value(QStringLiteral("uri")).toString() == uri) {
                      m_found.removeAt(i);
                      Q_EMIT foundChanged();
                      break;
                  }
              }
              if (first) {
                  setDefault(queue, false);
              }
          },
          [this, queue] {
              // lpadmin creates the queue before it asks the printer what it
              // can do, so a printer it can't drive would leave a queue that
              // only ever fails.
              auto cleanup = new QProcess(this);
              detach(cleanup);
              connect(cleanup, &QProcess::finished, this, [this, cleanup] {
                  cleanup->deleteLater();
                  reload();
              });
              cleanup->start(QStringLiteral("/usr/sbin/lpadmin"), {QStringLiteral("-x"), queue});
          });
}

void Printers::addAddress(const QString &address)
{
    QString host = address.trimmed();
    QUrl url = QUrl::fromUserInput(host);
    if (host.contains(QStringLiteral("://"))) {
        url = QUrl(host);
    } else {
        url = QUrl(QStringLiteral("ipp://") + host + QStringLiteral("/ipp/print"));
    }
    if (!url.isValid() || url.host().isEmpty() || !QStringList{QStringLiteral("ipp"), QStringLiteral("ipps")}.contains(url.scheme())) {
        Q_EMIT failed(QStringLiteral("The printer was not added"), QStringLiteral("Type the printer's network name or IP address, for example 192.168.1.20."));
        return;
    }
    add(url.toString(), url.host());
}

void Printers::setDefault(const QString &name, bool announce)
{
    // The account's own default (~/.cups/lpoptions), which every app's print
    // dialog reads through CUPS. No password is needed.
    cups_dest_t *dests = nullptr;
    const int count = cupsGetDests2(CUPS_HTTP_DEFAULT, &dests);
    bool found = false;
    for (int i = 0; i < count; ++i) {
        const bool match = !dests[i].instance && name == QString::fromUtf8(dests[i].name);
        dests[i].is_default = match;
        found = found || match;
    }
    if (found && cupsSetDests2(CUPS_HTTP_DEFAULT, count, dests) == 0) {
        if (announce) {
            Q_EMIT done(QStringLiteral("Default printer changed"), QStringLiteral("Apps will print to it unless you choose another printer."));
        }
    } else {
        Q_EMIT failed(QStringLiteral("The default printer was not changed"), QString::fromUtf8(cupsLastErrorString()));
    }
    cupsFreeDests(count, dests);
    reload();
}

void Printers::printTestPage(const QString &name)
{
    setBusy(name);
    QPointer<Printers> self(this);
    QThread *thread = QThread::create([self, name] {
        const QByteArray queue = name.toUtf8();
        const int job = cupsPrintFile2(CUPS_HTTP_DEFAULT, queue.constData(), testPage.toUtf8().constData(), "Test page", 0, nullptr);
        const QString error = job > 0 ? QString() : QString::fromUtf8(cupsLastErrorString());
        QMetaObject::invokeMethod(qApp, [self, job, error] {
            if (!self) {
                return;
            }
            self->setBusy({});
            self->reload();
            if (job > 0) {
                Q_EMIT self->done(QStringLiteral("Test page sent"), QStringLiteral("The test page is on its way to the printer."));
            } else {
                Q_EMIT self->failed(QStringLiteral("The test page was not sent"), error);
            }
        });
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void Printers::remove(const QString &name)
{
    admin(name, QStringLiteral("/usr/sbin/lpadmin"), {QStringLiteral("-x"), name}, QStringLiteral("Printer removed"),
          QStringLiteral("Apps no longer offer it. You can add it again at any time."),
          QStringLiteral("The printer was not removed"));
}

void Printers::resume(const QString &name)
{
    admin(name, QStringLiteral("/usr/sbin/cupsenable"), {name}, QStringLiteral("Printing resumed"), QStringLiteral("Documents waiting for it will print now."),
          QStringLiteral("The printer was not resumed"));
}

void Printers::cancelJobs(const QString &name)
{
    const QByteArray queue = name.toUtf8();
    if (cupsCancelJob2(CUPS_HTTP_DEFAULT, queue.constData(), CUPS_JOBID_ALL, 0) == IPP_STATUS_OK) {
        Q_EMIT done(QStringLiteral("Documents cancelled"), QStringLiteral("Your documents waiting for this printer were cancelled."));
    } else {
        Q_EMIT failed(QStringLiteral("The documents were not cancelled"), QString::fromUtf8(cupsLastErrorString()));
    }
    reload();
}

void Printers::cancelJob(const QString &name, int job)
{
    const QByteArray queue = name.toUtf8();
    if (cupsCancelJob2(CUPS_HTTP_DEFAULT, queue.constData(), job, 0) == IPP_STATUS_OK) {
        Q_EMIT done(QStringLiteral("Document cancelled"), QStringLiteral("It was taken out of the queue."));
    } else {
        Q_EMIT failed(QStringLiteral("The document was not cancelled"), QString::fromUtf8(cupsLastErrorString()));
    }
    reload();
}

void Printers::pause(const QString &name)
{
    admin(name, QStringLiteral("/usr/sbin/cupsdisable"), {name}, QStringLiteral("Printing paused"),
          QStringLiteral("Documents wait in the queue until you resume the printer."), QStringLiteral("The printer was not paused"));
}

namespace
{
const QStringList preferenceOptions = {QStringLiteral("media"), QStringLiteral("sides"), QStringLiteral("print-color-mode")};

QString choiceText(const QString &option, const QString &value, cups_dest_t *dest, cups_dinfo_t *info)
{
    if (option == QLatin1String("sides")) {
        if (value == QLatin1String("one-sided")) {
            return QStringLiteral("One-sided");
        }
        if (value == QLatin1String("two-sided-long-edge")) {
            return QStringLiteral("Two-sided, flip on long edge");
        }
        if (value == QLatin1String("two-sided-short-edge")) {
            return QStringLiteral("Two-sided, flip on short edge");
        }
    } else if (option == QLatin1String("print-color-mode")) {
        static const QList<std::pair<const char *, const char *>> words = {
            {"color", "Colour"},
            {"monochrome", "Black and white"},
            {"auto", "Automatic"},
            {"auto-monochrome", "Automatic, black and white"},
            {"bi-level", "Black and white, no greys"},
            {"process-monochrome", "Black and white, using every ink"},
            {"highlight", "Black and one colour"},
        };
        for (const auto &[key, word] : words) {
            if (value == QLatin1String(key)) {
                return QString::fromLatin1(word);
            }
        }
    } else if (option == QLatin1String("media")) {
        // PWG self-describing names (iso_a4_210x297mm) carry the size's
        // everyday name in the PPD column (A4, Letter).
        const QByteArray raw = value.toUtf8();
        if (pwg_media_t *media = pwgMediaForPWG(raw.constData()); media && media->ppd) {
            return QString::fromUtf8(media->ppd);
        }
    }
    const QByteArray o = option.toUtf8();
    const QByteArray v = value.toUtf8();
    const char *localised = cupsLocalizeDestValue(CUPS_HTTP_DEFAULT, dest, info, o.constData(), v.constData());
    return localised ? QString::fromUtf8(localised) : value;
}
}

QVariantMap Printers::preferences(const QString &name) const
{
    QVariantMap out;
    const QByteArray queue = name.toUtf8();
    cups_dest_t *dest = cupsGetNamedDest(CUPS_HTTP_DEFAULT, queue.constData(), nullptr);
    if (!dest) {
        return out;
    }
    cups_dinfo_t *info = cupsCopyDestInfo(CUPS_HTTP_DEFAULT, dest);
    if (!info) {
        cupsFreeDests(1, dest);
        return out;
    }
    for (const QString &option : preferenceOptions) {
        const QByteArray o = option.toUtf8();
        ipp_attribute_t *supported = cupsFindDestSupported(CUPS_HTTP_DEFAULT, dest, info, o.constData());
        QVariantList choices;
        QSet<QString> seen;
        for (int i = 0; supported && i < ippGetCount(supported); ++i) {
            const QString value = QString::fromUtf8(ippGetString(supported, i, nullptr));
            // custom_min_ and custom_max_ are the limits of a custom size,
            // not sizes.
            if (value.isEmpty() || value.startsWith(QLatin1String("custom_")) || seen.contains(value)) {
                continue;
            }
            seen.insert(value);
            choices << QVariantMap{{QStringLiteral("value"), value}, {QStringLiteral("text"), choiceText(option, value, dest, info)}};
        }
        if (choices.size() < 2) {
            continue;
        }
        // The person's own choice (lpoptions) first, then the printer's.
        QString current = QString::fromUtf8(cupsGetOption(o.constData(), dest->num_options, dest->options));
        if (current.isEmpty()) {
            ipp_attribute_t *def = cupsFindDestDefault(CUPS_HTTP_DEFAULT, dest, info, o.constData());
            current = def ? QString::fromUtf8(ippGetString(def, 0, nullptr)) : QString();
        }
        const QString key = option == QLatin1String("print-color-mode") ? QStringLiteral("color") : option;
        out.insert(key, QVariantMap{{QStringLiteral("value"), current}, {QStringLiteral("choices"), choices}});
    }
    cupsFreeDestInfo(info);
    cupsFreeDests(1, dest);
    return out;
}

void Printers::setPreference(const QString &name, const QString &option, const QString &value)
{
    if (!preferenceOptions.contains(option)) {
        return;
    }
    const QByteArray queue = name.toUtf8();
    const QByteArray o = option.toUtf8();
    const QByteArray v = value.toUtf8();
    cups_dest_t *dests = nullptr;
    const int count = cupsGetDests2(CUPS_HTTP_DEFAULT, &dests);
    cups_dest_t *dest = cupsGetDest(queue.constData(), nullptr, count, dests);
    bool saved = false;
    if (dest) {
        cups_dinfo_t *info = cupsCopyDestInfo(CUPS_HTTP_DEFAULT, dest);
        const bool supported = info && cupsCheckDestSupported(CUPS_HTTP_DEFAULT, dest, info, o.constData(), v.constData());
        if (info) {
            cupsFreeDestInfo(info);
        }
        if (supported) {
            // Kept per person, as "lpoptions -p name -o option=value" does,
            // and read by every app's print dialog through CUPS.
            dest->num_options = cupsAddOption(o.constData(), v.constData(), dest->num_options, &dest->options);
            saved = cupsSetDests2(CUPS_HTTP_DEFAULT, count, dests) == 0;
        }
    }
    cupsFreeDests(count, dests);
    if (!saved) {
        Q_EMIT failed(QStringLiteral("The printing preference was not saved"), QString::fromUtf8(cupsLastErrorString()));
    }
}

namespace
{
// cupsd asks for a password when the account may not list devices. Without
// this CUPS would ask on a terminal Settings was started from; with it the
// request is refused and an administrator is asked through pkexec instead.
const char *noPassword(const char *, http_t *, const char *, const char *, void *)
{
    return nullptr;
}

struct DeviceList {
    QSet<QString> known;
    QVariantList devices;
};

void onDevice(const char *deviceClass, const char *, const char *info, const char *model, const char *uri, const char *, void *data)
{
    auto *list = static_cast<DeviceList *>(data);
    const QString address = QString::fromUtf8(uri);
    // Bare scheme names (socket, lpd, ipp) are backends offering to be
    // typed in, not printers.
    // cups-brf is CUPS's braille embosser output to a file, not a device.
    if (!address.contains(QLatin1Char(':')) || address.startsWith(QLatin1String("cups-brf:")) || list->known.contains(address)) {
        return;
    }
    QString makeAndModel = QString::fromUtf8(model);
    if (makeAndModel.compare(QLatin1String("Unknown"), Qt::CaseInsensitive) == 0) {
        makeAndModel.clear();
    }
    list->devices << QVariantMap{{QStringLiteral("uri"), address},
                                 {QStringLiteral("name"), QString::fromUtf8(info)},
                                 {QStringLiteral("model"), makeAndModel},
                                 {QStringLiteral("kind"), qstrcmp(deviceClass, "direct") == 0 ? QStringLiteral("usb") : QStringLiteral("network")}};
}

// The same list from "lpinfo -l -v", for when only an administrator may see
// it.
QVariantList parseLpinfo(const QString &output, const QSet<QString> &known)
{
    DeviceList list;
    list.known = known;
    QVariantMap current;
    auto finish = [&] {
        if (!current.isEmpty()) {
            const QByteArray cls = current.value(QStringLiteral("class")).toString().toUtf8();
            const QByteArray info = current.value(QStringLiteral("info")).toString().toUtf8();
            const QByteArray model = current.value(QStringLiteral("make-and-model")).toString().toUtf8();
            const QByteArray uri = current.value(QStringLiteral("uri")).toString().toUtf8();
            onDevice(cls.constData(), nullptr, info.constData(), model.constData(), uri.constData(), nullptr, &list);
        }
        current.clear();
    };
    static const QRegularExpression field(QStringLiteral("^\\s*([a-z-]+) = (.*)$"));
    const QStringList lines = output.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        QString body = line;
        if (line.startsWith(QLatin1String("Device: "))) {
            finish();
            body = line.mid(8);
        }
        const auto m = field.match(body);
        if (m.hasMatch()) {
            current.insert(m.captured(1), m.captured(2).trimmed());
        }
    }
    finish();
    return list.devices;
}
}

void Printers::finishDevices(const QVariantList &devices)
{
    m_devices = devices;
    m_findingDevices = false;
    Q_EMIT devicesChanged();
}

void Printers::findDevices()
{
    if (m_findingDevices) {
        return;
    }
    m_findingDevices = true;
    m_devices.clear();
    Q_EMIT devicesChanged();

    QSet<QString> known;
    for (const QVariant &p : std::as_const(m_printers)) {
        known.insert(p.toMap().value(QStringLiteral("uri")).toString());
    }
    QPointer<Printers> self(this);
    QThread *thread = QThread::create([self, known] {
        cupsSetPasswordCB2(noPassword, nullptr);
        DeviceList list;
        list.known = known;
        const ipp_status_t status = cupsGetDevices(CUPS_HTTP_DEFAULT, 8, CUPS_INCLUDE_ALL, CUPS_EXCLUDE_NONE, onDevice, &list);
        const bool refused = status == IPP_STATUS_ERROR_FORBIDDEN || status == IPP_STATUS_ERROR_NOT_AUTHORIZED || status == IPP_STATUS_ERROR_NOT_AUTHENTICATED;
        const QVariantList devices = list.devices;
        QMetaObject::invokeMethod(qApp, [self, refused, devices, known] {
            if (!self) {
                return;
            }
            if (!refused) {
                self->finishDevices(devices);
                return;
            }
            auto process = new QProcess(self);
            detach(process);
            QObject::connect(process, &QProcess::finished, self, [self, process, known](int code, QProcess::ExitStatus status) {
                const QString output = QString::fromUtf8(process->readAllStandardOutput());
                process->deleteLater();
                if (!self) {
                    return;
                }
                const bool ok = status == QProcess::NormalExit && code == 0;
                self->finishDevices(ok ? parseLpinfo(output, known) : QVariantList());
                if (!ok) {
                    Q_EMIT self->failed(QStringLiteral("Printers plugged into this computer can't be listed"),
                                        QStringLiteral("An administrator's password is needed to look for them."));
                }
            });
            process->start(QStringLiteral("pkexec"), {QStringLiteral("/usr/sbin/lpinfo"), QStringLiteral("-l"), QStringLiteral("-v")});
        });
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void Printers::loadDrivers()
{
    if (m_loadingDrivers || !m_drivers.isEmpty()) {
        return;
    }
    m_loadingDrivers = true;
    Q_EMIT driversChanged();
    QPointer<Printers> self(this);
    QThread *thread = QThread::create([self] {
        QList<Driver> drivers;
        // CUPS-Get-PPDs is open to everyone (lpinfo -m); it can take a few
        // seconds while cups-driverd reads every driver package.
        ipp_t *request = ippNewRequest(IPP_OP_CUPS_GET_PPDS);
        static const char *const wanted[] = {"ppd-name", "ppd-make", "ppd-make-and-model"};
        ippAddStrings(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD, "requested-attributes", int(std::size(wanted)), nullptr, wanted);
        if (ipp_t *response = cupsDoRequest(CUPS_HTTP_DEFAULT, request, "/")) {
            Driver current;
            auto finish = [&] {
                // "everywhere" and the driverless PPDs build the queue from
                // what the printer says, which the main page already does.
                if (!current.ppd.isEmpty() && !current.make.isEmpty() && current.ppd != QLatin1String("everywhere")
                    && !current.ppd.startsWith(QLatin1String("driverless:"))) {
                    drivers << current;
                }
                current = Driver();
            };
            for (ipp_attribute_t *attr = ippFirstAttribute(response); attr; attr = ippNextAttribute(response)) {
                const char *attrName = ippGetName(attr);
                if (!attrName) {
                    finish();
                    continue;
                }
                const QByteArray key(attrName);
                if (key == "ppd-name") {
                    current.ppd = text(attr);
                } else if (key == "ppd-make") {
                    current.make = text(attr).trimmed();
                } else if (key == "ppd-make-and-model") {
                    current.model = text(attr).trimmed();
                }
            }
            finish();
            ippDelete(response);
        }
        std::sort(drivers.begin(), drivers.end(), [](const Driver &a, const Driver &b) {
            return a.model.compare(b.model, Qt::CaseInsensitive) < 0;
        });
        QMetaObject::invokeMethod(qApp, [self, drivers] {
            if (!self) {
                return;
            }
            self->m_drivers = drivers;
            self->m_loadingDrivers = false;
            Q_EMIT self->driversChanged();
            if (drivers.isEmpty()) {
                Q_EMIT self->failed(QStringLiteral("No printer drivers were found"),
                                    QStringLiteral("Install the driver package from your printer's maker, then try again."));
            }
        });
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

QStringList Printers::makes() const
{
    QStringList out;
    for (const Driver &d : m_drivers) {
        if (!out.contains(d.make, Qt::CaseInsensitive)) {
            out << d.make;
        }
    }
    out.sort(Qt::CaseInsensitive);
    return out;
}

QVariantList Printers::models(const QString &make) const
{
    QVariantList out;
    for (const Driver &d : m_drivers) {
        if (d.make.compare(make, Qt::CaseInsensitive) == 0) {
            out << QVariantMap{{QStringLiteral("value"), d.ppd}, {QStringLiteral("text"), d.model}};
        }
    }
    return out;
}

QString Printers::makeFor(const QString &model) const
{
    QString best;
    const QStringList all = makes();
    for (const QString &make : all) {
        if (model.startsWith(make, Qt::CaseInsensitive) && make.length() > best.length()) {
            best = make;
        }
    }
    return best;
}

QString Printers::addressUri(const QString &address, const QString &protocol) const
{
    const QString host = address.trimmed();
    static const QRegularExpression hostName(QStringLiteral("^[A-Za-z0-9.:\\[\\]-]+$"));
    if (!hostName.match(host).hasMatch()) {
        return {};
    }
    if (protocol == QLatin1String("lpd")) {
        return QStringLiteral("lpd://%1/queue").arg(host);
    }
    if (protocol == QLatin1String("ipp")) {
        return QStringLiteral("ipp://%1/ipp/print").arg(host);
    }
    // Port 9100 (HP JetDirect, "raw") is what most printers that need a
    // driver listen on.
    return QStringLiteral("socket://%1:9100").arg(host);
}

void Printers::addWithDriver(const QString &uri, const QString &driver, const QString &name)
{
    // lpadmin may run as root through pkexec, so only a driver CUPS listed
    // and a well-formed device address reach it, and neither can be read
    // as an option.
    const bool knownDriver = std::any_of(m_drivers.cbegin(), m_drivers.cend(), [&driver](const Driver &d) {
        return d.ppd == driver;
    });
    const QUrl url(uri);
    static const QRegularExpression scheme(QStringLiteral("^[a-z][a-z0-9+.-]*$"));
    if (!knownDriver || !url.isValid() || !scheme.match(url.scheme()).hasMatch() || uri.contains(QLatin1Char('\n')) || uri.startsWith(QLatin1Char('-'))
        || driver.startsWith(QLatin1Char('-'))) {
        Q_EMIT failed(QStringLiteral("The printer was not added"), QStringLiteral("Choose the printer and its driver again."));
        return;
    }
    const QString info = name.trimmed().isEmpty() ? QStringLiteral("Printer") : name.trimmed();
    const QString queue = queueName(info);
    const bool first = m_printers.isEmpty();
    admin(QStringLiteral("add"), QStringLiteral("/usr/sbin/lpadmin"),
          {QStringLiteral("-p"), queue, QStringLiteral("-E"), QStringLiteral("-v"), uri, QStringLiteral("-m"), driver, QStringLiteral("-D"), info},
          QStringLiteral("Printer added"), info + QStringLiteral(" is ready to use."), QStringLiteral("The printer was not added"), [this, queue, first, uri] {
              for (int i = 0; i < m_devices.size(); ++i) {
                  if (m_devices.at(i).toMap().value(QStringLiteral("uri")).toString() == uri) {
                      m_devices.removeAt(i);
                      Q_EMIT devicesChanged();
                      break;
                  }
              }
              if (first) {
                  setDefault(queue, false);
              }
          });
}

namespace
{
bool loopback(const QString &host)
{
    return host == QLatin1String("localhost") || host.startsWith(QLatin1String("127.")) || host == QLatin1String("::1");
}

// sane-airscan's own list of the eSCL and WSD scanners it finds, one per
// line under [devices]:  Office printer = http://192.168.1.20:80/eSCL/, eSCL
QVariantList parseAirscan(const QString &output)
{
    static const QRegularExpression line(QStringLiteral("^\\s*(.+?)\\s*=\\s*(\\S+?)\\s*,\\s*(eSCL|WSD)\\s*$"));
    // The same scanner over eSCL and WSD, IPv4 and IPv6, and on loopback
    // too when it is on this computer, is one entry: the address another
    // computer would use, over eSCL, where it offers one.
    auto rank = [](const QString &url, const QString &protocol) {
        const QString host = QUrl(url).host();
        return (protocol == QLatin1String("eSCL") ? 4 : 0) + (url.contains(QLatin1Char('[')) ? 0 : 2) + (loopback(host) ? 0 : 1);
    };
    QVariantList out;
    QHash<QString, int> at;
    const QStringList lines = output.split(QLatin1Char('\n'));
    for (const QString &text : lines) {
        const auto m = line.match(text);
        if (!m.hasMatch()) {
            continue;
        }
        const QString name = m.captured(1);
        const QString url = m.captured(2);
        const QString protocol = m.captured(3);
        const QVariantMap entry{{QStringLiteral("name"), name}, {QStringLiteral("url"), url}, {QStringLiteral("protocol"), protocol}};
        const QString key = name.toLower();
        if (!at.contains(key)) {
            at.insert(key, int(out.size()));
            out << entry;
        } else {
            const QVariantMap kept = out.at(at.value(key)).toMap();
            if (rank(url, protocol) > rank(kept.value(QStringLiteral("url")).toString(), kept.value(QStringLiteral("protocol")).toString())) {
                out[at.value(key)] = entry;
            }
        }
    }
    return out;
}

// The DNS-SD instance name in a dnssd:// device address, the name a
// printer's scanner half announces itself by too.
QString serviceName(const QString &uri)
{
    if (!uri.startsWith(QLatin1String("dnssd://"))) {
        return {};
    }
    const QString rest = uri.mid(8);
    const int end = int(rest.indexOf(QLatin1String("._")));
    return QUrl::fromPercentEncoding(rest.left(end < 0 ? rest.size() : end).toUtf8());
}

bool sameDevice(const QVariantMap &scanner, const QVariantMap &printer)
{
    const QString name = scanner.value(QStringLiteral("name")).toString();
    const QString uri = printer.value(QStringLiteral("uri")).toString();
    QString queue = printer.value(QStringLiteral("name")).toString();
    queue.replace(QLatin1Char('_'), QLatin1Char(' '));
    for (const QString &candidate : {serviceName(uri), printer.value(QStringLiteral("info")).toString(), queue}) {
        if (!candidate.isEmpty() && candidate.compare(name, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    // A printer added by its address: the same host. Every ipp-usb device
    // is on this computer's own address, so there only the name counts.
    const QString host = QUrl(uri).host();
    return !host.isEmpty() && !loopback(host) && host.compare(QUrl(scanner.value(QStringLiteral("url")).toString()).host(), Qt::CaseInsensitive) == 0;
}

KConfigGroup scannerConfig()
{
    return KSharedConfig::openConfig(QStringLiteral("wildos-settingsrc"), KConfig::NoGlobals)->group(QStringLiteral("Scanners"));
}
}

QStringList Printers::addedScanners() const
{
    return scannerConfig().readEntry("Added", QStringList());
}

QVariantList Printers::scanners() const
{
    QVariantList out;
    const QStringList added = addedScanners();
    for (const QString &name : added) {
        QVariantMap entry{{QStringLiteral("name"), name}, {QStringLiteral("url"), QString()}, {QStringLiteral("seen"), false}};
        for (const QVariant &s : m_networkScanners) {
            if (s.toMap().value(QStringLiteral("name")).toString().compare(name, Qt::CaseInsensitive) == 0) {
                entry.insert(QStringLiteral("url"), s.toMap().value(QStringLiteral("url")));
                entry.insert(QStringLiteral("seen"), true);
            }
        }
        out << entry;
    }
    return out;
}

bool Printers::partOfPrinter(const QVariantMap &scanner) const
{
    for (const QVariantList *list : {&m_printers, &m_found}) {
        for (const QVariant &p : *list) {
            if (sameDevice(scanner, p.toMap())) {
                return true;
            }
        }
    }
    return false;
}

QVariantList Printers::foundScanners() const
{
    QVariantList out;
    const QStringList added = addedScanners();
    for (const QVariant &s : m_networkScanners) {
        const QVariantMap scanner = s.toMap();
        if (!added.contains(scanner.value(QStringLiteral("name")).toString(), Qt::CaseInsensitive) && !partOfPrinter(scanner)) {
            out << scanner;
        }
    }
    return out;
}

void Printers::markScanners()
{
    for (QVariantList *list : {&m_printers, &m_found}) {
        for (QVariant &p : *list) {
            QVariantMap map = p.toMap();
            const bool scans = std::any_of(m_networkScanners.cbegin(), m_networkScanners.cend(), [&map](const QVariant &s) {
                return sameDevice(s.toMap(), map);
            });
            if (map.value(QStringLiteral("scans")).toBool() != scans) {
                map.insert(QStringLiteral("scans"), scans);
                p = map;
            }
        }
    }
    Q_EMIT scannersChanged();
}

void Printers::findScanners()
{
    const QString program = QStandardPaths::findExecutable(QStringLiteral("airscan-discover"));
    if (m_findingScanners || program.isEmpty()) {
        return;
    }
    m_findingScanners = true;
    Q_EMIT scannersChanged();
    auto process = new QProcess(this);
    detach(process);
    connect(process, &QProcess::finished, this, [this, process] {
        const QVariantList found = parseAirscan(QString::fromUtf8(process->readAllStandardOutput()));
        process->deleteLater();
        m_networkScanners = found;
        m_findingScanners = false;
        m_scannersLooked = true;
        markScanners();
        Q_EMIT changed();
        Q_EMIT foundChanged();
    });
    // airscan-discover listens for a few seconds, then prints what it
    // heard; a network that never answers must not leave it running.
    QTimer::singleShot(20000, process, [process] {
        process->kill();
    });
    process->start(program, {});
}

void Printers::addScanner(const QString &name)
{
    QStringList added = addedScanners();
    if (!added.contains(name, Qt::CaseInsensitive)) {
        added << name;
        KConfigGroup group = scannerConfig();
        group.writeEntry("Added", added);
        group.sync();
    }
    Q_EMIT scannersChanged();
    Q_EMIT done(QStringLiteral("Scanner added"), name + QStringLiteral(" is ready to use in Capture."));
}

void Printers::removeScanner(const QString &name)
{
    QStringList added = addedScanners();
    added.removeIf([&name](const QString &n) {
        return n.compare(name, Qt::CaseInsensitive) == 0;
    });
    KConfigGroup group = scannerConfig();
    group.writeEntry("Added", added);
    group.sync();
    Q_EMIT scannersChanged();
    Q_EMIT done(QStringLiteral("Scanner removed"), QStringLiteral("It's no longer in your list. Capture can still find it on the network."));
}

bool Printers::openCapture() const
{
    // Started by name, like Devices' openBackup: gtk-launch isn't in the
    // image.
    const QString program = QStandardPaths::findExecutable(QStringLiteral("wildos-capture"));
    return !program.isEmpty() && QProcess::startDetached(program, {QStringLiteral("--scan")});
}
