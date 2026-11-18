#include "agent.h"

#include "session.h"

#include <PolkitQt1/Subject>

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFile>
#include <QThread>

#include <grp.h>
#include <pwd.h>
#include <unistd.h>

namespace
{
const QString objectPath = QStringLiteral("/org/wildos/PolicyKit1/AuthenticationAgent");
constexpr int triesBeforeWait = 3;
constexpr int triesBeforeNo = 6;
constexpr int waitLength = 30;

QString fullNameOf(const passwd *pw)
{
    const QString gecos = QString::fromLocal8Bit(pw->pw_gecos).section(QLatin1Char(','), 0, 0).trimmed();
    return gecos.isEmpty() ? QString::fromLocal8Bit(pw->pw_name) : gecos;
}
}

Agent::Agent(QObject *parent)
    : PolkitQt1::Agent::Listener(parent)
{
    m_wait.setInterval(1000);
    connect(&m_wait, &QTimer::timeout, this, [this] {
        if (--m_waitSeconds <= 0) {
            m_waitSeconds = 0;
            m_wait.stop();
            m_error.clear();
        }
        Q_EMIT stateChanged();
    });
    m_retry.setSingleShot(true);
    m_retry.setInterval(5000);
    connect(&m_retry, &QTimer::timeout, this, &Agent::start);
}

Agent::~Agent()
{
    if (m_active && m_request.result) {
        m_request.result->setCompleted();
    }
}

void Agent::start()
{
    bool registered = false;
    // The rig has no logind; it names the one process to answer for.
    const qint64 processPid = qEnvironmentVariableIntValue("WILDOS_POLKIT_PROCESS");
    if (processPid > 0) {
        registered = registerListener(PolkitQt1::UnixProcessSubject(processPid), objectPath);
    } else {
        const QString id = SessionInfo::id();
        registered = id.isEmpty() ? registerListener(PolkitQt1::UnixSessionSubject(getpid()), objectPath)
                                  : registerListener(PolkitQt1::UnixSessionSubject(id), objectPath);
    }
    if (!registered) {
        qWarning("wildos-dialogs: polkit did not accept the permission prompt yet, trying again");
        m_retry.start();
    }
}

void Agent::initiateAuthentication(const QString &actionId, const QString &message, const QString &, const PolkitQt1::Details &details,
                                   const QString &cookie, const PolkitQt1::Identity::List &identities, PolkitQt1::Agent::AsyncResult *result)
{
    Request request;
    request.actionId = actionId;
    request.message = message;
    for (const QString &key : details.keys()) {
        request.details.insert(key, details.lookup(key));
    }
    if (qEnvironmentVariableIsSet("WILDOS_DIALOGS_DEBUG")) {
        qInfo() << "wildos-dialogs: request" << actionId << request.details;
    }
    request.cookie = cookie;
    request.identities = identities;
    request.result = result;
    if (m_active) {
        m_queue.enqueue(request);
    } else {
        begin(request);
    }
}

bool Agent::initiateAuthenticationFinish()
{
    return true;
}

void Agent::cancelAuthentication()
{
    cancel();
}

