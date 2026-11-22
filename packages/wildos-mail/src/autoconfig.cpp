#include "autoconfig.h"

#include <QDnsLookup>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrl>
#include <QXmlStreamReader>

namespace
{
QString domainOf(const QString &email)
{
    return email.section(QLatin1Char('@'), 1).trimmed().toLower();
}

QVariantMap settings(const QString &provider, const QString &imapHost, int imapPort, const QString &imapSecurity, const QString &smtpHost, int smtpPort,
                     const QString &smtpSecurity)
{
    return {
        {QStringLiteral("provider"), provider},
        {QStringLiteral("imapHost"), imapHost},
        {QStringLiteral("imapPort"), imapPort},
        {QStringLiteral("imapSecurity"), imapSecurity},
        {QStringLiteral("smtpHost"), smtpHost},
        {QStringLiteral("smtpPort"), smtpPort},
        {QStringLiteral("smtpSecurity"), smtpSecurity},
    };
}

QString securityOf(const QString &socketType)
{
    const QString s = socketType.toUpper();
    if (s == QLatin1String("SSL")) {
        return QStringLiteral("ssl");
    }
    if (s == QLatin1String("STARTTLS")) {
        return QStringLiteral("starttls");
    }
    return QStringLiteral("none");
}

bool isLoopback(const QString &host)
{
    const QString h = host.trimmed().toLower();
    if (h == QLatin1String("localhost") || h.endsWith(QLatin1String(".localhost"))) {
        return true;
    }
    QHostAddress address;
    return address.setAddress(h.startsWith(QLatin1Char('[')) ? h.mid(1, h.size() - 2) : h) && address.isLoopback();
}

// Thunderbird's autoconfig XML: the first IMAP server and the first SMTP
// server, with %EMAILADDRESS% and friends filled in.
QVariantMap parseConfig(const QByteArray &xml, const QString &email)
{
    QXmlStreamReader reader(xml);
    QVariantMap out;
    QString section;
    QVariantMap current;
    const auto fill = [&](QString value) {
        value.replace(QLatin1String("%EMAILADDRESS%"), email);
        value.replace(QLatin1String("%EMAILLOCALPART%"), email.section(QLatin1Char('@'), 0, 0));
        value.replace(QLatin1String("%EMAILDOMAIN%"), domainOf(email));
        return value;
    };
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            const QString name = reader.name().toString();
            if (name == QLatin1String("incomingServer") || name == QLatin1String("outgoingServer")) {
                section = reader.attributes().value(QLatin1String("type")).toString();
                current.clear();
            } else if (!section.isEmpty() && (name == QLatin1String("hostname") || name == QLatin1String("port") || name == QLatin1String("socketType")
                                              || name == QLatin1String("username"))) {
                current.insert(name, fill(reader.readElementText()));
            }
        } else if (reader.isEndElement()) {
            const QString name = reader.name().toString();
            if ((name == QLatin1String("incomingServer") && section == QLatin1String("imap") && !out.contains(QStringLiteral("imapHost")))) {
                out.insert(QStringLiteral("imapHost"), current.value(QStringLiteral("hostname")));
                out.insert(QStringLiteral("imapPort"), current.value(QStringLiteral("port")).toInt());
                out.insert(QStringLiteral("imapSecurity"), securityOf(current.value(QStringLiteral("socketType")).toString()));
                out.insert(QStringLiteral("user"), current.value(QStringLiteral("username")));
            } else if (name == QLatin1String("outgoingServer") && section == QLatin1String("smtp") && !out.contains(QStringLiteral("smtpHost"))) {
                out.insert(QStringLiteral("smtpHost"), current.value(QStringLiteral("hostname")));
                out.insert(QStringLiteral("smtpPort"), current.value(QStringLiteral("port")).toInt());
                out.insert(QStringLiteral("smtpSecurity"), securityOf(current.value(QStringLiteral("socketType")).toString()));
            }
            if (name == QLatin1String("incomingServer") || name == QLatin1String("outgoingServer")) {
                section.clear();
            }
        }
    }
    if (!out.contains(QStringLiteral("imapHost")) || !out.contains(QStringLiteral("smtpHost"))) {
        return {};
    }
    // A server on this PC means a local helper app (Proton Mail Bridge is
    // one) that is likely not installed; offered as found, the sign-in only
    // fails with "connection refused", so the person picks the servers.
    if (isLoopback(out.value(QStringLiteral("imapHost")).toString()) || isLoopback(out.value(QStringLiteral("smtpHost")).toString())) {
        return {};
    }
    out.insert(QStringLiteral("provider"), QStringLiteral("other"));
    return out;
}
}

