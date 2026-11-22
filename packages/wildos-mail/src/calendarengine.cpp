#include "calendarengine.h"
#include "formats.h"
#include "microsoftsignin.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJSEngine>
#include <QPointer>
#include <QSettings>
#include <QThreadPool>
#include <QRegularExpression>
#include <QUrl>
#include <QUuid>

#include <algorithm>

#include <libebackend/libebackend.h>

Q_LOGGING_CATEGORY(calendarLog, "wildos.calendar", QtInfoMsg)

namespace
{
const QChar sep(0x1f);
const QString holidaysUrl = QStringLiteral("https://www.gov.uk/bank-holidays/england-and-wales.ics");

// The colours a new calendar takes in turn (the board's list).
const QStringList palette = {
    QStringLiteral("#6f9bff"), QStringLiteral("#4cc38a"), QStringLiteral("#f0a04b"), QStringLiteral("#ee6f9a"),
    QStringLiteral("#a98bf5"), QStringLiteral("#46c2c9"), QStringLiteral("#e5c55a"), QStringLiteral("#9aa3b5"),
};

QString str(const gchar *s)
{
    return s ? QString::fromUtf8(s) : QString();
}

QString takeStr(gchar *s)
{
    const QString out = str(s);
    g_free(s);
    return out;
}

QString keyOf(const QString &calendar, const QString &uid, const QString &rid)
{
    return calendar + sep + uid + sep + rid;
}

bool splitKey(const QString &key, QString *calendar, QString *uid, QString *rid)
{
    const QStringList parts = key.split(sep);
    if (parts.size() != 3) {
        return false;
    }
    *calendar = parts[0];
    *uid = parts[1];
    *rid = parts[2];
    return true;
}

QString backendOf(ESource *source)
{
    if (!e_source_has_extension(source, E_SOURCE_EXTENSION_CALENDAR)) {
        return {};
    }
    auto *ext = E_SOURCE_BACKEND(e_source_get_extension(source, E_SOURCE_EXTENSION_CALENDAR));
    return str(e_source_backend_get_backend_name(ext));
}

QString mailtoless(const gchar *value)
{
    QString v = str(value);
    if (v.startsWith(QLatin1String("mailto:"), Qt::CaseInsensitive)) {
        v = v.mid(7);
    }
    return v.trimmed().toLower();
}

QString partstatName(ICalParameterPartstat p)
{
    switch (p) {
    case I_CAL_PARTSTAT_ACCEPTED:
        return QStringLiteral("going");
    case I_CAL_PARTSTAT_TENTATIVE:
        return QStringLiteral("maybe");
    case I_CAL_PARTSTAT_DECLINED:
        return QStringLiteral("notGoing");
    default:
        return QStringLiteral("waiting");
    }
}

// An sexp string, quoted for the calendar service.
QString quoted(const QString &text)
{
    QString t = text;
    t.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    t.replace(QLatin1Char('"'), QLatin1String("\\\""));
    return QLatin1Char('"') + t + QLatin1Char('"');
}

QString isoUtc(const QDateTime &t)
{
    return t.toUTC().toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'"));
}
}

CalendarEngine *CalendarEngine::instance()
{
    static CalendarEngine *engine = new CalendarEngine();
    return engine;
}

CalendarEngine *CalendarEngine::create(QQmlEngine *, QJSEngine *)
{
    CalendarEngine *engine = instance();
    QJSEngine::setObjectOwnership(engine, QJSEngine::CppOwnership);
    return engine;
}

CalendarEngine::CalendarEngine(QObject *parent)
    : QObject(parent)
{
    m_syncHold.setSingleShot(true);
    m_syncHold.setInterval(700);
    connect(&m_syncHold, &QTimer::timeout, this, &CalendarEngine::syncingChanged);
    const QString zoneId = QSettings().value(QStringLiteral("calendar/timeZone")).toString();
    m_zone = zoneId.isEmpty() ? QTimeZone::systemTimeZone() : QTimeZone(zoneId.toUtf8());
    if (!m_zone.isValid()) {
        m_zone = QTimeZone::systemTimeZone();
    }

    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(120);
    connect(&m_reloadTimer, &QTimer::timeout, this, &CalendarEngine::reload);

    m_cancel = g_cancellable_new();
    // The registry starts the calendar service on the session bus if it
    // isn't running yet, which can take a few seconds the first time.
    e_source_registry_new(m_cancel, &CalendarEngine::onRegistryReady, this);
}

CalendarEngine::~CalendarEngine()
{
    g_cancellable_cancel(m_cancel);
    for (ECalClientView *view : std::as_const(m_views)) {
        e_cal_client_view_stop(view, nullptr);
        g_object_unref(view);
    }
    for (ECalClient *client : std::as_const(m_clients)) {
        g_object_unref(client);
    }
    g_clear_object(&m_registry);
    g_clear_object(&m_cancel);
}

void CalendarEngine::onRegistryReady(GObject *, GAsyncResult *result, gpointer data)
{
    auto *self = static_cast<CalendarEngine *>(data);
    GError *error = nullptr;
    ESourceRegistry *registry = e_source_registry_new_finish(result, &error);
    if (!registry) {
        if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
            qCWarning(calendarLog) << "the calendar service didn't start:" << (error ? error->message : "");
            self->m_problem = tr("The calendar service didn't start.");
            Q_EMIT self->readyChanged();
        }
        g_clear_error(&error);
        return;
    }
    self->m_registry = registry;
    for (const char *signal : {"source-added", "source-removed", "source-changed", "source-enabled", "source-disabled"}) {
        g_signal_connect(registry, signal, G_CALLBACK(&CalendarEngine::onSourceChanged), self);
    }
    self->rebuildCalendars();
    Q_EMIT self->readyChanged();
}

void CalendarEngine::onSourceChanged(ESourceRegistry *, ESource *, gpointer data)
{
    auto *self = static_cast<CalendarEngine *>(data);
    // Several arrive at once when an account's calendars are found.
    QMetaObject::invokeMethod(self, &CalendarEngine::rebuildCalendars, Qt::QueuedConnection);
}

void CalendarEngine::rebuildCalendars()
{
    if (!m_registry) {
        return;
    }
    QVariantList calendars;
    QVariantList accounts;
    QSet<QString> present;

    GList *sources = e_source_registry_list_enabled(m_registry, E_SOURCE_EXTENSION_CALENDAR);
    for (GList *l = sources; l; l = l->next) {
        auto *source = E_SOURCE(l->data);
        const QString backend = backendOf(source);
        // Weather needs GNOME's location services and isn't offered.
        if (backend == QLatin1String("weather")) {
            continue;
        }
        const QString uid = str(e_source_get_uid(source));
        auto *selectable = E_SOURCE_SELECTABLE(e_source_get_extension(source, E_SOURCE_EXTENSION_CALENDAR));
        QString color = takeStr(e_source_selectable_dup_color(selectable));
        if (color.isEmpty()) {
            color = palette.value(calendars.size() % palette.size());
        }
        const bool selected = e_source_selectable_get_selected(selectable);

        QString group = tr("My calendars");
        QString groupUid;
        const QString parentUid = str(e_source_get_parent(source));
        if (!parentUid.isEmpty() && !parentUid.endsWith(QLatin1String("-stub"))) {
            if (ESource *parent = e_source_registry_ref_source(m_registry, parentUid.toUtf8().constData())) {
                group = str(e_source_get_display_name(parent));
                groupUid = parentUid;
                g_object_unref(parent);
            }
        }

        QString kind = backend;
        if (uid == QLatin1String("system-calendar")) {
            kind = QStringLiteral("system");
        }
        QString name = str(e_source_get_display_name(source));
        // The calendar every PC has, named as the board names it.
        if (uid == QLatin1String("system-calendar")) {
            name = tr("Calendar");
        } else if (uid == QLatin1String("birthdays")) {
            name = tr("Birthdays");
        }
        ECalClient *c = m_clients.value(uid);
        const bool readOnly = backend == QLatin1String("webcal") || backend == QLatin1String("contacts")
            || (c && e_client_is_readonly(E_CLIENT(c)));

        calendars << QVariantMap{
            {QStringLiteral("uid"), uid},
            {QStringLiteral("name"), name},
            {QStringLiteral("color"), color},
            {QStringLiteral("selected"), selected},
            {QStringLiteral("group"), group},
            {QStringLiteral("groupUid"), groupUid},
            {QStringLiteral("readOnly"), readOnly},
            {QStringLiteral("kind"), kind},
            // Who invitations go out as; empty where none can be sent.
            {QStringLiteral("email"), myAddress(uid)},
        };
        m_colors.insert(uid, color);
        if (readOnly) {
            m_readOnly.insert(uid);
        } else {
            m_readOnly.remove(uid);
        }
        present.insert(uid);
        if (selected && !m_clients.contains(uid)) {
            connectClient(source);
        } else if (!selected && m_clients.contains(uid)) {
            dropClient(uid);
        }
    }
    g_list_free_full(sources, g_object_unref);

    GList *collections = e_source_registry_list_enabled(m_registry, E_SOURCE_EXTENSION_COLLECTION);
    for (GList *l = collections; l; l = l->next) {
        auto *source = E_SOURCE(l->data);
        auto *ext = E_SOURCE_COLLECTION(e_source_get_extension(source, E_SOURCE_EXTENSION_COLLECTION));
        if (!e_source_collection_get_calendar_enabled(ext)) {
            continue;
        }
        const QString backend = str(e_source_backend_get_backend_name(E_SOURCE_BACKEND(ext)));
        accounts << QVariantMap{
            {QStringLiteral("uid"), str(e_source_get_uid(source))},
            {QStringLiteral("name"), str(e_source_get_display_name(source))},
            {QStringLiteral("kind"), backend},
            {QStringLiteral("email"), takeStr(e_source_collection_dup_identity(ext))},
        };
    }
    g_list_free_full(collections, g_object_unref);

    // The holidays and birthdays first in My calendars, after the PC's own.
    std::stable_sort(calendars.begin(), calendars.end(), [](const QVariant &a, const QVariant &b) {
        auto rank = [](const QVariantMap &m) {
            if (!m.value(QStringLiteral("groupUid")).toString().isEmpty()) {
                return 3;
            }
            const QString kind = m.value(QStringLiteral("kind")).toString();
            return kind == QLatin1String("system") ? 0 : (kind == QLatin1String("contacts") || kind == QLatin1String("webcal")) ? 2 : 1;
        };
        const QVariantMap ma = a.toMap();
        const QVariantMap mb = b.toMap();
        if (rank(ma) != rank(mb)) {
            return rank(ma) < rank(mb);
        }
        if (ma.value(QStringLiteral("group")) != mb.value(QStringLiteral("group"))) {
            return ma.value(QStringLiteral("group")).toString() < mb.value(QStringLiteral("group")).toString();
        }
        return false;
    });

    for (const QString &uid : m_clients.keys()) {
        if (!present.contains(uid)) {
            dropClient(uid);
        }
    }
    m_calendars = calendars;
    m_accounts = accounts;
    Q_EMIT calendarsChanged();
}

void CalendarEngine::connectClient(ESource *source)
{
    const QString uid = str(e_source_get_uid(source));
    m_clients.insert(uid, nullptr);
    auto *pending = new QString(uid);
    e_cal_client_connect(source, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, 30, m_cancel, &CalendarEngine::onClientConnected, pending);
}

void CalendarEngine::onClientConnected(GObject *, GAsyncResult *result, gpointer data)
{
    const QString uid = *static_cast<QString *>(data);
    delete static_cast<QString *>(data);
    GError *error = nullptr;
    EClient *client = e_cal_client_connect_finish(result, &error);
    CalendarEngine *self = instance();
    if (!client) {
        if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
            qCWarning(calendarLog) << "couldn't open calendar" << uid << (error ? error->message : "");
            self->m_clients.remove(uid);
        }
        g_clear_error(&error);
        return;
    }
    // Unselected while it was connecting.
    if (!self->m_clients.contains(uid)) {
        g_object_unref(client);
        return;
    }
    auto *cal = E_CAL_CLIENT(client);
    e_cal_client_set_default_timezone(cal, self->zone());
    self->m_clients.insert(uid, cal);
    self->rebuildCalendars();
    self->startView(uid);
    self->scheduleReload();
}

void CalendarEngine::dropClient(const QString &uid)
{
    if (ECalClientView *view = m_views.take(uid)) {
        g_signal_handlers_disconnect_by_data(view, this);
        e_cal_client_view_stop(view, nullptr);
        g_object_unref(view);
    }
    if (ECalClient *client = m_clients.take(uid)) {
        g_object_unref(client);
    }
    if (m_perCalendar.remove(uid)) {
        flatten();
    }
}

// Only a sign that something changed in the days on screen; the events
// themselves are fetched again, with repeats expanded.
void CalendarEngine::startView(const QString &uid)
{
    ECalClient *client = m_clients.value(uid);
    if (!client || !m_from.isValid()) {
        return;
    }
    if (ECalClientView *old = m_views.take(uid)) {
        g_signal_handlers_disconnect_by_data(old, this);
        e_cal_client_view_stop(old, nullptr);
        g_object_unref(old);
    }
    const QDateTime from(m_from, QTime(0, 0), m_zone);
    const QDateTime to(m_to, QTime(0, 0), m_zone);
    const QString sexp = QStringLiteral("(occur-in-time-range? (make-time \"%1\") (make-time \"%2\"))").arg(isoUtc(from), isoUtc(to));
    auto *pending = new QString(uid);
    e_cal_client_get_view(client, sexp.toUtf8().constData(), m_cancel, &CalendarEngine::onViewReady, pending);
}