void Agent::begin(const Request &request)
{
    m_request = request;
    m_cancelled = false;
    m_failures = 0;
    m_waitSeconds = 0;
    m_wait.stop();
    m_error.clear();
    m_busy = false;
    m_trust = QStringLiteral("checking");
    m_verdict = {};
    m_verdict.purpose = Verifier::purposeOf(request.actionId, request.details.value(QStringLiteral("program")));
    // Until the check is done the prompt would otherwise read "Allow  to
    // make changes".
    m_verdict.appName = Verifier::quickName(request.actionId, request.details, &m_verdict.iconName);
    ++m_serial;
    Q_EMIT serialChanged();

    // Groups (the sudo group, for "an administrator") stand for their
    // members; the person signed in comes first when they are one.
    m_people.clear();
    const auto add = [this](const passwd *pw) {
        if (!pw) {
            return;
        }
        for (const Person &known : std::as_const(m_people)) {
            if (known.uid == pw->pw_uid) {
                return;
            }
        }
        m_people.append({uint(pw->pw_uid), QString::fromLocal8Bit(pw->pw_name), fullNameOf(pw)});
    };
    for (const PolkitQt1::Identity &identity : std::as_const(request.identities)) {
        const QString text = identity.toString();
        if (text.startsWith(QLatin1String("unix-user:"))) {
            const QByteArray who = text.mid(10).toLocal8Bit();
            bool numeric = false;
            const uint uid = QString::fromLocal8Bit(who).toUInt(&numeric);
            add(numeric ? getpwuid(uid) : getpwnam(who.constData()));
        } else if (text.startsWith(QLatin1String("unix-group:"))) {
            const QByteArray name = text.mid(11).toLocal8Bit();
            bool numeric = false;
            const uint gid = QString::fromLocal8Bit(name).toUInt(&numeric);
            const group *gr = numeric ? getgrgid(gid) : getgrnam(name.constData());
            if (!gr) {
                continue;
            }
            const gid_t groupId = gr->gr_gid;
            QList<QByteArray> members;
            for (char **member = gr->gr_mem; member && *member; ++member) {
                members << QByteArray(*member);
            }
            for (const QByteArray &member : std::as_const(members)) {
                add(getpwnam(member.constData()));
            }
            setpwent();
            while (const passwd *pw = getpwent()) {
                if (pw->pw_gid == groupId && pw->pw_uid >= 1000 && pw->pw_uid < 60000) {
                    add(pw);
                }
            }
            endpwent();
        }
    }
    const uint me = getuid();
    for (int i = 0; i < m_people.size(); ++i) {
        if (m_people.at(i).uid == me) {
            m_people.move(i, 0);
            break;
        }
    }
    m_person = 0;

    m_active = true;
    Q_EMIT requestChanged();
    Q_EMIT personChanged();
    Q_EMIT stateChanged();
    Q_EMIT activeChanged();

    const int serial = m_serial;
    QPointer<Agent> self(this);
    QThread *thread = QThread::create([self, serial, actionId = request.actionId, message = request.message, details = request.details] {
        const Verdict verdict = Verifier::check(actionId, message, details);
        QMetaObject::invokeMethod(qApp, [self, serial, verdict] {
            if (!self || self->m_serial != serial || !self->m_active) {
                return;
            }
            self->m_verdict = verdict;
            self->m_trust = verdict.verified ? QStringLiteral("verified") : QStringLiteral("unverified");
            Q_EMIT self->requestChanged();
        });
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();

    m_info.clear();
    m_autoFinger = true;
    checkFingerprint();
    startSession();
}

void Agent::startSession()
{
    dropSession();
    m_prompted = false;
    m_listening = false;
    m_fingerEnd = FingerEnd::None;
    if (m_people.isEmpty()) {
        setError(tr("No account on this computer can allow this."));
        return;
    }
    const Person &who = m_people.at(m_person);
    m_session = new PolkitQt1::Agent::Session(PolkitQt1::UnixUserIdentity(who.uid), m_request.cookie, m_request.result, this);
    connect(m_session, &PolkitQt1::Agent::Session::request, this, [this](const QString &, bool echo) {
        m_prompted = true;
        m_promptEcho = echo;
        if (m_hasPending) {
            m_hasPending = false;
            m_session->setResponse(m_pending);
            m_pending.clear();
            m_prompted = false;
        } else {
            listen();
        }
    });
    connect(m_session, &PolkitQt1::Agent::Session::completed, this, &Agent::onCompleted);
    connect(m_session, &PolkitQt1::Agent::Session::showError, this, [this](const QString &text) {
        onMessage(text.trimmed(), true);
    });
    connect(m_session, &PolkitQt1::Agent::Session::showInfo, this, [this](const QString &text) {
        onMessage(text.trimmed(), false);
    });
    m_session->initiate();
}

// Deleting the session sends its helper SIGTERM (polkit's finalize), and
// with it goes a pam_fprintd still waiting for a finger; fprintd releases
// the reader when the helper's bus connection closes.
void Agent::dropSession()
{
    if (m_session) {
        m_session->disconnect(this);
        m_session->deleteLater();
        m_session = nullptr;
    }
}

// Answers polkit's password question with an empty password, which the
// wildos-fingerprint PAM profile takes as "use my finger", while leaving
// the field free to type in.
void Agent::listen()
{
    if (!m_session || !m_prompted || m_promptEcho || !m_autoFinger || !fingerprint() || m_busy || m_hasPending || m_waitSeconds > 0 || m_demo) {
        return;
    }
    m_prompted = false;
    m_listening = true;
    m_fingerEnd = FingerEnd::None;
    m_listenClock.start();
    m_info = tr("Touch the fingerprint reader, or type your password.");
    m_session->setResponse(QString());
    Q_EMIT stateChanged();
}

// A fresh session for a typed password; the one waiting on the reader goes.
void Agent::stopListening()
{
    m_autoFinger = false;
    if (!m_listening) {
        return;
    }
    m_info.clear();
    m_error.clear();
    startSession();
    Q_EMIT stateChanged();
}

void Agent::typing()
{
    if (m_active && !m_demo) {
        stopListening();
    }
}

void Agent::setError(const QString &text)
{
    m_error = text;
    m_info.clear();
    Q_EMIT stateChanged();
}

// pam_fprintd speaks for the reader: "Place your right index finger on the
// fingerprint reader", "Swipe ... across", "Failed to match fingerprint",
// "Verification timed out". Its asks are instructions, not errors, and its
// words are fprintd's.
void Agent::onMessage(const QString &text, bool error)
{
    if (!text.contains(QLatin1String("finger"), Qt::CaseInsensitive) && !text.contains(QLatin1String("Verification"))) {
        setError(text);
        return;
    }
    const bool swipe = text.contains(QLatin1String("swipe"), Qt::CaseInsensitive);
    if (text.contains(QLatin1String("timed out"), Qt::CaseInsensitive)) {
        m_fingerEnd = FingerEnd::TimedOut;
        m_error = tr("No finger was read in time. Type your password, or leave it empty and try again.");
        m_info.clear();
    } else if (text.contains(QLatin1String("match"), Qt::CaseInsensitive)) {
        m_fingerEnd = FingerEnd::NoMatch;
        m_error = tr("That fingerprint wasn't recognised. Try again.");
        m_info.clear();
    } else if (!error) {
        // After a finger that didn't match, pam_fprintd asks again at once;
        // the mismatch stays on screen until the next touch.
        m_info = m_listening ? (swipe ? tr("Swipe your finger across the fingerprint reader, or type your password.")
                                      : tr("Touch the fingerprint reader, or type your password."))
            : swipe ? tr("Swipe your finger across the fingerprint reader.")
                    : tr("Touch the fingerprint reader.");
    } else {
        // The reader gave up (unplugged, or no touch in time).
        m_fingerEnd = FingerEnd::Broken;
        m_error = tr("The fingerprint reader didn't answer. Type your password instead.");
        m_info.clear();
    }
    Q_EMIT stateChanged();
}

// Only your own fingers can be looked up: fprintd asks polkit before it
// tells anyone about another account's. A person chosen from the list who
// is not you just types their password. Adding or removing a finger is
// asked for while Settings holds the reader, so a finger can't answer that
// one.
// Settings > Accounts > Sign-in options' "Admin prompts" switch, the admin=
// line fingerprint-turn also reads. Off, the PAM side turns the empty
// password down, so listening for a finger would only cost a failed try.
static bool fingerprintForAdmin()
{
    QFile conf(QStringLiteral("/etc/wildos/fingerprint.conf"));
    if (!conf.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return true;
    }
    bool on = true;
    while (!conf.atEnd()) {
        const QByteArray line = conf.readLine().trimmed();
        const qsizetype equals = line.indexOf('=');
        if (equals > 0 && line.left(equals).trimmed() == "admin") {
            on = line.mid(equals + 1).trimmed() != "off";
        }
    }
    return on;
}

void Agent::checkFingerprint()
{
    m_fingerprint = false;
    Q_EMIT personChanged();
    if (m_request.actionId.startsWith(QLatin1String("net.reactivated.fprint.")) || !fingerprintForAdmin()) {
        return;
    }
    const QString service = QStringLiteral("net.reactivated.Fprint");
    const QDBusMessage manager = QDBusMessage::createMethodCall(service, QStringLiteral("/net/reactivated/Fprint/Manager"),
                                                                QStringLiteral("net.reactivated.Fprint.Manager"), QStringLiteral("GetDefaultDevice"));
    auto watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(manager, 5000), this);
    const int serial = m_serial;
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, service, serial] {
        watcher->deleteLater();
        const QDBusPendingReply<QDBusObjectPath> device = *watcher;
        if (device.isError() || serial != m_serial) {
            return;
        }
        QDBusMessage list = QDBusMessage::createMethodCall(service, device.value().path(), QStringLiteral("net.reactivated.Fprint.Device"),
                                                          QStringLiteral("ListEnrolledFingers"));
        list << QString();
        auto listed = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(list, 5000), this);
        connect(listed, &QDBusPendingCallWatcher::finished, this, [this, listed, serial] {
            listed->deleteLater();
            const QDBusPendingReply<QStringList> fingers = *listed;
            if (serial == m_serial && !fingers.isError() && !fingers.value().isEmpty()) {
                m_fingerprint = true;
                Q_EMIT personChanged();
                // Polkit may have asked for the password already.
                listen();
            }
        });
    });
}

