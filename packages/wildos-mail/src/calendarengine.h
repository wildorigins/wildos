#pragma once

#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QTimeZone>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>

#include <libecal/libecal.h>

#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(calendarLog)

class QQmlEngine;
class QJSEngine;

// Everything Calendar knows, on top of evolution-data-server's calendar
// service: the calendars from its source registry (on this PC, Google,
// CalDAV and web calendars) and the events in the days on screen, with
// repeating events already expanded. The service does the syncing; this
// asks it for what to draw and hands it what was changed.
class CalendarEngine : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(QString problem READ problem NOTIFY readyChanged)
    // [{uid, name, color, selected, group, groupUid, readOnly, kind}]
    Q_PROPERTY(QVariantList calendars READ calendars NOTIFY calendarsChanged)
    // [{uid, name, kind, email}]: Google and CalDAV accounts.
    Q_PROPERTY(QVariantList accounts READ accounts NOTIFY calendarsChanged)
    Q_PROPERTY(QString defaultCalendar READ defaultCalendar WRITE setDefaultCalendar NOTIFY calendarsChanged)
    Q_PROPERTY(QDate rangeStart READ rangeStart NOTIFY rangeChanged)
    Q_PROPERTY(QDate rangeEnd READ rangeEnd NOTIFY rangeChanged)
    // Goes up whenever the events in the range change; views bind to it.
    Q_PROPERTY(int revision READ revision NOTIFY eventsChanged)
    Q_PROPERTY(QString timeZoneName READ timeZoneName NOTIFY timeZoneChanged)
    Q_PROPERTY(QString timeZoneId READ timeZoneId WRITE setTimeZoneId NOTIFY timeZoneChanged)

public:
    // True while a refresh asked of the accounts is still running, held for
    // at least a moment so a quick one still shows the icon turning.
    Q_PROPERTY(bool syncing READ syncing NOTIFY syncingChanged)
    static CalendarEngine *instance();
    static CalendarEngine *create(QQmlEngine *, QJSEngine *);
    ~CalendarEngine() override;

    bool ready() const
    {
        return m_registry != nullptr;
    }
    QString problem() const
    {
        return m_problem;
    }
    QVariantList calendars() const
    {
        return m_calendars;
    }
    QVariantList accounts() const
    {
        return m_accounts;
    }
    QString defaultCalendar() const;
    void setDefaultCalendar(const QString &uid);
    QDate rangeStart() const
    {
        return m_from;
    }
    QDate rangeEnd() const
    {
        return m_to;
    }
    int revision() const
    {
        return m_revision;
    }
    QString timeZoneName() const;
    QString timeZoneId() const;
    void setTimeZoneId(const QString &id);

    // The days on screen: from the first up to, not including, the last.
    Q_INVOKABLE void setRange(const QDate &from, const QDate &to);
    Q_INVOKABLE void setSelected(const QString &calendarUid, bool selected);

    // What a view draws, from the events already fetched for the range.
    // Timed events on one day, laid out side by side where they overlap:
    // [{key, title, location, color, startMinute, endMinute, column, columns, ...}]
    Q_INVOKABLE QVariantList timedOn(const QDate &day) const;
    // Events that fill whole days, or run past midnight for a day or more,
    // across the days from..to: [{key, title, color, firstColumn, lastColumn, row}]
    Q_INVOKABLE QVariantList allDayBetween(const QDate &from, const QDate &to) const;
    // Everything on one day, all-day first, for a month cell.
    Q_INVOKABLE QVariantList eventsOn(const QDate &day) const;
    // Days in the range with something on, for the small month.
    Q_INVOKABLE bool busyOn(const QDate &day) const;

    // One event in full, for the event card and editing it.
    Q_INVOKABLE QVariantMap event(const QString &key) const;
    // Creates or changes an event (see calendarengine.cpp for the fields).
    Q_INVOKABLE void saveEvent(const QVariantMap &data);
    Q_INVOKABLE void removeEvent(const QString &key, bool wholeSeries);
    // Going, Maybe or Not going, for an event someone invited you to.
    Q_INVOKABLE void respond(const QString &key, const QString &status);
    // A dragged event, to its new start; it keeps its length.
    Q_INVOKABLE void moveEvent(const QString &key, const QDateTime &start);

    // Events for one day outside the range, for the editor's day beside the
    // form: answered with dayLoaded.
    Q_INVOKABLE void loadDay(const QDate &day);
    // Events whose title, place or notes contain the text, a year either
    // side of today: answered with searchDone.
    Q_INVOKABLE void search(const QString &text);
    // The next events with someone in them, as organiser or guest, over
    // the next three months: answered with upcomingFound, at most limit.
    Q_INVOKABLE void upcomingWith(const QString &address, int limit);

    Q_INVOKABLE void addLocalCalendar(const QString &name, const QString &color);
    Q_INVOKABLE void addWebCalendar(const QString &name, const QString &url, const QString &color);
    Q_INVOKABLE void renameCalendar(const QString &uid, const QString &name);
    Q_INVOKABLE void setColor(const QString &uid, const QString &color);
    Q_INVOKABLE void removeCalendar(const QString &uid);
    // A Google account from a refresh token (GoogleSignIn), or a CalDAV
    // server with a password; the service finds the calendars in it.
    Q_INVOKABLE void addGoogleAccount(const QString &email, const QString &refreshToken);
    // An Outlook.com or Microsoft 365 account from a refresh token
    // (MicrosoftSignIn): calendars, contacts and task lists through
    // evolution-data-server's Microsoft 365 module.
    Q_INVOKABLE void addMicrosoftAccount(const QString &email, const QString &refreshToken);
    // Whether the calendar service has an account for this address, and
    // whether its calendars and contacts are on: {exists, calendar, contacts}.
    Q_INVOKABLE QVariantMap accountParts(const QString &email) const;
    // part: "calendar" or "contacts".
    Q_INVOKABLE void setAccountPart(const QString &email, const QString &part, bool on);
    Q_INVOKABLE void addCalDavAccount(const QString &name, const QString &url, const QString &user, const QString &password);
    Q_INVOKABLE void removeAccount(const QString &uid);
    Q_INVOKABLE void refresh();
    bool syncing() const { return m_syncing > 0 || m_syncHold.isActive(); }
    // Counts a refresh in and out; the callback ends it.
    void startRefresh(EClient *client);
    static void onRefreshed(GObject *, GAsyncResult *result, gpointer);
    int m_syncing = 0;
    QTimer m_syncHold;

    // An invitation in a message (MailMsInvite.dc): what it's for, and
    // the answer. me is the address it came to.
    Q_INVOKABLE QVariantMap describeInvitation(const QString &ics, const QString &me) const;
    // Going, Maybe or Not going: changes your answer on the event where a
    // calendar account has it (its server tells the organiser), or adds it
    // to the default calendar; answered with invitationAnswered, whose
    // reply is the iCalendar to mail the organiser when no server will.
    Q_INVOKABLE void answerInvitation(const QString &ics, const QString &status, const QString &me);
    // A cancelled event, taken out of whichever calendar has it.
    Q_INVOKABLE void removeInvitation(const QString &ics);

    // UK bank holidays and birthdays from contacts (CalMsSettings).
    // iCalendar files. Export writes each calendar of people's own events
    // (not web, holiday or birthday calendars) to a file of its own in the
    // folder; import adds a file's events to a calendar, the default one
    // when calendarUid is empty, leaving out any already there. Both are
    // answered with notice.
    Q_INVOKABLE void exportTo(const QUrl &folder);
    Q_INVOKABLE void importFrom(const QUrl &file, const QString &calendarUid);
    // The blocking halves, for Mail's backup and restore on a worker
    // thread. writeAll gives how many events went out, readInto how many
    // came in (into the calendar of the same name if there is one, else
    // fallbackUid); -1 with error set when it failed.
    static int writeAll(const QString &folder, QString *error);
    static int readInto(const QString &path, const QString &calendarName, const QString &fallbackUid, QString *error);
    Q_INVOKABLE bool hasHolidays() const;
    Q_INVOKABLE void setHolidays(bool on);
    Q_INVOKABLE bool hasBirthdays() const;
    Q_INVOKABLE void setBirthdays(bool on);

