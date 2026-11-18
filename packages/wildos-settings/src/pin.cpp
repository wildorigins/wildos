#include "pin.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QFile>
#include <QLocalSocket>
#include <QRegularExpression>
#include <QTimer>

#include <sys/socket.h>
namespace
{
const QString socketPath = QStringLiteral("/run/wildos/pin.socket");

// The system keeps the key to the saved passwords of an account with a
// PIN, since a PIN sign-in types no password to make it from
// (wildos-setup's wildos-wallet-keys). Not waited for: without the key the
// PIN still signs in, and the session then asks for the password once.
void walletKeys(const QString &method, const QString &password = QString())
{
    const QString name = QStringLiteral("org.wildos.WalletKeys1");
    QDBusMessage call = QDBusMessage::createMethodCall(name, QStringLiteral("/org/wildos/WalletKeys1"), name, method);
    if (!password.isNull()) {
        call << password;
    }
    QDBusConnection::systemBus().asyncCall(call, 60000);
}
}

Pin::Pin(QObject *parent)
    : QObject(parent)
{
    m_available = QFile::exists(socketPath);
    refresh();
}

bool Pin::valid(const QString &pin) const
{
    static const QRegularExpression digits(QStringLiteral("^[0-9]{4,12}$"));
    return digits.match(pin).hasMatch();
}

// One request, one answer: the lines go out, this side closes, and
// whatever comes back before the service closes is the answer. An empty
// answer means the service could not be reached.
void Pin::ask(const QStringList &lines, std::function<void(const QString &)> answered)
{
    auto *socket = new QLocalSocket(this);
    auto *limit = new QTimer(socket);
    auto finished = std::make_shared<bool>(false);
    auto finish = [socket, finished, answered] {
        if (*finished) {
            return;
        }
        *finished = true;
        const QString reply = QString::fromUtf8(socket->readAll()).trimmed();
        socket->deleteLater();
        answered(reply);
    };
    connect(socket, &QLocalSocket::connected, socket, [socket, lines] {
        QByteArray request = lines.join(QLatin1Char('\n')).toUtf8();
        socket->write(request);
        socket->waitForBytesWritten(2000);
        request.fill('\0');
        // Closing the sending half is what tells the service the request
        // is whole; the answer still comes back on the other half.
        ::shutdown(socket->socketDescriptor(), SHUT_WR);
    });
    connect(socket, &QLocalSocket::disconnected, socket, finish);
    connect(socket, &QLocalSocket::errorOccurred, socket, [socket, finish](QLocalSocket::LocalSocketError error) {
        // The service closing after its answer is the normal end.
        if (error != QLocalSocket::PeerClosedError || socket->state() == QLocalSocket::UnconnectedState) {
            finish();
        }
    });
    // A wrong password waits two seconds and the hash takes a moment.
    limit->setSingleShot(true);
    connect(limit, &QTimer::timeout, socket, finish);
    limit->start(20000);
    socket->connectToServer(socketPath);
}

void Pin::refresh()
{
    if (!m_available) {
        return;
    }
    ask({QStringLiteral("status")}, [this](const QString &reply) {
        const bool set = reply == QLatin1String("set");
        if (set != m_set) {
            m_set = set;
            Q_EMIT changed();
        }
    });
}

void Pin::save(const QString &password, const QString &pin)
{
    if (m_busy || !m_available) {
        return;
    }
    if (!valid(pin)) {
        Q_EMIT failed(tr("A PIN is 4 to 12 digits."));
        return;
    }
    // The request is one line each, so a password holding a line break
    // could never match anyway.
    if (password.isEmpty() || password.contains(QLatin1Char('\n'))) {
        Q_EMIT failed(tr("That password is not right."));
        return;
    }
    m_busy = true;
    Q_EMIT changed();
    ask({QStringLiteral("set"), password, pin}, [this, password](const QString &reply) {
        m_busy = false;
        if (reply == QLatin1String("ok")) {
            walletKeys(QStringLiteral("Store"), password);
            m_set = true;
            Q_EMIT changed();
            Q_EMIT done(QStringLiteral("set"));
            return;
        }
        Q_EMIT changed();
        if (reply == QLatin1String("password")) {
            Q_EMIT failed(tr("That password is not right."));
        } else if (reply == QLatin1String("pin")) {
            Q_EMIT failed(tr("A PIN is 4 to 12 digits."));
        } else {
            Q_EMIT failed(tr("Wild OS could not save the PIN. Try again in a moment."));
        }
    });
}

void Pin::remove()
{
    if (m_busy || !m_available) {
        return;
    }
    m_busy = true;
    Q_EMIT changed();
    ask({QStringLiteral("remove")}, [this](const QString &reply) {
        m_busy = false;
        if (reply == QLatin1String("ok")) {
            walletKeys(QStringLiteral("Tidy"));
            m_set = false;
            Q_EMIT changed();
            Q_EMIT done(QStringLiteral("removed"));
            return;
        }
        Q_EMIT changed();
        Q_EMIT failed(tr("Wild OS could not remove the PIN. Try again in a moment."));
    });
}