void Agent::onCompleted(bool gained)
{
    if (!m_active) {
        return;
    }
    if (gained || m_cancelled) {
        if (m_request.result) {
            m_request.result->setCompleted();
        }
        finish();
        return;
    }
    // A finger nobody was asked for is not a wrong password. No touch in
    // time: listen again, unless the reader gave that answer at once,
    // which would only spin. Fingers that didn't match after
    // pam_fprintd's own tries, or a reader that failed: the password, or
    // an empty one with Yes to try again.
    if (m_listening) {
        m_listening = false;
        m_info.clear();
        if (m_fingerEnd == FingerEnd::TimedOut && m_listenClock.elapsed() >= 5000) {
            m_error.clear();
        } else {
            m_autoFinger = false;
            if (m_fingerEnd == FingerEnd::NoMatch) {
                m_error = tr("That fingerprint wasn't recognised. Type your password, or leave it empty and select Yes to try again.");
            } else if (m_fingerEnd == FingerEnd::TimedOut) {
                m_error = tr("The fingerprint reader didn't answer. Type your password instead.");
            }
        }
        Q_EMIT stateChanged();
        startSession();
        return;
    }
    ++m_failures;
    m_busy = false;
    m_hasPending = false;
    m_info.clear();
    if (m_failures >= triesBeforeNo) {
        if (m_request.result) {
            m_request.result->setCompleted();
        }
        finish();
        return;
    }
    if (m_failures % triesBeforeWait == 0) {
        m_waitSeconds = waitLength;
        m_error = tr("Too many wrong tries.");
        m_wait.start();
    } else if (m_error.isEmpty() || m_error.contains(QLatin1String("Authentication failure"), Qt::CaseInsensitive)) {
        m_error = tr("That password isn't right. Try again.");
    }
    Q_EMIT stateChanged();
    // Polkit's helper answers once per session, so another try needs a
    // fresh one.
    startSession();
}