void CalendarEngine::onViewReady(GObject *object, GAsyncResult *result, gpointer data)
{
    const QString uid = *static_cast<QString *>(data);
    delete static_cast<QString *>(data);
    ECalClientView *view = nullptr;
    GError *error = nullptr;
    if (!e_cal_client_get_view_finish(E_CAL_CLIENT(object), result, &view, &error)) {
        g_clear_error(&error);
        return;
    }
    CalendarEngine *self = instance();
    if (!self->m_clients.value(uid)) {
        g_object_unref(view);
        return;
    }
    if (ECalClientView *old = self->m_views.take(uid)) {
        g_signal_handlers_disconnect_by_data(old, self);
        e_cal_client_view_stop(old, nullptr);
        g_object_unref(old);
    }
    // Only changes matter; the first batch is what reload already has.
    e_cal_client_view_set_flags(view, E_CAL_CLIENT_VIEW_FLAGS_NONE, nullptr);
    for (const char *signal : {"objects-added", "objects-modified", "objects-removed"}) {
        g_signal_connect(view, signal, G_CALLBACK(&CalendarEngine::onViewObjects), self);
    }
    e_cal_client_view_start(view, nullptr);
    self->m_views.insert(uid, view);
}

void CalendarEngine::onViewObjects(ECalClientView *, gpointer, gpointer data)
{
    static_cast<CalendarEngine *>(data)->scheduleReload();
}

void CalendarEngine::scheduleReload()
{
    m_reloadTimer.start();
}

void CalendarEngine::run(std::function<void()> work, std::function<void()> done)
{
    QPointer<CalendarEngine> self(this);
    QThreadPool::globalInstance()->start([self, work, done]() {
        work();
        if (done && self) {
            QMetaObject::invokeMethod(self, done, Qt::QueuedConnection);
        }
    });
}

ECalClient *CalendarEngine::client(const QString &uid) const
{
    return m_clients.value(uid);
}

ICalTimezone *CalendarEngine::zone() const
{
    ICalTimezone *z = i_cal_timezone_get_builtin_timezone(m_zone.id().constData());
    return z ? z : i_cal_timezone_get_utc_timezone();
}

QDateTime CalendarEngine::toDateTime(ICalTime *time) const
{
    if (!time) {
        return {};
    }
    if (i_cal_time_is_date(time)) {
        return QDateTime(QDate(i_cal_time_get_year(time), i_cal_time_get_month(time), i_cal_time_get_day(time)), QTime(0, 0), m_zone);
    }
    ICalTimezone *z = i_cal_time_get_timezone(time);
    const time_t secs = i_cal_time_as_timet_with_zone(time, z ? z : zone());
    return QDateTime::fromSecsSinceEpoch(qint64(secs), m_zone);
}

QString CalendarEngine::myAddress(const QString &calendarUid) const
{
    if (!m_registry) {
        return {};
    }
    QString address;
    ESource *source = e_source_registry_ref_source(m_registry, calendarUid.toUtf8().constData());
    if (!source) {
        return {};
    }
    if (e_source_has_extension(source, E_SOURCE_EXTENSION_WEBDAV_BACKEND)) {
        auto *webdav = E_SOURCE_WEBDAV(e_source_get_extension(source, E_SOURCE_EXTENSION_WEBDAV_BACKEND));
        address = takeStr(e_source_webdav_dup_email_address(webdav));
    }
    if (address.isEmpty()) {
        const QString parentUid = str(e_source_get_parent(source));
        if (ESource *parent = parentUid.isEmpty() ? nullptr : e_source_registry_ref_source(m_registry, parentUid.toUtf8().constData())) {
            if (e_source_has_extension(parent, E_SOURCE_EXTENSION_COLLECTION)) {
                address = takeStr(e_source_collection_dup_identity(E_SOURCE_COLLECTION(e_source_get_extension(parent, E_SOURCE_EXTENSION_COLLECTION))));
            }
            g_object_unref(parent);
        }
    }
    g_object_unref(source);
    return address.toLower();
}

namespace
{
struct FetchContext {
    const CalendarEngine *engine;
    QString calendarUid;
    QString me;
    std::function<void(ICalComponent *, ICalTime *, ICalTime *)> add;
};

gboolean addInstance(ICalComponent *icomp, ICalTime *start, ICalTime *end, gpointer data, GCancellable *, GError **)
{
    static_cast<FetchContext *>(data)->add(icomp, start, end);
    return TRUE;
}
}

QList<CalendarEngine::Instance> CalendarEngine::fetch(ECalClient *client, const QString &calendarUid, const QDate &from, const QDate &to) const
{
    QList<Instance> out;
    const time_t start = QDateTime(from, QTime(0, 0), m_zone).toSecsSinceEpoch();
    const time_t end = QDateTime(to, QTime(0, 0), m_zone).toSecsSinceEpoch();
    FetchContext context;
    context.engine = this;
    context.calendarUid = calendarUid;
    context.me = myAddress(calendarUid);
    context.add = [&](ICalComponent *icomp, ICalTime *s, ICalTime *e) {
        Instance i;
        i.calendarUid = calendarUid;
        i.uid = str(i_cal_component_get_uid(icomp));
        i.rid = takeStr(e_cal_util_component_get_recurid_as_string(icomp));
        i.key = keyOf(calendarUid, i.uid, i.rid);
        i.title = str(i_cal_component_get_summary(icomp));
        i.location = str(i_cal_component_get_location(icomp));
        i.allDay = i_cal_time_is_date(s);
        i.start = toDateTime(s);
        i.end = toDateTime(e);
        if (!i.end.isValid() || i.end <= i.start) {
            i.end = i.allDay ? i.start.addDays(1) : i.start.addSecs(i.allDay ? 86400 : 0);
        }
        i.recurring = e_cal_util_component_has_recurrences(icomp) || !i.rid.isEmpty();
        // Free or busy, and whether someone else asked you.
        ICalProperty *transp = i_cal_component_get_first_property(icomp, I_CAL_TRANSP_PROPERTY);
        if (transp) {
            i.busy = i_cal_property_get_transp(transp) != I_CAL_TRANSP_TRANSPARENT;
            g_object_unref(transp);
        }
        if (e_cal_util_component_has_attendee(icomp)) {
            ICalProperty *organizer = i_cal_component_get_first_property(icomp, I_CAL_ORGANIZER_PROPERTY);
            const QString org = organizer ? mailtoless(i_cal_property_get_organizer(organizer)) : QString();
            g_clear_object(&organizer);
            i.invited = !context.me.isEmpty() && !org.isEmpty() && org != context.me;
            for (ICalProperty *p = i_cal_component_get_first_property(icomp, I_CAL_ATTENDEE_PROPERTY); p;
                 p = i_cal_component_get_next_property(icomp, I_CAL_ATTENDEE_PROPERTY)) {
                if (mailtoless(i_cal_property_get_attendee(p)) == context.me) {
                    ICalParameter *param = i_cal_property_get_first_parameter(p, I_CAL_PARTSTAT_PARAMETER);
                    i.myStatus = partstatName(param ? i_cal_parameter_get_partstat(param) : I_CAL_PARTSTAT_NEEDSACTION);
                    g_clear_object(&param);
                }
                g_object_unref(p);
            }
        }
        out << i;
    };
    e_cal_client_generate_instances_sync(client, start, end, nullptr, &addInstance, &context);
    return out;
}

void CalendarEngine::setRange(const QDate &from, const QDate &to)
{
    if (from == m_from && to == m_to) {
        return;
    }
    m_from = from;
    m_to = to;
    ++m_generation;
    Q_EMIT rangeChanged();
    for (const QString &uid : m_clients.keys()) {
        startView(uid);
    }
    reload();
}

void CalendarEngine::reload()
{
    if (!m_from.isValid()) {
        return;
    }
    const int generation = m_generation;
    const QDate from = m_from;
    const QDate to = m_to;
    for (auto it = m_clients.cbegin(); it != m_clients.cend(); ++it) {
        if (!it.value()) {
            continue;
        }
        const QString uid = it.key();
        ECalClient *client = it.value();
        g_object_ref(client);
        auto result = std::make_shared<QList<Instance>>();
        run([this, client, uid, from, to, result]() {
            *result = fetch(client, uid, from, to);
            g_object_unref(client);
        }, [this, uid, generation, result]() {
            if (generation != m_generation || !m_clients.value(uid)) {
                return;
            }
            qCDebug(calendarLog) << "fetched" << result->size() << "events from" << uid;
            m_perCalendar.insert(uid, *result);
            flatten();
        });
    }
}

void CalendarEngine::flatten()
{
    m_instances.clear();
    m_byKey.clear();
    for (const QList<Instance> &list : std::as_const(m_perCalendar)) {
        m_instances << list;
    }
    std::sort(m_instances.begin(), m_instances.end(), [](const Instance &a, const Instance &b) {
        if (a.start != b.start) {
            return a.start < b.start;
        }
        return a.end > b.end;
    });
    for (int i = 0; i < m_instances.size(); ++i) {
        m_byKey.insert(m_instances[i].key, i);
    }
    ++m_revision;
    Q_EMIT eventsChanged();
}

QVariantMap CalendarEngine::row(const Instance &i) const
{
    return {
        {QStringLiteral("key"), i.key},
        {QStringLiteral("calendarUid"), i.calendarUid},
        {QStringLiteral("title"), i.title.isEmpty() ? tr("(No title)") : i.title},
        {QStringLiteral("location"), i.location},
        {QStringLiteral("color"), m_colors.value(i.calendarUid)},
        {QStringLiteral("readOnly"), m_readOnly.contains(i.calendarUid)},
        {QStringLiteral("start"), i.start},
        {QStringLiteral("end"), i.end},
        {QStringLiteral("allDay"), i.allDay},
        {QStringLiteral("recurring"), i.recurring},
        {QStringLiteral("invited"), i.invited},
        {QStringLiteral("busy"), i.busy},
        {QStringLiteral("myStatus"), i.myStatus},
        {QStringLiteral("timeText"), i.allDay ? QString() : Formats::time(i.start)},
        {QStringLiteral("rangeText"), i.allDay ? tr("All day") : tr("%1 to %2").arg(Formats::time(i.start), Formats::time(i.end))},
    };
}

namespace
{
bool fillsDays(const QDateTime &start, const QDateTime &end, bool allDay)
{
    return allDay || start.secsTo(end) >= 86400;
}
}

QVariantList CalendarEngine::timedOn(const QDate &day) const
{
    struct Seg {
        const Instance *i;
        int s;
        int e;
        int column = 0;
        int columns = 1;
    };
    QList<Seg> segs;
    const QDateTime dayStart(day, QTime(0, 0), m_zone);
    const QDateTime dayEnd(day.addDays(1), QTime(0, 0), m_zone);
    for (const Instance &i : m_instances) {
        if (fillsDays(i.start, i.end, i.allDay) || i.end <= dayStart || i.start >= dayEnd) {
            continue;
        }
        const int s = i.start < dayStart ? 0 : int(dayStart.secsTo(i.start) / 60);
        int e = i.end > dayEnd ? 24 * 60 : int(dayStart.secsTo(i.end) / 60);
        // Short events still need room for their title.
        e = std::max(e, s + 15);
        segs << Seg{&i, s, e};
    }
    std::sort(segs.begin(), segs.end(), [](const Seg &a, const Seg &b) {
        return a.s != b.s ? a.s < b.s : a.e > b.e;
    });
    // Overlapping events share the width: each cluster of overlaps is split
    // into as many columns as it needs at its busiest.
    int clusterStart = 0;
    int clusterEnd = -1;
    QList<int> columnEnds;
    auto closeCluster = [&](int upTo) {
        for (int k = clusterStart; k < upTo; ++k) {
            segs[k].columns = int(columnEnds.size());
        }
    };
    for (int k = 0; k < segs.size(); ++k) {
        if (segs[k].s >= clusterEnd) {
            closeCluster(k);
            clusterStart = k;
            columnEnds.clear();
        }
        int c = 0;
        while (c < columnEnds.size() && columnEnds[c] > segs[k].s) {
            ++c;
        }
        if (c == columnEnds.size()) {
            columnEnds << segs[k].e;
        } else {
            columnEnds[c] = segs[k].e;
        }
        segs[k].column = c;
        clusterEnd = std::max(clusterEnd, segs[k].e);
    }
    closeCluster(int(segs.size()));

    QVariantList out;
    for (const Seg &s : segs) {
        QVariantMap m = row(*s.i);
        m.insert(QStringLiteral("startMinute"), s.s);
        m.insert(QStringLiteral("endMinute"), s.e);
        m.insert(QStringLiteral("column"), s.column);
        m.insert(QStringLiteral("columns"), s.columns);
        out << m;
    }
    return out;
}

QVariantList CalendarEngine::allDayBetween(const QDate &from, const QDate &to) const
{
    QVariantList out;
    QList<QList<QPair<int, int>>> rows;
    const int days = int(from.daysTo(to));
    for (const Instance &i : m_instances) {
        if (!fillsDays(i.start, i.end, i.allDay)) {
            continue;
        }
        const QDate first = i.start.date();
        // An all-day end is the start of the day after.
        const QDate last = (i.end.time() == QTime(0, 0) ? i.end.date().addDays(-1) : i.end.date());
        if (last < from || first >= to) {
            continue;
        }
        const int a = std::max(0, int(from.daysTo(first)));
        const int b = std::min(days - 1, int(from.daysTo(last)));
        int r = 0;
        for (; r < rows.size(); ++r) {
            bool free = true;
            for (const auto &span : rows[r]) {
                if (!(b < span.first || a > span.second)) {
                    free = false;
                    break;
                }
            }
            if (free) {
                break;
            }
        }
        if (r == rows.size()) {
            rows.append(QList<QPair<int, int>>());
        }
        rows[r].append({a, b});
        QVariantMap m = row(i);
        m.insert(QStringLiteral("firstColumn"), a);
        m.insert(QStringLiteral("lastColumn"), b);
        m.insert(QStringLiteral("row"), r);
        out << m;
    }
    return out;
}