Q_SIGNALS:
    void readyChanged();
    void calendarsChanged();
    void syncingChanged();
    void rangeChanged();
    void eventsChanged();
    void timeZoneChanged();
    void notice(const QString &text);
    void saved(const QString &key);
    void dayLoaded(const QDate &day, const QVariantList &events);
    void searchDone(const QString &text, const QVariantList &results);
    void upcomingFound(const QString &address, const QVariantList &events);
    void accountAdded(const QString &uid);
    void accountFailed(const QString &message);
    void invitationAnswered(const QString &uid, const QString &status, const QString &reply, const QString &organizer, const QString &title);

private:
    explicit CalendarEngine(QObject *parent = nullptr);

    struct Instance {
        QString key;
        QString calendarUid;
        QString uid;
        QString rid;
        QString title;
        QString location;
        QDateTime start;
        QDateTime end;
        bool allDay = false;
        bool recurring = false;
        bool invited = false;
        bool busy = true;
        QString myStatus;
    };

    static void onRegistryReady(GObject *, GAsyncResult *result, gpointer self);
    static void onSourceChanged(ESourceRegistry *, ESource *, gpointer self);
    static void onClientConnected(GObject *, GAsyncResult *result, gpointer data);
    static void onViewReady(GObject *, GAsyncResult *result, gpointer data);
    static void onViewObjects(ECalClientView *, gpointer, gpointer self);

    void rebuildCalendars();
    void connectClient(ESource *source);
    void dropClient(const QString &uid);
    void startView(const QString &uid);
    void scheduleReload();
    void reload();
    void flatten();
    void run(std::function<void()> work, std::function<void()> done);
    ECalClient *client(const QString &uid) const;
    ICalTimezone *zone() const;
    QDateTime toDateTime(ICalTime *time) const;
    QVariantMap row(const Instance &i) const;
    QList<Instance> fetch(ECalClient *client, const QString &calendarUid, const QDate &from, const QDate &to) const;
    QString myAddress(const QString &calendarUid) const;
    void createSources(const QList<ESource *> &sources, std::function<void(bool, const QString &)> done);

    ESourceRegistry *m_registry = nullptr;
    GCancellable *m_cancel = nullptr;
    QString m_problem;
    QHash<QString, ECalClient *> m_clients;
    QHash<QString, ECalClientView *> m_views;
    QHash<QString, QString> m_colors;
    QSet<QString> m_readOnly;
    QVariantList m_calendars;
    QVariantList m_accounts;
    QDate m_from;
    QDate m_to;
    QHash<QString, QList<Instance>> m_perCalendar;
    QList<Instance> m_instances;
    QHash<QString, int> m_byKey;
    int m_revision = 0;
    int m_generation = 0;
    QTimer m_reloadTimer;
    QTimeZone m_zone;
};