void Agent::finish()
{
    dropSession();
    m_listening = false;
    m_autoFinger = false;
    m_wait.stop();
    m_active = false;
    m_busy = false;
    m_request = {};
    Q_EMIT stateChanged();
    Q_EMIT activeChanged();
    if (!m_queue.isEmpty()) {
        QTimer::singleShot(300, this, [this] {
            if (!m_active && !m_queue.isEmpty()) {
                begin(m_queue.dequeue());
            }
        });
    }
}

void Agent::choose(int index)
{
    if (index < 0 || index >= m_people.size() || index == m_person) {
        return;
    }
    m_person = index;
    m_failures = 0;
    m_error.clear();
    m_info.clear();
    Q_EMIT personChanged();
    Q_EMIT stateChanged();
    if (!m_demo) {
        startSession();
    }
}

void Agent::submit(const QString &password)
{
    if (!m_active || m_waitSeconds > 0 || m_busy) {
        return;
    }
    if (m_demo) {
        if (password == QLatin1String("wrong")) {
            ++m_failures;
            m_error = tr("That password isn't right. Try again.");
            Q_EMIT stateChanged();
        } else {
            m_active = false;
            Q_EMIT activeChanged();
        }
        return;
    }
    // The finger is already being listened for; a typed password takes
    // over from it.
    if (m_listening) {
        if (password.isEmpty()) {
            return;
        }
        stopListening();
    }
    m_busy = true;
    m_error.clear();
    m_info.clear();
    Q_EMIT stateChanged();
    if (m_session && m_prompted) {
        m_prompted = false;
        m_session->setResponse(password);
    } else {
        m_pending = password;
        m_hasPending = true;
    }
}