QVariantList CalendarEngine::eventsOn(const QDate &day) const
{
    QVariantList allDay;
    QVariantList timed;
    const QDateTime dayStart(day, QTime(0, 0), m_zone);
    const QDateTime dayEnd(day.addDays(1), QTime(0, 0), m_zone);
    for (const Instance &i : m_instances) {
        if (i.end <= dayStart || i.start >= dayEnd) {
            continue;
        }
        (fillsDays(i.start, i.end, i.allDay) ? allDay : timed) << row(i);
    }
    return allDay + timed;
}

bool CalendarEngine::busyOn(const QDate &day) const
{
    const QDateTime dayStart(day, QTime(0, 0), m_zone);
    const QDateTime dayEnd(day.addDays(1), QTime(0, 0), m_zone);
    for (const Instance &i : m_instances) {
        if (i.end > dayStart && i.start < dayEnd) {
            return true;
        }
    }
    return false;
}

QVariantMap CalendarEngine::event(const QString &key) const
{
    QString calendarUid, uid, rid;
    if (!splitKey(key, &calendarUid, &uid, &rid)) {
        return {};
    }
    ECalClient *c = client(calendarUid);
    if (!c) {
        return {};
    }
    ICalComponent *icomp = nullptr;
    GError *error = nullptr;
    if (!e_cal_client_get_object_sync(c, uid.toUtf8().constData(), rid.isEmpty() ? nullptr : rid.toUtf8().constData(), &icomp, nullptr, &error)) {
        qCWarning(calendarLog) << "couldn't read event:" << (error ? error->message : "");
        g_clear_error(&error);
        return {};
    }
    ECalComponent *comp = e_cal_component_new_from_icalcomponent(icomp);
    if (!comp) {
        return {};
    }
    QVariantMap m;
    const int index = m_byKey.value(key, -1);
    if (index >= 0) {
        m = row(m_instances[index]);
    } else {
        m.insert(QStringLiteral("key"), key);
        m.insert(QStringLiteral("calendarUid"), calendarUid);
        m.insert(QStringLiteral("color"), m_colors.value(calendarUid));
        m.insert(QStringLiteral("title"), str(i_cal_component_get_summary(icomp)));
        m.insert(QStringLiteral("location"), str(i_cal_component_get_location(icomp)));
        ECalComponentDateTime *dt = e_cal_component_get_dtstart(comp);
        ECalComponentDateTime *de = e_cal_component_get_dtend(comp);
        if (dt) {
            ICalTime *t = e_cal_component_datetime_get_value(dt);
            m.insert(QStringLiteral("allDay"), bool(i_cal_time_is_date(t)));
            m.insert(QStringLiteral("start"), toDateTime(t));
        }
        if (de) {
            m.insert(QStringLiteral("end"), toDateTime(e_cal_component_datetime_get_value(de)));
        }
        e_cal_component_datetime_free(dt);
        e_cal_component_datetime_free(de);
    }

    QStringList notes;
    GSList *descriptions = e_cal_component_get_descriptions(comp);
    for (GSList *l = descriptions; l; l = l->next) {
        notes << str(e_cal_component_text_get_value(static_cast<ECalComponentText *>(l->data)));
    }
    g_slist_free_full(descriptions, reinterpret_cast<GDestroyNotify>(e_cal_component_text_free));
    m.insert(QStringLiteral("description"), notes.join(QLatin1Char('\n')));

    // The first reminder, in minutes before the start.
    int reminder = -1;
    GSList *alarmUids = e_cal_component_get_alarm_uids(comp);
    for (GSList *l = alarmUids; l && reminder < 0; l = l->next) {
        ECalComponentAlarm *alarm = e_cal_component_get_alarm(comp, static_cast<const gchar *>(l->data));
        if (!alarm) {
            continue;
        }
        ECalComponentAlarmTrigger *trigger = e_cal_component_alarm_get_trigger(alarm);
        if (trigger && e_cal_component_alarm_trigger_get_kind(trigger) == E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START) {
            ICalDuration *d = e_cal_component_alarm_trigger_get_duration(trigger);
            if (d) {
                reminder = std::max(0, -int(i_cal_duration_as_int(d)) / 60);
            }
        }
        e_cal_component_alarm_free(alarm);
    }
    g_slist_free_full(alarmUids, g_free);
    m.insert(QStringLiteral("reminder"), reminder);

    QString repeat = QStringLiteral("none");
    if (e_cal_component_has_rrules(comp)) {
        GSList *rules = e_cal_component_get_rrules(comp);
        if (rules) {
            auto *r = static_cast<ICalRecurrence *>(rules->data);
            const QString text = takeStr(i_cal_recurrence_to_string(r));
            switch (i_cal_recurrence_get_freq(r)) {
            case I_CAL_DAILY_RECURRENCE:
                repeat = QStringLiteral("daily");
                break;
            case I_CAL_WEEKLY_RECURRENCE:
                repeat = text.contains(QLatin1String("BYDAY=MO,TU,WE,TH,FR")) ? QStringLiteral("weekdays") : QStringLiteral("weekly");
                break;
            case I_CAL_MONTHLY_RECURRENCE:
                repeat = QStringLiteral("monthly");
                break;
            case I_CAL_YEARLY_RECURRENCE:
                repeat = QStringLiteral("yearly");
                break;
            default:
                repeat = QStringLiteral("other");
            }
        }
        g_slist_free_full(rules, g_object_unref);
    }
    m.insert(QStringLiteral("repeat"), repeat);
    m.insert(QStringLiteral("recurring"), repeat != QLatin1String("none") || !rid.isEmpty());
    m.insert(QStringLiteral("busy"), e_cal_component_get_transparency(comp) != E_CAL_COMPONENT_TRANSP_TRANSPARENT);
    m.insert(QStringLiteral("private"), e_cal_component_get_classification(comp) == E_CAL_COMPONENT_CLASS_PRIVATE
                 || e_cal_component_get_classification(comp) == E_CAL_COMPONENT_CLASS_CONFIDENTIAL);

    const QString me = myAddress(calendarUid);
    QString organizer;
    QString organizerName;
    if (ECalComponentOrganizer *o = e_cal_component_get_organizer(comp)) {
        organizer = mailtoless(e_cal_component_organizer_get_value(o));
        organizerName = str(e_cal_component_organizer_get_cn(o));
        e_cal_component_organizer_free(o);
    }
    QVariantList people;
    int going = 0, maybe = 0;
    QString myStatus;
    GSList *attendees = e_cal_component_get_attendees(comp);
    for (GSList *l = attendees; l; l = l->next) {
        auto *a = static_cast<ECalComponentAttendee *>(l->data);
        const QString email = mailtoless(e_cal_component_attendee_get_value(a));
        const QString status = partstatName(e_cal_component_attendee_get_partstat(a));
        going += status == QLatin1String("going");
        maybe += status == QLatin1String("maybe");
        if (email == me) {
            myStatus = status;
        }
        people << QVariantMap{
            {QStringLiteral("email"), email},
            {QStringLiteral("name"), str(e_cal_component_attendee_get_cn(a))},
            {QStringLiteral("status"), email == organizer ? QStringLiteral("organiser") : status},
            {QStringLiteral("me"), email == me},
        };
    }
    g_slist_free_full(attendees, reinterpret_cast<GDestroyNotify>(e_cal_component_attendee_free));
    // The organiser first, as the board lists them.
    std::stable_sort(people.begin(), people.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("status")) == QLatin1String("organiser") && b.toMap().value(QStringLiteral("status")) != QLatin1String("organiser");
    });
    if (!organizer.isEmpty() && !people.isEmpty()) {
        bool listed = false;
        for (const QVariant &p : std::as_const(people)) {
            listed = listed || p.toMap().value(QStringLiteral("email")) == organizer;
        }
        if (!listed) {
            people.prepend(QVariantMap{{QStringLiteral("email"), organizer}, {QStringLiteral("name"), organizerName}, {QStringLiteral("status"), QStringLiteral("organiser")}, {QStringLiteral("me"), organizer == me}});
        }
    }
    m.insert(QStringLiteral("people"), people);
    m.insert(QStringLiteral("going"), going);
    m.insert(QStringLiteral("maybe"), maybe);
    m.insert(QStringLiteral("myStatus"), myStatus);
    m.insert(QStringLiteral("invited"), !organizer.isEmpty() && !me.isEmpty() && organizer != me && !people.isEmpty());
    m.insert(QStringLiteral("organizer"), organizer);
    m.insert(QStringLiteral("me"), me);
    bool readOnly = e_client_is_readonly(E_CLIENT(c));
    m.insert(QStringLiteral("readOnly"), readOnly);
    g_object_unref(comp);
    return m;
}

namespace
{
ICalTime *icalTime(const QDateTime &t, bool allDay, ICalTimezone *zone)
{
    if (allDay) {
        ICalTime *d = i_cal_time_new_null_time();
        i_cal_time_set_date(d, t.date().year(), t.date().month(), t.date().day());
        i_cal_time_set_is_date(d, TRUE);
        return d;
    }
    return i_cal_time_new_from_timet_with_zone(time_t(t.toSecsSinceEpoch()), FALSE, zone);
}

void setTimes(ECalComponent *comp, const QDateTime &start, const QDateTime &end, bool allDay, ICalTimezone *zone)
{
    const gchar *tzid = allDay ? nullptr : i_cal_timezone_get_tzid(zone);
    ECalComponentDateTime *s = e_cal_component_datetime_new_take(icalTime(start, allDay, zone), g_strdup(tzid));
    // An all-day event ends at the start of the day after its last.
    const QDateTime e = allDay ? QDateTime(end.date().addDays(1), QTime(0, 0), end.timeZone()) : end;
    ECalComponentDateTime *f = e_cal_component_datetime_new_take(icalTime(e, allDay, zone), g_strdup(tzid));
    e_cal_component_set_dtstart(comp, s);
    e_cal_component_set_dtend(comp, f);
    e_cal_component_datetime_free(s);
    e_cal_component_datetime_free(f);
}

void setRepeat(ECalComponent *comp, const QString &repeat)
{
    QString rule;
    if (repeat == QLatin1String("daily")) {
        rule = QStringLiteral("FREQ=DAILY");
    } else if (repeat == QLatin1String("weekdays")) {
        rule = QStringLiteral("FREQ=WEEKLY;BYDAY=MO,TU,WE,TH,FR");
    } else if (repeat == QLatin1String("weekly")) {
        rule = QStringLiteral("FREQ=WEEKLY");
    } else if (repeat == QLatin1String("monthly")) {
        rule = QStringLiteral("FREQ=MONTHLY");
    } else if (repeat == QLatin1String("yearly")) {
        rule = QStringLiteral("FREQ=YEARLY");
    } else if (repeat == QLatin1String("other")) {
        // A rule made elsewhere that this form can't show; left as it is.
        return;
    }
    if (rule.isEmpty()) {
        e_cal_component_set_rrules(comp, nullptr);
        return;
    }
    GSList *rules = g_slist_prepend(nullptr, i_cal_recurrence_new_from_string(rule.toUtf8().constData()));
    e_cal_component_set_rrules(comp, rules);
    g_slist_free_full(rules, g_object_unref);
}

void setReminder(ECalComponent *comp, int minutes, const QString &title)
{
    e_cal_component_remove_all_alarms(comp);
    if (minutes < 0) {
        return;
    }
    ECalComponentAlarm *alarm = e_cal_component_alarm_new();
    e_cal_component_alarm_set_action(alarm, E_CAL_COMPONENT_ALARM_DISPLAY);
    ICalDuration *before = i_cal_duration_new_from_int(-minutes * 60);
    ECalComponentAlarmTrigger *trigger = e_cal_component_alarm_trigger_new_relative(E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START, before);
    e_cal_component_alarm_take_trigger(alarm, trigger);
    g_object_unref(before);
    e_cal_component_alarm_take_description(alarm, e_cal_component_text_new(title.toUtf8().constData(), nullptr));
    e_cal_component_add_alarm(comp, alarm);
    e_cal_component_alarm_free(alarm);
}

void setPeople(ECalComponent *comp, const QStringList &emails, const QString &me, const QVariantList &previous)
{
    if (emails.isEmpty()) {
        e_cal_component_set_attendees(comp, nullptr);
        e_cal_component_set_organizer(comp, nullptr);
        return;
    }
    QHash<QString, QString> statuses;
    for (const QVariant &v : previous) {
        const QVariantMap p = v.toMap();
        statuses.insert(p.value(QStringLiteral("email")).toString(), p.value(QStringLiteral("status")).toString());
    }
    GSList *list = nullptr;
    for (const QString &email : emails) {
        const QString e = email.trimmed().toLower();
        if (e.isEmpty() || e == me) {
            continue;
        }
        const QString was = statuses.value(e);
        const ICalParameterPartstat partstat = was == QLatin1String("going") ? I_CAL_PARTSTAT_ACCEPTED
            : was == QLatin1String("maybe") ? I_CAL_PARTSTAT_TENTATIVE
            : was == QLatin1String("notGoing") ? I_CAL_PARTSTAT_DECLINED : I_CAL_PARTSTAT_NEEDSACTION;
        list = g_slist_append(list, e_cal_component_attendee_new_full(("mailto:" + e).toUtf8().constData(), nullptr, I_CAL_CUTYPE_INDIVIDUAL,
                                                                      I_CAL_ROLE_REQPARTICIPANT, partstat, TRUE, nullptr, nullptr, nullptr, nullptr, nullptr));
    }
    if (!me.isEmpty()) {
        // The organiser goes along too.
        list = g_slist_prepend(list, e_cal_component_attendee_new_full(("mailto:" + me).toUtf8().constData(), nullptr, I_CAL_CUTYPE_INDIVIDUAL,
                                                                       I_CAL_ROLE_CHAIR, I_CAL_PARTSTAT_ACCEPTED, FALSE, nullptr, nullptr, nullptr, nullptr, nullptr));
        ECalComponentOrganizer *organizer = e_cal_component_organizer_new_full(("mailto:" + me).toUtf8().constData(), nullptr, nullptr, nullptr);
        e_cal_component_set_organizer(comp, organizer);
        e_cal_component_organizer_free(organizer);
    }
    e_cal_component_set_attendees(comp, list);
    g_slist_free_full(list, reinterpret_cast<GDestroyNotify>(e_cal_component_attendee_free));
}
}