AutoConfig::AutoConfig(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
    m_network->setCookieJar(nullptr);
}

QString AutoConfig::providerOf(const QString &email)
{
    const QString domain = domainOf(email);
    if (domain == QLatin1String("gmail.com") || domain == QLatin1String("googlemail.com")) {
        return QStringLiteral("gmail");
    }
    if (domain == QLatin1String("icloud.com") || domain == QLatin1String("me.com") || domain == QLatin1String("mac.com")) {
        return QStringLiteral("icloud");
    }
    if (domain.startsWith(QLatin1String("yahoo.")) || domain.startsWith(QLatin1String("ymail.")) || domain == QLatin1String("rocketmail.com")) {
        return QStringLiteral("yahoo");
    }
    if (domain.startsWith(QLatin1String("outlook.")) || domain.startsWith(QLatin1String("hotmail.")) || domain.startsWith(QLatin1String("live."))
        || domain == QLatin1String("msn.com")) {
        return QStringLiteral("outlook");
    }
    if (domain == QLatin1String("aol.com")) {
        return QStringLiteral("aol");
    }
    if (domain == QLatin1String("fastmail.com") || domain == QLatin1String("fastmail.fm")) {
        return QStringLiteral("fastmail");
    }
    if (domain == QLatin1String("proton.me") || domain == QLatin1String("protonmail.com") || domain == QLatin1String("protonmail.ch")
        || domain == QLatin1String("pm.me")) {
        return QStringLiteral("proton");
    }
    return QStringLiteral("other");
}

QVariantMap AutoConfig::preset(const QString &provider, const QString &email)
{
    QVariantMap s;
    if (provider == QLatin1String("gmail")) {
        s = settings(provider, QStringLiteral("imap.gmail.com"), 993, QStringLiteral("ssl"), QStringLiteral("smtp.gmail.com"), 465, QStringLiteral("ssl"));
    } else if (provider == QLatin1String("icloud")) {
        s = settings(provider, QStringLiteral("imap.mail.me.com"), 993, QStringLiteral("ssl"), QStringLiteral("smtp.mail.me.com"), 587, QStringLiteral("starttls"));
    } else if (provider == QLatin1String("yahoo")) {
        s = settings(provider, QStringLiteral("imap.mail.yahoo.com"), 993, QStringLiteral("ssl"), QStringLiteral("smtp.mail.yahoo.com"), 465, QStringLiteral("ssl"));
    } else if (provider == QLatin1String("fastmail")) {
        s = settings(provider, QStringLiteral("imap.fastmail.com"), 993, QStringLiteral("ssl"), QStringLiteral("smtp.fastmail.com"), 465, QStringLiteral("ssl"));
    } else if (provider == QLatin1String("aol")) {
        s = settings(provider, QStringLiteral("imap.aol.com"), 993, QStringLiteral("ssl"), QStringLiteral("smtp.aol.com"), 465, QStringLiteral("ssl"));
    } else if (provider == QLatin1String("outlook")) {
        s = settings(provider, QStringLiteral("outlook.office365.com"), 993, QStringLiteral("ssl"), QStringLiteral("smtp.office365.com"), 587, QStringLiteral("starttls"));
    } else {
        return {};
    }
    s.insert(QStringLiteral("email"), email);
    s.insert(QStringLiteral("user"), email);
    return s;
}