void Agent::cancel()
{
    if (!m_active) {
        return;
    }
    if (m_demo) {
        m_active = false;
        Q_EMIT activeChanged();
        return;
    }
    m_cancelled = true;
    if (m_session) {
        m_session->cancel();
    }
    // The session normally reports back from cancel(); when it was never
    // started there is nothing to report.
    if (m_active) {
        if (m_request.result) {
            m_request.result->setCompleted();
        }
        finish();
    }
}

QVariantList Agent::details() const
{
    QVariantList list;
    const auto add = [&list](const QString &label, const QString &value) {
        if (!value.isEmpty()) {
            list.append(QVariantMap{{QStringLiteral("label"), label}, {QStringLiteral("value"), value}});
        }
    };
    add(tr("Action"), m_request.actionId);
    const QString commandLine = !m_verdict.commandLine.isEmpty() ? m_verdict.commandLine : m_request.details.value(QStringLiteral("command_line"));
    add(tr("Program"), !commandLine.isEmpty() ? commandLine : m_verdict.program);
    const QString pid = m_request.details.value(QStringLiteral("polkit.subject-pid"));
    add(tr("Asked by"), m_verdict.caller.isEmpty() ? QString() : pid.isEmpty() ? m_verdict.caller : tr("%1 (process %2)").arg(m_verdict.caller, pid));
    add(tr("Package"), m_verdict.package.isEmpty() ? QString() : m_verdict.origin.isEmpty() ? m_verdict.package : tr("%1, from %2").arg(m_verdict.package, m_verdict.origin));
    add(tr("Publisher"), m_verdict.vendor);
    return list;
}

QVariantList Agent::people() const
{
    QVariantList list;
    for (const Person &who : m_people) {
        list.append(QVariantMap{{QStringLiteral("text"), who.fullName}, {QStringLiteral("value"), who.login}});
    }
    return list;
}

bool Agent::forSelf() const
{
    return m_person < m_people.size() && m_people.at(m_person).uid == getuid();
}

void Agent::demo(const QString &kind)
{
    m_demo = true;
    m_request = {};
    m_request.actionId = kind == QLatin1String("unverified") ? QStringLiteral("org.freedesktop.policykit.exec") : QStringLiteral("org.freedesktop.packagekit.system-update");
    if (kind == QLatin1String("unverified")) {
        m_request.details.insert(QStringLiteral("command_line"), QStringLiteral("/home/alex/Downloads/printer-setup.sh --install"));
        m_request.details.insert(QStringLiteral("polkit.subject-pid"), QStringLiteral("4182"));
        m_verdict.appName = QStringLiteral("printer-setup.sh");
        m_verdict.iconName = QStringLiteral("application-x-shellscript");
        m_verdict.purpose = QStringLiteral("Wants to run printer-setup.sh as administrator, with full access to this computer.");
        m_verdict.why = QStringLiteral("The script isn't part of Wild OS.");
        m_verdict.caller = QStringLiteral("/usr/bin/bash");
        m_trust = QStringLiteral("unverified");
    } else {
        m_request.details.insert(QStringLiteral("polkit.subject-pid"), QStringLiteral("2214"));
        m_verdict.appName = QStringLiteral("Updates");
        m_verdict.iconName = QStringLiteral("wildos-updates");
        m_verdict.purpose = QStringLiteral("Wants to install a security update for the system.");
        m_verdict.caller = QStringLiteral("/usr/bin/wildos-settings");
        m_verdict.package = QStringLiteral("wildos-settings");
        m_verdict.origin = QStringLiteral("Wild OS");
        m_verdict.vendor = QStringLiteral("The PackageKit Project");
        m_trust = QStringLiteral("verified");
    }
    m_people = {{uint(getuid()), QStringLiteral("alex"), QStringLiteral("Alex")}};
    if (kind == QLatin1String("admins")) {
        m_people = {{60001, QStringLiteral("sam"), QStringLiteral("Sam Carter")}, {60002, QStringLiteral("jo"), QStringLiteral("Jo Carter")}};
    }
    m_person = 0;
    m_error = kind == QLatin1String("wrong") ? tr("That password isn't right. Try again.") : QString();
    m_waitSeconds = 0;
    m_active = true;
    ++m_serial;
    Q_EMIT serialChanged();
    Q_EMIT requestChanged();
    Q_EMIT personChanged();
    Q_EMIT stateChanged();
    Q_EMIT activeChanged();
}