// data: key (empty for a new event), calendarUid, title, start, end
// (QDateTime; for all-day events the first and last day), allDay, location,
// description, reminder (minutes before, -1 for none), repeat (none, daily,
// weekdays, weekly, monthly, yearly), attendees (addresses), busy, private,
// wholeSeries (for a change to one repeat of a series).
void CalendarEngine::saveEvent(const QVariantMap &data)
{
    const QString key = data.value(QStringLiteral("key")).toString();
    QString calendarUid = data.value(QStringLiteral("calendarUid")).toString();
    QString oldCalendar, uid, rid;
    if (!key.isEmpty()) {
        splitKey(key, &oldCalendar, &uid, &rid);
    }
    if (calendarUid.isEmpty()) {
        calendarUid = oldCalendar.isEmpty() ? defaultCalendar() : oldCalendar;
    }
    ECalClient *target = client(calendarUid);
    if (!target) {
        Q_EMIT notice(tr("That calendar isn't open. Turn it on in the list and try again."));
        return;
    }
    ECalClient *source = oldCalendar.isEmpty() ? nullptr : client(oldCalendar);
    const bool moving = source && oldCalendar != calendarUid;
    const bool wholeSeries = data.value(QStringLiteral("wholeSeries"), true).toBool();
    const QVariantMap previous = key.isEmpty() ? QVariantMap() : event(key);
    const QString me = myAddress(calendarUid);
    ICalTimezone *z = zone();
    g_object_ref(target);
    if (source) {
        g_object_ref(source);
    }
    auto result = std::make_shared<QString>();
    auto error = std::make_shared<QString>();
    run([=]() {
        ECalComponent *comp = nullptr;
        if (!uid.isEmpty() && !moving) {
            ICalComponent *icomp = nullptr;
            const QByteArray r = wholeSeries ? QByteArray() : rid.toUtf8();
            if (e_cal_client_get_object_sync(target, uid.toUtf8().constData(), r.isEmpty() ? nullptr : r.constData(), &icomp, nullptr, nullptr)) {
                comp = e_cal_component_new_from_icalcomponent(icomp);
            }
        }
        const bool creating = !comp;
        if (creating) {
            comp = e_cal_component_new();
            e_cal_component_set_new_vtype(comp, E_CAL_COMPONENT_EVENT);
            gchar *newUid = e_util_generate_uid();
            e_cal_component_set_uid(comp, newUid);
            g_free(newUid);
        }
        const QString title = data.value(QStringLiteral("title")).toString().trimmed();
        ECalComponentText *summary = e_cal_component_text_new(title.toUtf8().constData(), nullptr);
        e_cal_component_set_summary(comp, summary);
        e_cal_component_text_free(summary);
        const QString location = data.value(QStringLiteral("location")).toString().trimmed();
        e_cal_component_set_location(comp, location.isEmpty() ? nullptr : location.toUtf8().constData());
        const QString notes = data.value(QStringLiteral("description")).toString();
        GSList *descriptions = notes.trimmed().isEmpty() ? nullptr : g_slist_prepend(nullptr, e_cal_component_text_new(notes.toUtf8().constData(), nullptr));
        e_cal_component_set_descriptions(comp, descriptions);
        g_slist_free_full(descriptions, reinterpret_cast<GDestroyNotify>(e_cal_component_text_free));

        const bool allDay = data.value(QStringLiteral("allDay")).toBool();
        // Moving one repeat keeps the series' times; changing the series
        // keeps each repeat where the rule puts it.
        if (!(rid.isEmpty() == false && wholeSeries && !creating)) {
            setTimes(comp, data.value(QStringLiteral("start")).toDateTime(), data.value(QStringLiteral("end")).toDateTime(), allDay, z);
        }
        if (rid.isEmpty() || wholeSeries) {
            setRepeat(comp, data.value(QStringLiteral("repeat")).toString());
        }
        setReminder(comp, data.value(QStringLiteral("reminder"), -1).toInt(), title);
        e_cal_component_set_transparency(comp, data.value(QStringLiteral("busy"), true).toBool() ? E_CAL_COMPONENT_TRANSP_OPAQUE : E_CAL_COMPONENT_TRANSP_TRANSPARENT);
        e_cal_component_set_classification(comp, data.value(QStringLiteral("private")).toBool() ? E_CAL_COMPONENT_CLASS_PRIVATE : E_CAL_COMPONENT_CLASS_PUBLIC);
        setPeople(comp, data.value(QStringLiteral("attendees")).toStringList(), me, previous.value(QStringLiteral("people")).toList());
        if (!creating) {
            e_cal_component_commit_sequence(comp);
        }

        GError *err = nullptr;
        if (!allDay) {
            e_cal_client_add_timezone_sync(target, z, nullptr, nullptr);
        }
        ICalComponent *icomp = e_cal_component_get_icalcomponent(comp);
        bool ok;
        gchar *newUid = nullptr;
        if (creating) {
            ok = e_cal_client_create_object_sync(target, icomp, E_CAL_OPERATION_FLAG_NONE, &newUid, nullptr, &err);
        } else {
            ok = e_cal_client_modify_object_sync(target, icomp, (rid.isEmpty() || wholeSeries) ? E_CAL_OBJ_MOD_ALL : E_CAL_OBJ_MOD_THIS,
                                                 E_CAL_OPERATION_FLAG_NONE, nullptr, &err);
        }
        if (ok && moving) {
            e_cal_client_remove_object_sync(source, uid.toUtf8().constData(), nullptr, E_CAL_OBJ_MOD_ALL, E_CAL_OPERATION_FLAG_NONE, nullptr, nullptr);
        }
        if (ok) {
            const QString savedUid = newUid ? str(newUid) : str(e_cal_component_get_uid(comp));
            *result = keyOf(calendarUid, savedUid, (creating || wholeSeries) ? QString() : rid);
        } else {
            *error = str(err ? err->message : nullptr);
        }
        g_free(newUid);
        g_clear_error(&err);
        g_object_unref(comp);
        g_object_unref(target);
        if (source) {
            g_object_unref(source);
        }
    }, [this, result, error]() {
        if (!error->isEmpty()) {
            qCWarning(calendarLog) << "couldn't save event:" << *error;
            Q_EMIT notice(tr("The event wasn't saved. %1").arg(*error));
            return;
        }
        scheduleReload();
        Q_EMIT saved(*result);
    });
}

void CalendarEngine::removeEvent(const QString &key, bool wholeSeries)
{
    QString calendarUid, uid, rid;
    if (!splitKey(key, &calendarUid, &uid, &rid)) {
        return;
    }
    ECalClient *c = client(calendarUid);
    if (!c) {
        return;
    }
    // Gone from the view at once; the service catches up.
    if (m_perCalendar.contains(calendarUid)) {
        auto &list = m_perCalendar[calendarUid];
        list.erase(std::remove_if(list.begin(), list.end(), [&](const Instance &i) {
            return i.uid == uid && (wholeSeries || i.rid == rid);
        }), list.end());
        flatten();
    }
    g_object_ref(c);
    auto error = std::make_shared<QString>();
    run([=]() {
        GError *err = nullptr;
        const bool all = wholeSeries || rid.isEmpty();
        if (!e_cal_client_remove_object_sync(c, uid.toUtf8().constData(), all ? nullptr : rid.toUtf8().constData(),
                                             all ? E_CAL_OBJ_MOD_ALL : E_CAL_OBJ_MOD_THIS, E_CAL_OPERATION_FLAG_NONE, nullptr, &err)) {
            *error = str(err ? err->message : nullptr);
        }
        g_clear_error(&err);
        g_object_unref(c);
    }, [this, error]() {
        if (!error->isEmpty()) {
            Q_EMIT notice(tr("The event wasn't deleted. %1").arg(*error));
        }
        scheduleReload();
    });
}

void CalendarEngine::respond(const QString &key, const QString &status)
{
    QString calendarUid, uid, rid;
    if (!splitKey(key, &calendarUid, &uid, &rid)) {
        return;
    }
    ECalClient *c = client(calendarUid);
    const QString me = myAddress(calendarUid);
    if (!c || me.isEmpty()) {
        return;
    }
    const ICalParameterPartstat partstat = status == QLatin1String("going") ? I_CAL_PARTSTAT_ACCEPTED
        : status == QLatin1String("maybe") ? I_CAL_PARTSTAT_TENTATIVE : I_CAL_PARTSTAT_DECLINED;
    g_object_ref(c);
    auto error = std::make_shared<QString>();
    run([=]() {
        ICalComponent *icomp = nullptr;
        GError *err = nullptr;
        if (!e_cal_client_get_object_sync(c, uid.toUtf8().constData(), nullptr, &icomp, nullptr, &err)) {
            *error = str(err ? err->message : nullptr);
            g_clear_error(&err);
            g_object_unref(c);
            return;
        }
        for (ICalProperty *p = i_cal_component_get_first_property(icomp, I_CAL_ATTENDEE_PROPERTY); p;
             p = i_cal_component_get_next_property(icomp, I_CAL_ATTENDEE_PROPERTY)) {
            if (mailtoless(i_cal_property_get_attendee(p)) == me) {
                i_cal_property_remove_parameter_by_kind(p, I_CAL_PARTSTAT_PARAMETER);
                ICalParameter *param = i_cal_parameter_new_partstat(partstat);
                i_cal_property_take_parameter(p, param);
                i_cal_property_remove_parameter_by_kind(p, I_CAL_RSVP_PARAMETER);
            }
            g_object_unref(p);
        }
        // The calendar's server tells the organiser.
        if (!e_cal_client_modify_object_sync(c, icomp, E_CAL_OBJ_MOD_ALL, E_CAL_OPERATION_FLAG_NONE, nullptr, &err)) {
            *error = str(err ? err->message : nullptr);
        }
        g_clear_error(&err);
        g_object_unref(icomp);
        g_object_unref(c);
    }, [this, error]() {
        if (!error->isEmpty()) {
            Q_EMIT notice(tr("Your answer wasn't saved. %1").arg(*error));
        }
        scheduleReload();
    });
}

void CalendarEngine::moveEvent(const QString &key, const QDateTime &start)
{
    const int index = m_byKey.value(key, -1);
    if (index < 0) {
        return;
    }
    const Instance i = m_instances[index];
    const qint64 length = i.start.secsTo(i.end);
    QVariantMap data = event(key);
    if (data.isEmpty() || data.value(QStringLiteral("readOnly")).toBool()) {
        return;
    }
    QStringList attendees;
    for (const QVariant &p : data.value(QStringLiteral("people")).toList()) {
        attendees << p.toMap().value(QStringLiteral("email")).toString();
    }
    data.insert(QStringLiteral("attendees"), attendees);
    data.insert(QStringLiteral("start"), start);
    data.insert(QStringLiteral("end"), i.allDay ? start.addSecs(length - 86400) : start.addSecs(length));
    // A dragged repeat moves on its own.
    data.insert(QStringLiteral("wholeSeries"), !i.recurring);
    // Drawn where it was dropped straight away.
    if (m_perCalendar.contains(i.calendarUid)) {
        for (Instance &x : m_perCalendar[i.calendarUid]) {
            if (x.key == key) {
                x.start = start;
                x.end = start.addSecs(length);
            }
        }
        flatten();
    }
    saveEvent(data);
}

void CalendarEngine::loadDay(const QDate &day)
{
    QList<QPair<QString, ECalClient *>> clients;
    for (auto it = m_clients.cbegin(); it != m_clients.cend(); ++it) {
        if (it.value()) {
            g_object_ref(it.value());
            clients << qMakePair(it.key(), it.value());
        }
    }
    auto result = std::make_shared<QList<Instance>>();
    run([this, clients, day, result]() {
        for (const auto &[uid, c] : clients) {
            *result << fetch(c, uid, day, day.addDays(1));
            g_object_unref(c);
        }
    }, [this, day, result]() {
        std::sort(result->begin(), result->end(), [](const Instance &a, const Instance &b) {
            return a.start < b.start;
        });
        QVariantList rows;
        for (const Instance &i : std::as_const(*result)) {
            QVariantMap m = row(i);
            const QDateTime dayStart(day, QTime(0, 0), m_zone);
            m.insert(QStringLiteral("startMinute"), i.allDay ? 0 : std::max<qint64>(0, dayStart.secsTo(i.start) / 60));
            m.insert(QStringLiteral("endMinute"), i.allDay ? 24 * 60 : std::min<qint64>(24 * 60, dayStart.secsTo(i.end) / 60));
            rows << m;
        }
        Q_EMIT dayLoaded(day, rows);
    });
}