void AutoConfig::lookup(const QString &email)
{
    ++m_generation;
    const QString domain = domainOf(email);
    if (domain.isEmpty()) {
        return;
    }
    m_busy = true;
    Q_EMIT busyChanged();
    const QString provider = providerOf(email);
    // Proton has no server Mail can sign in to, only Proton Mail Bridge on
    // the PC, so there are no settings to find; the dialog explains instead.
    if (provider == QLatin1String("proton")) {
        finish({{QStringLiteral("provider"), provider}, {QStringLiteral("email"), email}}, false);
        return;
    }
    if (provider != QLatin1String("other")) {
        finish(preset(provider, email), false);
        return;
    }
    tryUrl({QStringLiteral("https://autoconfig.%1/mail/config-v1.1.xml").arg(domain),
            QStringLiteral("https://%1/.well-known/autoconfig/mail/config-v1.1.xml").arg(domain),
            QStringLiteral("https://autoconfig.thunderbird.net/v1.1/%1").arg(domain)},
           email);
}

void AutoConfig::tryUrl(const QStringList &urls, const QString &email)
{
    if (urls.isEmpty()) {
        tryMx(email);
        return;
    }
    const int generation = m_generation;
    QNetworkRequest request{QUrl(urls.first())};
    request.setTransferTimeout(6000);
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, urls, email, generation]() {
        reply->deleteLater();
        if (generation != m_generation) {
            return;
        }
        QVariantMap s = reply->error() == QNetworkReply::NoError ? parseConfig(reply->readAll(), email) : QVariantMap();
        if (!s.isEmpty()) {
            s.insert(QStringLiteral("email"), email);
            if (s.value(QStringLiteral("user")).toString().isEmpty()) {
                s.insert(QStringLiteral("user"), email);
            }
            finish(s, false);
            return;
        }
        tryUrl(urls.mid(1), email);
    });
}

// Mail for many company and custom domains is run by Google or Microsoft;
// the domain's mail server record says which.
void AutoConfig::tryMx(const QString &email)
{
    const int generation = m_generation;
    auto *dns = new QDnsLookup(QDnsLookup::MX, domainOf(email), this);
    connect(dns, &QDnsLookup::finished, this, [this, dns, email, generation]() {
        dns->deleteLater();
        if (generation != m_generation) {
            return;
        }
        QString mx;
        if (dns->error() == QDnsLookup::NoError && !dns->mailExchangeRecords().isEmpty()) {
            mx = dns->mailExchangeRecords().first().exchange().toLower();
        }
        if (mx.endsWith(QLatin1String("google.com")) || mx.endsWith(QLatin1String("googlemail.com"))) {
            finish(preset(QStringLiteral("gmail"), email), false);
            return;
        }
        if (mx.endsWith(QLatin1String("outlook.com"))) {
            finish(preset(QStringLiteral("outlook"), email), false);
            return;
        }
        // Custom domains on Proton, whose mail is no more reachable than an
        // @proton.me address.
        if (mx.endsWith(QLatin1String("protonmail.ch")) || mx.endsWith(QLatin1String("proton.me"))) {
            finish({{QStringLiteral("provider"), QStringLiteral("proton")}, {QStringLiteral("email"), email}}, false);
            return;
        }
        const QString domain = domainOf(email);
        QVariantMap s = settings(QStringLiteral("other"), QStringLiteral("imap.") + domain, 993, QStringLiteral("ssl"), QStringLiteral("smtp.") + domain, 465,
                                 QStringLiteral("ssl"));
        s.insert(QStringLiteral("email"), email);
        s.insert(QStringLiteral("user"), email);
        finish(s, true);
    });
    dns->lookup();
}

void AutoConfig::finish(const QVariantMap &settings, bool guessed)
{
    m_busy = false;
    Q_EMIT busyChanged();
    Q_EMIT found(settings, guessed);
}