void CalendarEngine::search(const QString &text)
{
    const QString t = text.trimmed();
    if (t.isEmpty()) {
        Q_EMIT searchDone(text, {});
        return;
    }
    const QDateTime now = QDateTime::currentDateTime();
    const QString sexp = QStringLiteral("(and (occur-in-time-range? (make-time \"%1\") (make-time \"%2\")) (or (contains? \"summary\" %3) (contains? \"location\" %3) (contains? \"description\" %3)))")
                             .arg(isoUtc(now.addYears(-1)), isoUtc(now.addYears(1)), quoted(t));
    QList<QPair<QString, ECalClient *>> clients;
    for (auto it = m_clients.cbegin(); it != m_clients.cend(); ++it) {
        if (it.value()) {
            g_object_ref(it.value());
            clients << qMakePair(it.key(), it.value());
        }
    }
    auto result = std::make_shared<QVariantList>();
    run([this, clients, sexp, result]() {
        for (const auto &[uid, c] : clients) {
            GSList *objects = nullptr;
            if (e_cal_client_get_object_list_sync(c, sexp.toUtf8().constData(), &objects, nullptr, nullptr)) {
                for (GSList *l = objects; l; l = l->next) {
                    auto *icomp = static_cast<ICalComponent *>(l->data);
                    ICalTime *start = i_cal_component_get_dtstart(icomp);
                    const QDateTime when = toDateTime(start);
                    const bool allDay = start && i_cal_time_is_date(start);
                    g_clear_object(&start);
                    const QString rid = takeStr(e_cal_util_component_get_recurid_as_string(icomp));
                    *result << QVariantMap{
                        {QStringLiteral("key"), keyOf(uid, str(i_cal_component_get_uid(icomp)), rid)},
                        {QStringLiteral("title"), str(i_cal_component_get_summary(icomp))},
                        {QStringLiteral("location"), str(i_cal_component_get_location(icomp))},
                        {QStringLiteral("start"), when},
                        {QStringLiteral("allDay"), allDay},
                        {QStringLiteral("calendarUid"), uid},
                    };
                }
                g_slist_free_full(objects, g_object_unref);
            }
            g_object_unref(c);
        }
    }, [this, text, result]() {
        QVariantList rows = *result;
        for (QVariant &v : rows) {
            QVariantMap m = v.toMap();
            m.insert(QStringLiteral("color"), m_colors.value(m.value(QStringLiteral("calendarUid")).toString()));
            v = m;
        }
        std::sort(rows.begin(), rows.end(), [](const QVariant &a, const QVariant &b) {
            return a.toMap().value(QStringLiteral("start")).toDateTime() < b.toMap().value(QStringLiteral("start")).toDateTime();
        });
        Q_EMIT searchDone(text, rows);
    });
}

void CalendarEngine::upcomingWith(const QString &address, int limit)
{
    const QString who = address.trimmed().toLower();
    if (who.isEmpty()) {
        Q_EMIT upcomingFound(address, {});
        return;
    }
    const QDate from = QDate::currentDate();
    const QDate to = from.addMonths(3);
    const QString sexp = QStringLiteral("(and (occur-in-time-range? (make-time \"%1\") (make-time \"%2\")) (or (contains? \"attendee\" %3) (contains? \"organizer\" %3)))")
                             .arg(isoUtc(QDateTime(from, QTime(0, 0), m_zone)), isoUtc(QDateTime(to, QTime(0, 0), m_zone)), quoted(who));
    QList<QPair<QString, ECalClient *>> clients;
    for (auto it = m_clients.cbegin(); it != m_clients.cend(); ++it) {
        if (it.value()) {
            g_object_ref(it.value());
            clients << qMakePair(it.key(), it.value());
        }
    }
    auto result = std::make_shared<QList<Instance>>();
    run([this, clients, sexp, from, to, result]() {
        for (const auto &[uid, c] : clients) {
            QSet<QString> uids;
            GSList *objects = nullptr;
            if (e_cal_client_get_object_list_sync(c, sexp.toUtf8().constData(), &objects, nullptr, nullptr)) {
                for (GSList *l = objects; l; l = l->next) {
                    uids.insert(str(i_cal_component_get_uid(static_cast<ICalComponent *>(l->data))));
                }
                g_slist_free_full(objects, g_object_unref);
            }
            // Expanded, so a weekly meeting shows its next time, not its first.
            if (!uids.isEmpty()) {
                for (const Instance &i : fetch(c, uid, from, to)) {
                    if (uids.contains(i.uid) && i.end > QDateTime::currentDateTime()) {
                        *result << i;
                    }
                }
            }
            g_object_unref(c);
        }
    }, [this, address, limit, result]() {
        std::sort(result->begin(), result->end(), [](const Instance &a, const Instance &b) {
            return a.start < b.start;
        });
        QVariantList rows;
        for (const Instance &i : std::as_const(*result)) {
            if (limit > 0 && rows.size() >= limit) {
                break;
            }
            rows << row(i);
        }
        Q_EMIT upcomingFound(address, rows);
    });
}

void CalendarEngine::setSelected(const QString &calendarUid, bool selected)
{
    if (!m_registry) {
        return;
    }
    ESource *source = e_source_registry_ref_source(m_registry, calendarUid.toUtf8().constData());
    if (!source) {
        return;
    }
    auto *selectable = E_SOURCE_SELECTABLE(e_source_get_extension(source, E_SOURCE_EXTENSION_CALENDAR));
    e_source_selectable_set_selected(selectable, selected);
    e_source_write(source, nullptr, nullptr, nullptr);
    g_object_unref(source);
    // The list changes now; the registry confirms it a moment later.
    for (QVariant &v : m_calendars) {
        QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("uid")) == calendarUid) {
            m.insert(QStringLiteral("selected"), selected);
            v = m;
        }
    }
    Q_EMIT calendarsChanged();
}

QString CalendarEngine::defaultCalendar() const
{
    if (!m_registry) {
        return QStringLiteral("system-calendar");
    }
    ESource *source = e_source_registry_ref_default_calendar(m_registry);
    const QString uid = source ? str(e_source_get_uid(source)) : QStringLiteral("system-calendar");
    g_clear_object(&source);
    return uid;
}

void CalendarEngine::setDefaultCalendar(const QString &uid)
{
    if (!m_registry) {
        return;
    }
    if (ESource *source = e_source_registry_ref_source(m_registry, uid.toUtf8().constData())) {
        e_source_registry_set_default_calendar(m_registry, source);
        g_object_unref(source);
        Q_EMIT calendarsChanged();
    }
}

QString CalendarEngine::timeZoneId() const
{
    return QString::fromUtf8(m_zone.id());
}

QString CalendarEngine::timeZoneName() const
{
    QString city = QString::fromUtf8(m_zone.id()).section(QLatin1Char('/'), -1).replace(QLatin1Char('_'), QLatin1Char(' '));
    return tr("%1 (%2)").arg(city, m_zone.abbreviation(QDateTime::currentDateTime()));
}

void CalendarEngine::setTimeZoneId(const QString &id)
{
    QTimeZone z = id.isEmpty() ? QTimeZone::systemTimeZone() : QTimeZone(id.toUtf8());
    if (!z.isValid() || z == m_zone) {
        return;
    }
    m_zone = z;
    QSettings().setValue(QStringLiteral("calendar/timeZone"), id);
    for (ECalClient *c : std::as_const(m_clients)) {
        if (c) {
            e_cal_client_set_default_timezone(c, zone());
        }
    }
    Q_EMIT timeZoneChanged();
    ++m_generation;
    reload();
}

void CalendarEngine::createSources(const QList<ESource *> &sources, std::function<void(bool, const QString &)> done)
{
    if (!m_registry) {
        done(false, tr("The calendar service isn't running."));
        return;
    }
    ESourceRegistry *registry = m_registry;
    g_object_ref(registry);
    auto error = std::make_shared<QString>();
    run([registry, sources, error]() {
        GList *list = nullptr;
        for (ESource *s : sources) {
            list = g_list_append(list, s);
        }
        GError *err = nullptr;
        if (!e_source_registry_create_sources_sync(registry, list, nullptr, &err)) {
            *error = str(err ? err->message : "");
        }
        g_clear_error(&err);
        g_list_free(list);
        g_object_unref(registry);
    }, [sources, error, done]() {
        for (ESource *s : sources) {
            g_object_unref(s);
        }
        done(error->isEmpty(), *error);
    });
}

namespace
{
ESource *newCalendarSource(const QString &parent, const QString &backend, const QString &name, const QString &color)
{
    ESource *source = e_source_new(nullptr, nullptr, nullptr);
    e_source_set_parent(source, parent.toUtf8().constData());
    e_source_set_display_name(source, name.toUtf8().constData());
    auto *ext = e_source_get_extension(source, E_SOURCE_EXTENSION_CALENDAR);
    e_source_backend_set_backend_name(E_SOURCE_BACKEND(ext), backend.toUtf8().constData());
    e_source_selectable_set_color(E_SOURCE_SELECTABLE(ext), color.toUtf8().constData());
    e_source_selectable_set_selected(E_SOURCE_SELECTABLE(ext), TRUE);
    return source;
}
}

void CalendarEngine::addLocalCalendar(const QString &name, const QString &color)
{
    const QString c = color.isEmpty() ? palette.value(m_calendars.size() % palette.size()) : color;
    createSources({newCalendarSource(QStringLiteral("local-stub"), QStringLiteral("local"), name, c)}, [this](bool ok, const QString &error) {
        if (!ok) {
            Q_EMIT notice(tr("The calendar wasn't made. %1").arg(error));
        }
    });
}

void CalendarEngine::addWebCalendar(const QString &name, const QString &address, const QString &color)
{
    QString a = address.trimmed();
    if (a.startsWith(QLatin1String("webcal://"), Qt::CaseInsensitive)) {
        a = QStringLiteral("https://") + a.mid(9);
    }
    const QUrl url(a);
    if (!url.isValid() || (url.scheme() != QLatin1String("https") && url.scheme() != QLatin1String("http"))) {
        Q_EMIT notice(tr("That isn't a calendar address."));
        return;
    }
    const QString c = color.isEmpty() ? palette.value(m_calendars.size() % palette.size()) : color;
    ESource *source = newCalendarSource(QStringLiteral("webcal-stub"), QStringLiteral("webcal"), name.isEmpty() ? url.host() : name, c);
    auto *webdav = E_SOURCE_WEBDAV(e_source_get_extension(source, E_SOURCE_EXTENSION_WEBDAV_BACKEND));
    GUri *uri = g_uri_parse(url.toString(QUrl::FullyEncoded).toUtf8().constData(), GUriFlags(G_URI_FLAGS_HAS_PASSWORD | G_URI_FLAGS_ENCODED_PATH | G_URI_FLAGS_ENCODED_QUERY | G_URI_FLAGS_ENCODED_FRAGMENT | G_URI_FLAGS_SCHEME_NORMALIZE | G_URI_FLAGS_PARSE_RELAXED), nullptr);
    if (uri) {
        e_source_webdav_set_uri(webdav, uri);
        g_uri_unref(uri);
    }
    auto *refresh = E_SOURCE_REFRESH(e_source_get_extension(source, E_SOURCE_EXTENSION_REFRESH));
    e_source_refresh_set_enabled(refresh, TRUE);
    e_source_refresh_set_interval_minutes(refresh, 12 * 60);
    const QString uid = str(e_source_get_uid(source));
    createSources({source}, [this, uid, a](bool ok, const QString &error) {
        if (!ok) {
            Q_EMIT notice(tr("The calendar wasn't added. %1").arg(error));
        } else if (a == holidaysUrl) {
            QSettings().setValue(QStringLiteral("calendar/holidaysUid"), uid);
            Q_EMIT calendarsChanged();
        }
    });
}

void CalendarEngine::renameCalendar(const QString &uid, const QString &name)
{
    if (ESource *source = m_registry ? e_source_registry_ref_source(m_registry, uid.toUtf8().constData()) : nullptr) {
        e_source_set_display_name(source, name.toUtf8().constData());
        e_source_write(source, nullptr, nullptr, nullptr);
        g_object_unref(source);
    }
}

void CalendarEngine::setColor(const QString &uid, const QString &color)
{
    if (ESource *source = m_registry ? e_source_registry_ref_source(m_registry, uid.toUtf8().constData()) : nullptr) {
        e_source_selectable_set_color(E_SOURCE_SELECTABLE(e_source_get_extension(source, E_SOURCE_EXTENSION_CALENDAR)), color.toUtf8().constData());
        e_source_write(source, nullptr, nullptr, nullptr);
        g_object_unref(source);
    }
}

void CalendarEngine::removeCalendar(const QString &uid)
{
    if (ESource *source = m_registry ? e_source_registry_ref_source(m_registry, uid.toUtf8().constData()) : nullptr) {
        if (e_source_get_removable(source)) {
            e_source_remove(source, nullptr, nullptr, nullptr);
        } else {
            // Built in (Birthdays, an account's calendar): hidden instead.
            e_source_set_enabled(source, FALSE);
            e_source_write(source, nullptr, nullptr, nullptr);
        }
        g_object_unref(source);
    }
}

void CalendarEngine::removeAccount(const QString &uid)
{
    removeCalendar(uid);
}

void CalendarEngine::refresh()
{
    for (ECalClient *c : std::as_const(m_clients)) {
        if (c) {
            startRefresh(E_CLIENT(c));
        }
    }
}

void CalendarEngine::startRefresh(EClient *client)
{
    if (m_syncing++ == 0) {
        m_syncHold.start();
        Q_EMIT syncingChanged();
    }
    e_client_refresh(client, m_cancel, &CalendarEngine::onRefreshed, nullptr);
}

void CalendarEngine::onRefreshed(GObject *object, GAsyncResult *result, gpointer)
{
    GError *error = nullptr;
    e_client_refresh_finish(E_CLIENT(object), result, &error);
    const bool cancelled = g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
    g_clear_error(&error);
    if (cancelled) {
        return;
    }
    CalendarEngine *self = instance();
    if (--self->m_syncing == 0) {
        Q_EMIT self->syncingChanged();
    }
}

namespace
{
ESource *refSource(gpointer registry, const gchar *uid)
{
    return e_source_registry_ref_source(static_cast<ESourceRegistry *>(registry), uid);
}

ESource *newCollection(const QString &name, const QString &backend, const QString &identity)
{
    ESource *source = e_source_new(nullptr, nullptr, nullptr);
    e_source_set_display_name(source, name.toUtf8().constData());
    auto *collection = E_SOURCE_COLLECTION(e_source_get_extension(source, E_SOURCE_EXTENSION_COLLECTION));
    e_source_backend_set_backend_name(E_SOURCE_BACKEND(collection), backend.toUtf8().constData());
    e_source_collection_set_identity(collection, identity.toUtf8().constData());
    e_source_collection_set_calendar_enabled(collection, TRUE);
    e_source_collection_set_contacts_enabled(collection, TRUE);
    // Mail reads the account's mail itself.
    e_source_collection_set_mail_enabled(collection, FALSE);
    return source;
}
}

void CalendarEngine::addGoogleAccount(const QString &email, const QString &refreshToken)
{
    if (!m_registry) {
        Q_EMIT accountFailed(tr("The calendar service isn't running."));
        return;
    }
    ESource *source = newCollection(email, QStringLiteral("google"), email);
    auto *auth = E_SOURCE_AUTHENTICATION(e_source_get_extension(source, E_SOURCE_EXTENSION_AUTHENTICATION));
    e_source_authentication_set_user(auth, email.toUtf8().constData());
    e_source_authentication_set_method(auth, "Google");
    const QString uid = str(e_source_get_uid(source));
    ESourceRegistry *registry = m_registry;
    g_object_ref(registry);
    g_object_ref(source);
    auto error = std::make_shared<QString>();
    // The token goes where the service's Google module looks for it.
    run([registry, source, refreshToken, error]() {
        EOAuth2Services *services = e_oauth2_services_new();
        EOAuth2Service *service = e_oauth2_services_find(services, source);
        GError *err = nullptr;
        if (!service) {
            *error = CalendarEngine::tr("no Google sign-in in the calendar service");
        } else if (!e_oauth2_service_refresh_and_store_token_sync(service, source, refreshToken.toUtf8().constData(), &refSource, registry, nullptr, &err)) {
            *error = str(err ? err->message : "");
        }
        g_clear_error(&err);
        g_clear_object(&service);
        g_object_unref(services);
        g_object_unref(registry);
    }, [this, source, uid, error]() {
        if (!error->isEmpty()) {
            g_object_unref(source);
            qCWarning(calendarLog) << "Google token for calendar not stored:" << *error;
            Q_EMIT accountFailed(tr("Google didn't let Calendar in. %1").arg(*error));
            return;
        }
        createSources({source}, [this, uid](bool ok, const QString &e) {
            if (ok) {
                Q_EMIT accountAdded(uid);
            } else {
                Q_EMIT accountFailed(tr("The account wasn't added. %1").arg(e));
            }
        });
    });
}

// The Microsoft 365 module's sign-in service and settings live in the
// calendar service's own module, loaded here too so Mail can set up an
// account for it and hand it the token. Its other types (the account
// backend) are only registered, never run, in Mail.
static bool loadMicrosoftModule()
{
    static const bool loaded = []() {
        const char *path = "/usr/lib/evolution-data-server/registry-modules/module-microsoft365-backend.so";
        if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
            return false;
        }
        // Kept for the life of Mail: its types stay registered.
        return e_module_load_file(path) != nullptr;
    }();
    return loaded;
}

void CalendarEngine::addMicrosoftAccount(const QString &email, const QString &refreshToken)
{
    if (!m_registry) {
        Q_EMIT accountFailed(tr("The calendar service isn't running."));
        return;
    }
    if (!loadMicrosoftModule()) {
        Q_EMIT accountFailed(tr("Microsoft calendars aren't available on this PC. Install the latest updates and try again."));
        return;
    }
    // evolution-data-server keeps one list of sign-in services, filled from
    // the loaded modules when it is first made. Anything in Mail may have
    // made it before the Microsoft module was loaded, so the module's
    // service is added to it here if it isn't there yet. Making the service
    // also registers its settings ("Microsoft365 Backend") with the source.
    {
        EOAuth2Services *services = e_oauth2_services_new();
        bool listed = false;
        GSList *list = e_oauth2_services_list(services);
        for (GSList *l = list; l; l = l->next) {
            listed = listed || g_strcmp0(e_oauth2_service_get_name(E_OAUTH2_SERVICE(l->data)), "Microsoft365") == 0;
        }
        g_slist_free_full(list, g_object_unref);
        const GType type = g_type_from_name("EOAuth2ServiceMicrosoft365");
        if (!listed && type) {
            // It adds itself to the list, which keeps it.
            g_object_unref(g_object_new(type, "extensible", services, nullptr));
        }
        g_object_unref(services);
    }
    // One account per address: an earlier try at it goes, so adding it
    // again starts clean instead of leaving a second copy beside it.
    GList *existing = e_source_registry_list_sources(m_registry, E_SOURCE_EXTENSION_COLLECTION);
    for (GList *l = existing; l; l = l->next) {
        auto *s = E_SOURCE(l->data);
        auto *c = E_SOURCE_COLLECTION(e_source_get_extension(s, E_SOURCE_EXTENSION_COLLECTION));
        if (g_strcmp0(e_source_backend_get_backend_name(E_SOURCE_BACKEND(c)), "microsoft365") == 0
            && str(e_source_collection_get_identity(c)).compare(email, Qt::CaseInsensitive) == 0 && e_source_get_removable(s)) {
            e_source_remove(s, nullptr, nullptr, nullptr);
        }
    }
    g_list_free_full(existing, g_object_unref);

    ESource *source = newCollection(email, QStringLiteral("microsoft365"), email);
    auto *auth = E_SOURCE_AUTHENTICATION(e_source_get_extension(source, E_SOURCE_EXTENSION_AUTHENTICATION));
    e_source_authentication_set_user(auth, email.toUtf8().constData());
    // What Evolution's own Microsoft 365 setup writes.
    e_source_authentication_set_host(auth, "graph.microsoft.com");
    e_source_authentication_set_port(auth, 442);
    e_source_authentication_set_method(auth, "Microsoft365");
    gpointer camel = e_source_get_extension(source, e_source_camel_get_extension_name("microsoft365"));
    CamelSettings *settings = camel ? e_source_camel_get_settings(E_SOURCE_CAMEL(camel)) : nullptr;
    if (!settings || !g_object_class_find_property(G_OBJECT_GET_CLASS(settings), "oauth2-client-id")) {
        g_object_unref(source);
        Q_EMIT accountFailed(tr("Microsoft calendars aren't available on this PC. Install the latest updates and try again."));
        return;
    }
    // Wild OS Mail's app, so Microsoft's page and the person's account list
    // name Wild OS Mail, and one sign-in covers mail and calendars.
    g_object_set(settings,
                 "email", email.toUtf8().constData(),
                 "user", email.toUtf8().constData(),
                 "override-oauth2", TRUE,
                 "oauth2-client-id", MicrosoftSignIn::clientId().toUtf8().constData(),
                 "oauth2-tenant", MicrosoftSignIn::tenant().toUtf8().constData(),
                 "oauth2-redirect-uri", MicrosoftSignIn::redirectUri().toUtf8().constData(),
                 nullptr);
    const QString uid = str(e_source_get_uid(source));
    ESourceRegistry *registry = m_registry;
    g_object_ref(registry);
    g_object_ref(source);
    auto error = std::make_shared<QString>();
    run([registry, source, refreshToken, error]() {
        EOAuth2Services *services = e_oauth2_services_new();
        EOAuth2Service *service = e_oauth2_services_find(services, source);
        GError *err = nullptr;
        if (!service) {
            *error = CalendarEngine::tr("no Microsoft sign-in in the calendar service");
        } else if (!e_oauth2_service_refresh_and_store_token_sync(service, source, refreshToken.toUtf8().constData(), &refSource, registry, nullptr, &err)) {
            *error = str(err ? err->message : "");
        }
        g_clear_error(&err);
        g_clear_object(&service);
        g_object_unref(services);
        g_object_unref(registry);
    }, [this, source, uid, error]() {
        if (!error->isEmpty()) {
            g_object_unref(source);
            qCWarning(calendarLog) << "Microsoft token for calendar not stored:" << *error;
            Q_EMIT accountFailed(tr("Microsoft didn't let Calendar in. %1").arg(*error));
            return;
        }
        createSources({source}, [this, uid](bool ok, const QString &e) {
            if (ok) {
                Q_EMIT accountAdded(uid);
            } else {
                Q_EMIT accountFailed(tr("The account wasn't added. %1").arg(e));
            }
        });
    });
}

QVariantMap CalendarEngine::accountParts(const QString &email) const
{
    QVariantMap parts{{QStringLiteral("exists"), false}, {QStringLiteral("calendar"), false}, {QStringLiteral("contacts"), false}};
    if (!m_registry) {
        return parts;
    }
    GList *list = e_source_registry_list_sources(m_registry, E_SOURCE_EXTENSION_COLLECTION);
    for (GList *l = list; l; l = l->next) {
        auto *c = E_SOURCE_COLLECTION(e_source_get_extension(E_SOURCE(l->data), E_SOURCE_EXTENSION_COLLECTION));
        if (str(e_source_collection_get_identity(c)).compare(email, Qt::CaseInsensitive) == 0) {
            parts = {{QStringLiteral("exists"), true},
                     {QStringLiteral("calendar"), bool(e_source_collection_get_calendar_enabled(c))},
                     {QStringLiteral("contacts"), bool(e_source_collection_get_contacts_enabled(c))}};
            break;
        }
    }
    g_list_free_full(list, g_object_unref);
    return parts;
}

void CalendarEngine::setAccountPart(const QString &email, const QString &part, bool on)
{
    if (!m_registry) {
        return;
    }
    GList *list = e_source_registry_list_sources(m_registry, E_SOURCE_EXTENSION_COLLECTION);
    for (GList *l = list; l; l = l->next) {
        auto *source = E_SOURCE(l->data);
        auto *c = E_SOURCE_COLLECTION(e_source_get_extension(source, E_SOURCE_EXTENSION_COLLECTION));
        if (str(e_source_collection_get_identity(c)).compare(email, Qt::CaseInsensitive) != 0) {
            continue;
        }
        if (part == QLatin1String("calendar")) {
            e_source_collection_set_calendar_enabled(c, on);
        } else {
            e_source_collection_set_contacts_enabled(c, on);
        }
        e_source_write(source, nullptr, nullptr, nullptr);
    }
    g_list_free_full(list, g_object_unref);
}

void CalendarEngine::addCalDavAccount(const QString &name, const QString &address, const QString &user, const QString &password)
{
    const QUrl url = QUrl::fromUserInput(address.trimmed());
    if (!url.isValid() || url.host().isEmpty()) {
        Q_EMIT accountFailed(tr("That isn't a server address."));
        return;
    }
    ESource *source = newCollection(name.isEmpty() ? url.host() : name, QStringLiteral("webdav"), user);
    auto *collection = E_SOURCE_COLLECTION(e_source_get_extension(source, E_SOURCE_EXTENSION_COLLECTION));
    e_source_collection_set_calendar_url(collection, url.toString(QUrl::FullyEncoded).toUtf8().constData());
    // iCloud keeps contacts on a server of their own; People finds them
    // there with the same app-specific password.
    const QString contacts = url.host().compare(QLatin1String("caldav.icloud.com"), Qt::CaseInsensitive) == 0
        ? QStringLiteral("https://contacts.icloud.com/")
        : url.toString(QUrl::FullyEncoded);
    e_source_collection_set_contacts_url(collection, contacts.toUtf8().constData());
    auto *auth = E_SOURCE_AUTHENTICATION(e_source_get_extension(source, E_SOURCE_EXTENSION_AUTHENTICATION));
    e_source_authentication_set_user(auth, user.toUtf8().constData());
    e_source_authentication_set_host(auth, url.host().toUtf8().constData());
    e_source_authentication_set_port(auth, guint16(url.port(url.scheme() == QLatin1String("http") ? 80 : 443)));
    e_source_authentication_set_method(auth, "plain/password");
    const QString uid = str(e_source_get_uid(source));
    g_object_ref(source);
    createSources({source}, [this, source, uid, user, password](bool ok, const QString &e) {
        if (!ok) {
            g_object_unref(source);
            Q_EMIT accountFailed(tr("The account wasn't added. %1").arg(e));
            return;
        }
        // Kept by KWallet's Secret Service, where the calendar service
        // looks for it.
        auto error = std::make_shared<QString>();
        run([source, password, error]() {
            GError *err = nullptr;
            if (!e_source_store_password_sync(source, password.toUtf8().constData(), TRUE, nullptr, &err)) {
                *error = str(err ? err->message : "");
            }
            g_clear_error(&err);
        }, [this, source, uid, user, password, error]() {
            if (!error->isEmpty()) {
                Q_EMIT accountFailed(tr("The password wasn't saved. %1").arg(*error));
            } else {
                // The registry tried the account once when it was created,
                // before the password was stored, and now waits for a sign-in
                // that only Evolution's own prompt would give. Hand it the
                // credentials so it looks for the calendars now. This has to
                // be the registry's copy of the source: the one built here
                // has no D-Bus object behind it, so authenticating it does
                // nothing.
                if (ESource *live = m_registry ? e_source_registry_ref_source(m_registry, uid.toUtf8().constData()) : nullptr) {
                    ENamedParameters *credentials = e_named_parameters_new();
                    e_named_parameters_set(credentials, E_SOURCE_CREDENTIAL_USERNAME, user.toUtf8().constData());
                    e_named_parameters_set(credentials, E_SOURCE_CREDENTIAL_PASSWORD, password.toUtf8().constData());
                    e_source_invoke_authenticate(live, credentials, nullptr, nullptr, nullptr);
                    e_named_parameters_free(credentials);
                    g_object_unref(live);
                } else {
                    qCWarning(calendarLog) << "New calendar account not in the registry yet:" << uid;
                }
                Q_EMIT accountAdded(uid);
            }
            g_object_unref(source);
        });
    });
}

bool CalendarEngine::hasHolidays() const
{
    const QString uid = QSettings().value(QStringLiteral("calendar/holidaysUid")).toString();
    for (const QVariant &v : m_calendars) {
        if (v.toMap().value(QStringLiteral("uid")) == uid) {
            return true;
        }
    }
    return false;
}

void CalendarEngine::setHolidays(bool on)
{
    if (on == hasHolidays()) {
        return;
    }
    if (on) {
        addWebCalendar(tr("UK holidays"), holidaysUrl, QStringLiteral("#9aa3b5"));
    } else {
        removeCalendar(QSettings().value(QStringLiteral("calendar/holidaysUid")).toString());
    }
}

bool CalendarEngine::hasBirthdays() const
{
    for (const QVariant &v : m_calendars) {
        if (v.toMap().value(QStringLiteral("uid")) == QLatin1String("birthdays")) {
            return true;
        }
    }
    return false;
}

void CalendarEngine::setBirthdays(bool on)
{
    if (ESource *source = m_registry ? e_source_registry_ref_source(m_registry, "birthdays") : nullptr) {
        e_source_set_enabled(source, on);
        e_source_write(source, nullptr, nullptr, nullptr);
        g_object_unref(source);
    }
}

namespace
{
// The event in an invitation's VCALENDAR (or the VEVENT itself).
ICalComponent *eventOf(ICalComponent *cal)
{
    if (i_cal_component_isa(cal) == I_CAL_VEVENT_COMPONENT) {
        return I_CAL_COMPONENT(g_object_ref(cal));
    }
    return i_cal_component_get_first_component(cal, I_CAL_VEVENT_COMPONENT);
}

// A start or end in an invitation, with its time zone found in the
// invitation's own VTIMEZONE, else the built-in one of that name.
QDateTime inviteTime(ICalComponent *cal, ICalComponent *event, ICalPropertyKind kind, bool *allDay)
{
    ICalProperty *prop = i_cal_component_get_first_property(event, kind);
    if (!prop) {
        return {};
    }
    ICalTime *t = kind == I_CAL_DTSTART_PROPERTY ? i_cal_property_get_dtstart(prop) : i_cal_property_get_dtend(prop);
    QDateTime out;
    if (t) {
        if (i_cal_time_is_date(t)) {
            if (allDay) {
                *allDay = true;
            }
            out = QDateTime(QDate(i_cal_time_get_year(t), i_cal_time_get_month(t), i_cal_time_get_day(t)), QTime(0, 0));
        } else {
            ICalTimezone *zone = nullptr;
            ICalTimezone *owned = nullptr;
            if (i_cal_time_is_utc(t)) {
                zone = i_cal_timezone_get_utc_timezone();
            } else if (ICalParameter *tzid = i_cal_property_get_first_parameter(prop, I_CAL_TZID_PARAMETER)) {
                const gchar *id = i_cal_parameter_get_tzid(tzid);
                owned = i_cal_component_get_timezone(cal, id);
                zone = owned ? owned : i_cal_timezone_get_builtin_timezone(id);
                g_object_unref(tzid);
            }
            if (zone) {
                out = QDateTime::fromSecsSinceEpoch(qint64(i_cal_time_as_timet_with_zone(t, zone)));
            } else {
                // Floating: the same clock time wherever you are.
                out = QDateTime(QDate(i_cal_time_get_year(t), i_cal_time_get_month(t), i_cal_time_get_day(t)),
                                QTime(i_cal_time_get_hour(t), i_cal_time_get_minute(t)));
            }
            g_clear_object(&owned);
        }
        g_object_unref(t);
    }
    g_object_unref(prop);
    return out;
}

QString methodName(ICalComponent *cal)
{
    switch (i_cal_component_get_method(cal)) {
    case I_CAL_METHOD_REQUEST:
        return QStringLiteral("request");
    case I_CAL_METHOD_CANCEL:
        return QStringLiteral("cancel");
    case I_CAL_METHOD_REPLY:
        return QStringLiteral("reply");
    case I_CAL_METHOD_COUNTER:
        return QStringLiteral("counter");
    default:
        return QStringLiteral("publish");
    }
}

void setMyStatus(ICalComponent *event, const QString &me, ICalParameterPartstat partstat, bool addIfMissing)
{
    bool found = false;
    for (ICalProperty *p = i_cal_component_get_first_property(event, I_CAL_ATTENDEE_PROPERTY); p;
         p = i_cal_component_get_next_property(event, I_CAL_ATTENDEE_PROPERTY)) {
        if (mailtoless(i_cal_property_get_attendee(p)) == me) {
            i_cal_property_remove_parameter_by_kind(p, I_CAL_PARTSTAT_PARAMETER);
            i_cal_property_take_parameter(p, i_cal_parameter_new_partstat(partstat));
            i_cal_property_remove_parameter_by_kind(p, I_CAL_RSVP_PARAMETER);
            found = true;
        }
        g_object_unref(p);
    }
    if (!found && addIfMissing) {
        ICalProperty *p = i_cal_property_new_attendee(("mailto:" + me).toUtf8().constData());
        i_cal_property_take_parameter(p, i_cal_parameter_new_partstat(partstat));
        i_cal_component_take_property(event, p);
    }
}
}

QVariantMap CalendarEngine::describeInvitation(const QString &ics, const QString &me) const
{
    ICalComponent *cal = ics.isEmpty() ? nullptr : i_cal_component_new_from_string(ics.toUtf8().constData());
    if (!cal) {
        return {};
    }
    ICalComponent *event = eventOf(cal);
    if (!event) {
        g_object_unref(cal);
        return {};
    }
    const QString mine = me.trimmed().toLower();
    bool allDay = false;
    const QDateTime start = inviteTime(cal, event, I_CAL_DTSTART_PROPERTY, &allDay);
    QDateTime end = inviteTime(cal, event, I_CAL_DTEND_PROPERTY, nullptr);
    if (!end.isValid()) {
        end = allDay ? start.addDays(1) : start;
    }
    QString organizer, organizerName;
    if (ICalProperty *o = i_cal_component_get_first_property(event, I_CAL_ORGANIZER_PROPERTY)) {
        organizer = mailtoless(i_cal_property_get_organizer(o));
        if (ICalParameter *cn = i_cal_property_get_first_parameter(o, I_CAL_CN_PARAMETER)) {
            organizerName = str(i_cal_parameter_get_cn(cn));
            g_object_unref(cn);
        }
        g_object_unref(o);
    }
    int people = 0, accepted = 0, waiting = 0;
    QString myStatus;
    for (ICalProperty *p = i_cal_component_get_first_property(event, I_CAL_ATTENDEE_PROPERTY); p;
         p = i_cal_component_get_next_property(event, I_CAL_ATTENDEE_PROPERTY)) {
        ++people;
        ICalParameter *param = i_cal_property_get_first_parameter(p, I_CAL_PARTSTAT_PARAMETER);
        const QString status = partstatName(param ? i_cal_parameter_get_partstat(param) : I_CAL_PARTSTAT_NEEDSACTION);
        g_clear_object(&param);
        accepted += status == QLatin1String("going");
        waiting += status == QLatin1String("waiting");
        if (mailtoless(i_cal_property_get_attendee(p)) == mine) {
            myStatus = status;
        }
        g_object_unref(p);
    }
    QVariantMap m{
        {QStringLiteral("method"), methodName(cal)},
        {QStringLiteral("uid"), str(i_cal_component_get_uid(event))},
        {QStringLiteral("sequence"), i_cal_component_get_sequence(event)},
        {QStringLiteral("title"), str(i_cal_component_get_summary(event))},
        {QStringLiteral("location"), str(i_cal_component_get_location(event))},
        {QStringLiteral("description"), str(i_cal_component_get_description(event))},
        {QStringLiteral("start"), start},
        // The last day, for an all-day event.
        {QStringLiteral("end"), allDay ? end.addDays(-1) : end},
        {QStringLiteral("allDay"), allDay},
        {QStringLiteral("recurring"), bool(e_cal_util_component_has_recurrences(event))},
        {QStringLiteral("organizer"), organizer},
        {QStringLiteral("organizerName"), organizerName},
        {QStringLiteral("people"), people},
        {QStringLiteral("accepted"), accepted},
        {QStringLiteral("waiting"), waiting},
        {QStringLiteral("myStatus"), myStatus},
    };
    g_object_unref(event);
    g_object_unref(cal);
    return m;
}

void CalendarEngine::answerInvitation(const QString &ics, const QString &status, const QString &me)
{
    if (!m_registry) {
        Q_EMIT notice(tr("The calendar service is still starting. Try again in a moment."));
        return;
    }
    const QString mine = me.trimmed().toLower();
    const ICalParameterPartstat partstat = status == QLatin1String("going") ? I_CAL_PARTSTAT_ACCEPTED
        : status == QLatin1String("maybe") ? I_CAL_PARTSTAT_TENTATIVE : I_CAL_PARTSTAT_DECLINED;
    ESourceRegistry *registry = m_registry;
    g_object_ref(registry);
    const QString defaultUid = defaultCalendar();
    struct Outcome {
        QString uid, reply, organizer, title, error;
    };
    auto outcome = std::make_shared<Outcome>();
    run([=]() {
        ICalComponent *cal = i_cal_component_new_from_string(ics.toUtf8().constData());
        ICalComponent *event = cal ? eventOf(cal) : nullptr;
        if (!event) {
            outcome->error = tr("The invitation can't be read.");
            g_clear_object(&cal);
            g_object_unref(registry);
            return;
        }
        const QString uid = str(i_cal_component_get_uid(event));
        outcome->uid = uid;
        outcome->title = str(i_cal_component_get_summary(event));
        if (ICalProperty *o = i_cal_component_get_first_property(event, I_CAL_ORGANIZER_PROPERTY)) {
            outcome->organizer = mailtoless(i_cal_property_get_organizer(o));
            g_object_unref(o);
        }

        // Where a calendar already has it: a Google or CalDAV account puts
        // invitations in the calendar itself, and its server answers for you.
        bool handledByServer = false;
        bool found = false;
        GList *sources = e_source_registry_list_enabled(registry, E_SOURCE_EXTENSION_CALENDAR);
        for (GList *l = sources; l && !found; l = l->next) {
            auto *source = E_SOURCE(l->data);
            const QString backend = backendOf(source);
            if (backend == QLatin1String("webcal") || backend == QLatin1String("contacts") || backend == QLatin1String("weather")) {
                continue;
            }
            EClient *client = e_cal_client_connect_sync(source, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, 15, nullptr, nullptr);
            if (!client) {
                continue;
            }
            ICalComponent *existing = nullptr;
            if (e_cal_client_get_object_sync(E_CAL_CLIENT(client), uid.toUtf8().constData(), nullptr, &existing, nullptr, nullptr) && existing) {
                found = true;
                const bool server = backend != QLatin1String("local");
                if (partstat == I_CAL_PARTSTAT_DECLINED && !server) {
                    e_cal_client_remove_object_sync(E_CAL_CLIENT(client), uid.toUtf8().constData(), nullptr, E_CAL_OBJ_MOD_ALL, E_CAL_OPERATION_FLAG_NONE, nullptr, nullptr);
                } else {
                    setMyStatus(existing, mine, partstat, false);
                    GError *err = nullptr;
                    if (!e_cal_client_modify_object_sync(E_CAL_CLIENT(client), existing, E_CAL_OBJ_MOD_ALL, E_CAL_OPERATION_FLAG_NONE, nullptr, &err)) {
                        outcome->error = str(err ? err->message : "");
                    }
                    g_clear_error(&err);
                }
                handledByServer = server;
                g_object_unref(existing);
            }
            g_object_unref(client);
        }
        g_list_free_full(sources, g_object_unref);

        // Not in any calendar yet: going or maybe adds it to the default one.
        if (!found && partstat != I_CAL_PARTSTAT_DECLINED) {
            if (ESource *source = e_source_registry_ref_source(registry, defaultUid.toUtf8().constData())) {
                if (EClient *client = e_cal_client_connect_sync(source, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, 15, nullptr, nullptr)) {
                    // The invitation's own time zones go in with it.
                    for (ICalComponent *tz = i_cal_component_get_first_component(cal, I_CAL_VTIMEZONE_COMPONENT); tz;
                         tz = i_cal_component_get_next_component(cal, I_CAL_VTIMEZONE_COMPONENT)) {
                        // A copy: the zone takes over the component it's given.
                        ICalComponent *own = i_cal_component_clone(tz);
                        ICalTimezone *zone = i_cal_timezone_new();
                        if (i_cal_timezone_set_component(zone, own)) {
                            e_cal_client_add_timezone_sync(E_CAL_CLIENT(client), zone, nullptr, nullptr);
                        }
                        g_object_unref(own);
                        g_object_unref(zone);
                        g_object_unref(tz);
                    }
                    ICalComponent *copy = i_cal_component_clone(event);
                    setMyStatus(copy, mine, partstat, true);
                    GError *err = nullptr;
                    if (!e_cal_client_create_object_sync(E_CAL_CLIENT(client), copy, E_CAL_OPERATION_FLAG_NONE, nullptr, nullptr, &err)) {
                        outcome->error = str(err ? err->message : "");
                    }
                    g_clear_error(&err);
                    g_object_unref(copy);
                    g_object_unref(client);
                }
                g_object_unref(source);
            }
        }

        // The organiser hears by mail unless a server told them.
        if (!handledByServer && !outcome->organizer.isEmpty()) {
            ICalComponent *reply = i_cal_component_new_vcalendar();
            i_cal_component_take_property(reply, i_cal_property_new_version("2.0"));
            i_cal_component_take_property(reply, i_cal_property_new_prodid("-//Wild OS//Calendar//EN"));
            i_cal_component_set_method(reply, I_CAL_METHOD_REPLY);
            for (ICalComponent *tz = i_cal_component_get_first_component(cal, I_CAL_VTIMEZONE_COMPONENT); tz;
                 tz = i_cal_component_get_next_component(cal, I_CAL_VTIMEZONE_COMPONENT)) {
                i_cal_component_take_component(reply, i_cal_component_clone(tz));
                g_object_unref(tz);
            }
            ICalComponent *answer = i_cal_component_new_vevent();
            i_cal_component_set_uid(answer, uid.toUtf8().constData());
            i_cal_component_set_sequence(answer, i_cal_component_get_sequence(event));
            ICalTime *now = i_cal_time_new_current_with_zone(i_cal_timezone_get_utc_timezone());
            i_cal_component_set_dtstamp(answer, now);
            g_object_unref(now);
            for (ICalPropertyKind kind : {I_CAL_DTSTART_PROPERTY, I_CAL_DTEND_PROPERTY, I_CAL_SUMMARY_PROPERTY, I_CAL_ORGANIZER_PROPERTY, I_CAL_RECURRENCEID_PROPERTY}) {
                if (ICalProperty *p = i_cal_component_get_first_property(event, kind)) {
                    i_cal_component_take_property(answer, i_cal_property_clone(p));
                    g_object_unref(p);
                }
            }
            ICalProperty *attendee = i_cal_property_new_attendee(("mailto:" + mine).toUtf8().constData());
            i_cal_property_take_parameter(attendee, i_cal_parameter_new_partstat(partstat));
            i_cal_component_take_property(answer, attendee);
            i_cal_component_take_component(reply, answer);
            outcome->reply = takeStr(i_cal_component_as_ical_string(reply));
            g_object_unref(reply);
        }
        g_object_unref(event);
        g_object_unref(cal);
        g_object_unref(registry);
    }, [this, outcome, status]() {
        if (!outcome->error.isEmpty()) {
            Q_EMIT notice(tr("Your answer wasn't saved. %1").arg(outcome->error));
            return;
        }
        scheduleReload();
        Q_EMIT invitationAnswered(outcome->uid, status, outcome->reply, outcome->organizer, outcome->title);
    });
}

void CalendarEngine::removeInvitation(const QString &ics)
{
    if (!m_registry) {
        return;
    }
    ICalComponent *cal = i_cal_component_new_from_string(ics.toUtf8().constData());
    ICalComponent *event = cal ? eventOf(cal) : nullptr;
    const QString uid = event ? str(i_cal_component_get_uid(event)) : QString();
    g_clear_object(&event);
    g_clear_object(&cal);
    if (uid.isEmpty()) {
        return;
    }
    ESourceRegistry *registry = m_registry;
    g_object_ref(registry);
    run([registry, uid]() {
        GList *sources = e_source_registry_list_enabled(registry, E_SOURCE_EXTENSION_CALENDAR);
        for (GList *l = sources; l; l = l->next) {
            if (EClient *client = e_cal_client_connect_sync(E_SOURCE(l->data), E_CAL_CLIENT_SOURCE_TYPE_EVENTS, 15, nullptr, nullptr)) {
                if (!e_client_is_readonly(client)) {
                    e_cal_client_remove_object_sync(E_CAL_CLIENT(client), uid.toUtf8().constData(), nullptr, E_CAL_OBJ_MOD_ALL, E_CAL_OPERATION_FLAG_NONE, nullptr, nullptr);
                }
                g_object_unref(client);
            }
        }
        g_list_free_full(sources, g_object_unref);
        g_object_unref(registry);
    }, [this]() {
        scheduleReload();
        Q_EMIT notice(tr("Taken out of your calendar"));
    });
}

namespace
{
// Calendars of people's own events: web calendars (holidays among them)
// come back from their address, and birthdays from the contacts.
bool ownEvents(ESource *source)
{
    const QString backend = backendOf(source);
    return backend != QLatin1String("webcal") && backend != QLatin1String("contacts") && backend != QLatin1String("weather");
}

void collectZone(ICalParameter *param, gpointer data)
{
    if (const gchar *tzid = i_cal_parameter_get_tzid(param)) {
        static_cast<QSet<QByteArray> *>(data)->insert(QByteArray(tzid));
    }
}

QString safeFileName(QString name)
{
    name.replace(QRegularExpression(QStringLiteral("[/\\\\:*?\"<>|]")), QStringLiteral(" "));
    name = name.simplified();
    return name.isEmpty() ? CalendarEngine::tr("Calendar") : name;
}

// One calendar as an .ics file, with the time zones its events use, as
// every calendar app reads them; how many events, or -1.
int writeCalendar(ECalClient *client, const QString &path, QString *error)
{
    GSList *objects = nullptr;
    GError *err = nullptr;
    if (!e_cal_client_get_object_list_sync(client, "#t", &objects, nullptr, &err)) {
        *error = str(err ? err->message : "");
        g_clear_error(&err);
        return -1;
    }
    QByteArray body;
    QSet<QByteArray> zones;
    int count = 0;
    for (GSList *l = objects; l; l = l->next) {
        auto *icomp = static_cast<ICalComponent *>(l->data);
        i_cal_component_foreach_tzid(icomp, collectZone, &zones);
        body += takeStr(i_cal_component_as_ical_string(icomp)).toUtf8();
        ICalProperty *rid = i_cal_component_get_first_property(icomp, I_CAL_RECURRENCEID_PROPERTY);
        if (rid) {
            g_object_unref(rid);
        } else {
            ++count;
        }
    }
    g_slist_free_full(objects, g_object_unref);
    QByteArray head = "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Wild OS//Mail//EN\r\n";
    for (const QByteArray &tzid : std::as_const(zones)) {
        ICalTimezone *zone = nullptr;
        if (e_cal_client_get_timezone_sync(client, tzid.constData(), &zone, nullptr, nullptr) && zone) {
            if (ICalComponent *vtz = i_cal_timezone_get_component(zone)) {
                head += takeStr(i_cal_component_as_ical_string(vtz)).toUtf8();
                g_object_unref(vtz);
            }
        }
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = CalendarEngine::tr("Mail couldn't write to %1.").arg(QFileInfo(path).absolutePath());
        return -1;
    }
    file.write(head);
    file.write(body);
    file.write("END:VCALENDAR\r\n");
    return count;
}

// Every event of an .ics file into the calendar: its time zones first, then
// whole events before the changed single dates of repeating ones, which
// need their series in place. An event whose UID is already there is left
// alone, so importing the same file twice adds nothing.
int readCalendar(ECalClient *client, const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = CalendarEngine::tr("The file couldn't be read.");
        return -1;
    }
    ICalComponent *cal = i_cal_component_new_from_string(file.readAll().constData());
    if (!cal) {
        *error = CalendarEngine::tr("That isn't a calendar file Mail can read.");
        return -1;
    }
    QList<ICalComponent *> events;
    if (i_cal_component_isa(cal) == I_CAL_VEVENT_COMPONENT) {
        events << i_cal_component_clone(cal);
    } else {
        for (ICalComponent *tz = i_cal_component_get_first_component(cal, I_CAL_VTIMEZONE_COMPONENT); tz;
             tz = i_cal_component_get_next_component(cal, I_CAL_VTIMEZONE_COMPONENT)) {
            // A copy: the zone takes over the component it's given.
            ICalComponent *own = i_cal_component_clone(tz);
            ICalTimezone *zone = i_cal_timezone_new();
            if (i_cal_timezone_set_component(zone, own)) {
                e_cal_client_add_timezone_sync(client, zone, nullptr, nullptr);
            }
            g_object_unref(own);
            g_object_unref(zone);
            g_object_unref(tz);
        }
        for (ICalComponent *ev = i_cal_component_get_first_component(cal, I_CAL_VEVENT_COMPONENT); ev;
             ev = i_cal_component_get_next_component(cal, I_CAL_VEVENT_COMPONENT)) {
            events << i_cal_component_clone(ev);
            g_object_unref(ev);
        }
    }
    g_object_unref(cal);
    auto detached = [](ICalComponent *c) {
        ICalProperty *rid = i_cal_component_get_first_property(c, I_CAL_RECURRENCEID_PROPERTY);
        const bool found = rid != nullptr;
        g_clear_object(&rid);
        return found;
    };
    std::stable_partition(events.begin(), events.end(), [&](ICalComponent *c) {
        return !detached(c);
    });
    int added = 0;
    for (ICalComponent *ev : std::as_const(events)) {
        if (str(i_cal_component_get_uid(ev)).isEmpty()) {
            i_cal_component_set_uid(ev, QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8().constData());
        }
        GError *err = nullptr;
        bool ok = false;
        if (detached(ev)) {
            ok = e_cal_client_modify_object_sync(client, ev, E_CAL_OBJ_MOD_THIS, E_CAL_OPERATION_FLAG_NONE, nullptr, &err);
        } else {
            ok = e_cal_client_create_object_sync(client, ev, E_CAL_OPERATION_FLAG_NONE, nullptr, nullptr, &err);
        }
        if (ok) {
            ++added;
        } else if (!g_error_matches(err, E_CAL_CLIENT_ERROR, E_CAL_CLIENT_ERROR_OBJECT_ID_ALREADY_EXISTS)) {
            qCWarning(calendarLog) << "couldn't import an event" << (err ? err->message : "");
        }
        g_clear_error(&err);
        g_object_unref(ev);
    }
    return added;
}
}

int CalendarEngine::writeAll(const QString &folder, QString *error)
{
    GError *err = nullptr;
    ESourceRegistry *registry = e_source_registry_new_sync(nullptr, &err);
    if (!registry) {
        *error = str(err ? err->message : "");
        g_clear_error(&err);
        return -1;
    }
    QDir().mkpath(folder);
    int total = 0;
    QSet<QString> used;
    GList *sources = e_source_registry_list_enabled(registry, E_SOURCE_EXTENSION_CALENDAR);
    for (GList *l = sources; l; l = l->next) {
        auto *source = E_SOURCE(l->data);
        if (!ownEvents(source)) {
            continue;
        }
        EClient *client = e_cal_client_connect_sync(source, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, 30, nullptr, nullptr);
        if (!client) {
            continue;
        }
        const QString uid = str(e_source_get_uid(source));
        const QString base = safeFileName(uid == QLatin1String("system-calendar") ? tr("Calendar") : str(e_source_get_display_name(source)));
        // Two calendars of the same name (one per account) keep a file each.
        QString name = base;
        for (int n = 2; used.contains(name.toLower()); ++n) {
            name = QStringLiteral("%1 (%2)").arg(base).arg(n);
        }
        used.insert(name.toLower());
        const int count = writeCalendar(E_CAL_CLIENT(client), folder + QLatin1Char('/') + name + QStringLiteral(".ics"), error);
        g_object_unref(client);
        if (count < 0) {
            total = -1;
            break;
        }
        total += count;
    }
    g_list_free_full(sources, g_object_unref);
    g_object_unref(registry);
    return total;
}

int CalendarEngine::readInto(const QString &path, const QString &calendarName, const QString &fallbackUid, QString *error)
{
    GError *err = nullptr;
    ESourceRegistry *registry = e_source_registry_new_sync(nullptr, &err);
    if (!registry) {
        *error = str(err ? err->message : "");
        g_clear_error(&err);
        return -1;
    }
    ESource *target = nullptr;
    if (!calendarName.isEmpty()) {
        GList *sources = e_source_registry_list_enabled(registry, E_SOURCE_EXTENSION_CALENDAR);
        for (GList *l = sources; l && !target; l = l->next) {
            auto *source = E_SOURCE(l->data);
            const QString uid = str(e_source_get_uid(source));
            const QString name = uid == QLatin1String("system-calendar") ? tr("Calendar") : str(e_source_get_display_name(source));
            if (ownEvents(source) && name.compare(calendarName, Qt::CaseInsensitive) == 0) {
                target = E_SOURCE(g_object_ref(source));
            }
        }
        g_list_free_full(sources, g_object_unref);
    }
    if (!target && !fallbackUid.isEmpty()) {
        target = e_source_registry_ref_source(registry, fallbackUid.toUtf8().constData());
    }
    if (!target) {
        target = e_source_registry_ref_default_calendar(registry);
    }
    g_object_unref(registry);
    EClient *client = target ? e_cal_client_connect_sync(target, E_CAL_CLIENT_SOURCE_TYPE_EVENTS, 30, nullptr, &err) : nullptr;
    g_clear_object(&target);
    if (!client || e_client_is_readonly(client)) {
        *error = err ? str(err->message) : tr("The calendar can't be changed.");
        g_clear_error(&err);
        g_clear_object(&client);
        return -1;
    }
    const int added = readCalendar(E_CAL_CLIENT(client), path, error);
    g_object_unref(client);
    return added;
}

void CalendarEngine::exportTo(const QUrl &folder)
{
    const QString dir = folder.toLocalFile();
    auto count = std::make_shared<int>(0);
    auto error = std::make_shared<QString>();
    run([dir, count, error]() {
        *count = writeAll(dir, error.get());
    }, [this, dir, count, error]() {
        if (*count < 0) {
            Q_EMIT notice(tr("The calendars weren't saved. %1").arg(*error));
        } else {
            Q_EMIT notice(*count == 1 ? tr("1 event saved to %1").arg(dir) : tr("%1 events saved to %2").arg(*count).arg(dir));
        }
    });
}

void CalendarEngine::importFrom(const QUrl &file, const QString &calendarUid)
{
    const QString path = file.toLocalFile();
    const QString target = calendarUid.isEmpty() ? defaultCalendar() : calendarUid;
    auto count = std::make_shared<int>(0);
    auto error = std::make_shared<QString>();
    run([path, target, count, error]() {
        *count = readInto(path, {}, target, error.get());
    }, [this, count, error]() {
        if (*count < 0) {
            Q_EMIT notice(tr("The events weren't imported. %1").arg(*error));
            return;
        }
        scheduleReload();
        Q_EMIT notice(*count == 1 ? tr("1 event imported") : tr("%1 events imported").arg(*count));
    });
}
