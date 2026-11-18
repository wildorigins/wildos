#include "hostkeys.h"
#include "hosts.h"
#include "paths.h"

#include <QLocale>
#include <QCryptographicHash>
#include <QDate>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageAuthenticationCode>
#include <QProcess>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>

namespace
{
struct Entry {
    QString marker;
    QString patterns;
    QString type;
    QString key;
    int index = -1;
};

QList<Entry> parse(const QString &path)
{
    QList<Entry> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return out;
    }
    int index = 0;
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        const int at = index++;
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        QStringList parts = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        Entry e;
        e.index = at;
        if (!parts.isEmpty() && parts.first().startsWith(QLatin1Char('@'))) {
            e.marker = parts.takeFirst();
        }
        if (parts.size() < 3) {
            continue;
        }
        e.patterns = parts[0];
        e.type = parts[1];
        e.key = parts[2];
        out.append(e);
    }
    return out;
}

bool hashedMatch(const QString &pattern, const QString &name)
{
    // |1|salt|hash, the hash an HMAC-SHA1 of the name keyed by the salt.
    const QStringList p = pattern.split(QLatin1Char('|'));
    if (p.size() != 4 || p[1] != QLatin1String("1")) {
        return false;
    }
    const QByteArray salt = QByteArray::fromBase64(p[2].toLatin1());
    const QByteArray hash = QByteArray::fromBase64(p[3].toLatin1());
    return QMessageAuthenticationCode::hash(name.toUtf8(), salt, QCryptographicHash::Sha1) == hash;
}

bool matches(const QString &patterns, const QString &name)
{
    if (patterns.startsWith(QLatin1String("|1|"))) {
        return hashedMatch(patterns, name);
    }
    bool hit = false;
    for (const QString &raw : patterns.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const bool negated = raw.startsWith(QLatin1Char('!'));
        const QString p = negated ? raw.mid(1) : raw;
        // ssh's patterns know only * and ?; the brackets of [host]:port
        // are literal.
        QString rx = QRegularExpression::escape(p);
        rx.replace(QLatin1String("\\*"), QLatin1String(".*")).replace(QLatin1String("\\?"), QLatin1String("."));
        const QRegularExpression re(QRegularExpression::anchoredPattern(rx), QRegularExpression::CaseInsensitiveOption);
        if (re.match(name).hasMatch()) {
            if (negated) {
                return false;
            }
            hit = true;
        }
    }
    return hit;
}

QString shortType(const QString &type)
{
    if (type == QLatin1String("ssh-ed25519")) {
        return QStringLiteral("ED25519");
    }
    if (type == QLatin1String("sk-ssh-ed25519@openssh.com")) {
        return QStringLiteral("ED25519-SK");
    }
    if (type.startsWith(QLatin1String("sk-ecdsa"))) {
        return QStringLiteral("ECDSA-SK");
    }
    if (type.startsWith(QLatin1String("ecdsa"))) {
        return QStringLiteral("ECDSA");
    }
    if (type == QLatin1String("ssh-rsa")) {
        return QStringLiteral("RSA");
    }
    return type;
}

int preference(const QString &type)
{
    // OpenSSH's own order of preference for host keys.
    if (type == QLatin1String("ssh-ed25519")) {
        return 0;
    }
    if (type.startsWith(QLatin1String("ecdsa"))) {
        return 1;
    }
    return 2;
}

QString datesFile()
{
    return Paths::data() + QStringLiteral("/host-keys-seen.json");
}
}

HostKeys *HostKeys::instance()
{
    static HostKeys *keys = new HostKeys;
    return keys;
}

HostKeys *HostKeys::create(QQmlEngine *, QJSEngine *)
{
    HostKeys *k = instance();
    QJSEngine::setObjectOwnership(k, QJSEngine::CppOwnership);
    return k;
}

HostKeys::HostKeys(QObject *parent)
    : QObject(parent)
{
}

QString HostKeys::knownHostsFile()
{
    return Paths::ssh() + QStringLiteral("/known_hosts");
}

QString HostKeys::entryName(const QString &host, int port)
{
    return port == 22 || port <= 0 ? host : QStringLiteral("[%1]:%2").arg(host).arg(port);
}

QString HostKeys::fingerprint(const QString &keyLine)
{
    const QStringList parts = keyLine.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    for (const QString &p : parts) {
        // The key is the first field that decodes to a blob starting with
        // its own type name.
        const QByteArray blob = QByteArray::fromBase64(p.toLatin1());
        if (blob.size() > 20 && blob.at(0) == 0 && blob.at(1) == 0 && blob.at(2) == 0) {
            const QByteArray hash = QCryptographicHash::hash(blob, QCryptographicHash::Sha256).toBase64(QByteArray::OmitTrailingEquals);
            return QStringLiteral("SHA256:") + QString::fromLatin1(hash);
        }
    }
    return {};
}

void HostKeys::check(const QString &host, int port, const QJSValue &callback)
{
    auto *scan = new QProcess(this);
    scan->setProgram(QStringLiteral("ssh-keyscan"));
    scan->setArguments({QStringLiteral("-T"), QStringLiteral("8"), QStringLiteral("-p"), QString::number(port),
                        QStringLiteral("-t"), QStringLiteral("ed25519,ecdsa,rsa"), QStringLiteral("--"), host});
    connect(scan, &QProcess::finished, this, [this, scan, host, port, callback](int, QProcess::ExitStatus) mutable {
        scan->deleteLater();
        const QString name = entryName(host, port);
        QVariantMap result;
        result.insert(QStringLiteral("name"), name);

        // What the server offers now, best first.
        QList<QPair<QString, QString>> offered;
        for (const QByteArray &raw : scan->readAllStandardOutput().split('\n')) {
            const QStringList f = QString::fromUtf8(raw).trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (f.size() >= 3 && !f[0].startsWith(QLatin1Char('#'))) {
                offered.append({f[1], f[2]});
            }
        }
        std::sort(offered.begin(), offered.end(), [](const auto &a, const auto &b) {
            return preference(a.first) < preference(b.first);
        });
        if (offered.isEmpty()) {
            result.insert(QStringLiteral("state"), QStringLiteral("error"));
            const QString err = QString::fromUtf8(scan->readAllStandardError()).trimmed();
            result.insert(QStringLiteral("error"), err.contains(QLatin1String("Name or service not known")) || err.contains(QLatin1String("not known"))
                                                       ? tr("Remote couldn't find %1 on the network. Check the address.").arg(host)
                                                       : tr("%1 didn't answer on port %2. Check it's switched on and that SSH is running there.").arg(host).arg(port));
            if (callback.isCallable()) {
                callback.call({qjsEngine(this)->toScriptValue(result)});
            }
            return;
        }

        QList<Entry> known = parse(knownHostsFile());
        for (Entry &e : parse(QStringLiteral("/etc/ssh/ssh_known_hosts"))) {
            e.index = -1;
            known.append(e);
        }
        QList<Entry> mine;
        bool revoked = false;
        for (const Entry &e : std::as_const(known)) {
            if (!matches(e.patterns, name)) {
                continue;
            }
            if (e.marker == QLatin1String("@revoked")) {
                for (const auto &o : std::as_const(offered)) {
                    revoked |= o.second == e.key;
                }
                continue;
            }
            if (e.marker.isEmpty()) {
                mine.append(e);
            }
        }

        const auto &best = offered.first();
        result.insert(QStringLiteral("keyType"), shortType(best.first));
        result.insert(QStringLiteral("keyLine"), best.first + QLatin1Char(' ') + best.second);
        result.insert(QStringLiteral("fingerprint"), fingerprint(best.second));
        QString state = QStringLiteral("new");
        if (revoked) {
            state = QStringLiteral("revoked");
        } else {
            for (const Entry &e : std::as_const(mine)) {
                for (const auto &o : std::as_const(offered)) {
                    if (o.first == e.type && o.second == e.key) {
                        state = QStringLiteral("known");
                    }
                }
            }
            if (state != QLatin1String("known") && !mine.isEmpty()) {
                state = QStringLiteral("changed");
                // The new key of the same type as the old one, if there is
                // one: that is the comparison the person can check.
                for (const auto &o : std::as_const(offered)) {
                    if (o.first == mine.first().type) {
                        result.insert(QStringLiteral("keyType"), shortType(o.first));
                        result.insert(QStringLiteral("keyLine"), o.first + QLatin1Char(' ') + o.second);
                        result.insert(QStringLiteral("fingerprint"), fingerprint(o.second));
                        break;
                    }
                }
            }
        }
        result.insert(QStringLiteral("state"), state);
        QVariantList previous;
        for (const Entry &e : std::as_const(mine)) {
            const QString fp = fingerprint(e.key);
            previous.append(QVariantMap{{QStringLiteral("keyType"), shortType(e.type)},
                                        {QStringLiteral("fingerprint"), fp},
                                        {QStringLiteral("seen"), seen(fp)}});
        }
        result.insert(QStringLiteral("previous"), previous);
        if (callback.isCallable()) {
            callback.call({qjsEngine(this)->toScriptValue(result)});
        }
    });
    scan->start();
}

bool HostKeys::append(const QString &line)
{
    QFile f(knownHostsFile());
    if (!f.open(QIODevice::Append | QIODevice::Text)) {
        return false;
    }
    // A file that does not end in a newline would join our entry to its
    // last one.
    if (f.size() > 0) {
        QFile check(knownHostsFile());
        if (check.open(QIODevice::ReadOnly) && check.seek(check.size() - 1) && check.read(1) != "\n") {
            f.write("\n");
        }
    }
    f.write(line.toUtf8() + '\n');
    f.close();
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    m_loaded = false;
    Q_EMIT knownChanged();
    return true;
}

bool HostKeys::trust(const QString &host, int port, const QString &keyLine)
{
    const QStringList k = keyLine.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (k.size() < 2) {
        return false;
    }
    // Hashed as Debian's ssh hashes the names it adds (HashKnownHosts), so
    // the file doesn't list where this PC connects to.
    QByteArray salt(20, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(salt.data()), salt.size() / 4);
    const QByteArray hash = QMessageAuthenticationCode::hash(entryName(host, port).toUtf8(), salt, QCryptographicHash::Sha1);
    const QString hashed = QStringLiteral("|1|%1|%2").arg(QString::fromLatin1(salt.toBase64()), QString::fromLatin1(hash.toBase64()));
    if (!append(hashed + QLatin1Char(' ') + k[0] + QLatin1Char(' ') + k[1])) {
        return false;
    }
    remember(fingerprint(k[1]));
    return true;
}

bool HostKeys::replace(const QString &host, int port, const QString &keyLine)
{
    const QString name = entryName(host, port);
    QFile f(knownHostsFile());
    if (f.open(QIODevice::ReadOnly)) {
        QStringList kept;
        const QList<QByteArray> lines = f.readAll().split('\n');
        f.close();
        for (const QByteArray &raw : lines) {
            const QString line = QString::fromUtf8(raw);
            QStringList parts = line.trimmed().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
            if (!parts.isEmpty() && parts.first().startsWith(QLatin1Char('@'))) {
                // Revocations and certificate authorities are kept.
                kept.append(line);
                continue;
            }
            if (!parts.isEmpty() && !line.trimmed().startsWith(QLatin1Char('#')) && matches(parts.first(), name)) {
                continue;
            }
            kept.append(line);
        }
        while (!kept.isEmpty() && kept.last().trimmed().isEmpty()) {
            kept.removeLast();
        }
        QSaveFile out(knownHostsFile());
        if (!out.open(QIODevice::WriteOnly)) {
            return false;
        }
        out.write(kept.join(QLatin1Char('\n')).toUtf8());
        if (!kept.isEmpty()) {
            out.write("\n");
        }
        if (!out.commit()) {
            return false;
        }
    }
    return trust(host, port, keyLine);
}

void HostKeys::remember(const QString &fp)
{
    QFile f(datesFile());
    QJsonObject dates;
    if (f.open(QIODevice::ReadOnly)) {
        dates = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
    }
    dates.insert(fp, QDate::currentDate().toString(Qt::ISODate));
    QDir().mkpath(Paths::data());
    QSaveFile out(datesFile());
    if (out.open(QIODevice::WriteOnly)) {
        out.write(QJsonDocument(dates).toJson());
        out.commit();
    }
}

QString HostKeys::seen(const QString &fp) const
{
    QFile f(datesFile());
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QString iso = QJsonDocument::fromJson(f.readAll()).object().value(fp).toString();
    const QDate d = QDate::fromString(iso, Qt::ISODate);
    return d.isValid() ? QLocale::system().toString(d, QLocale::ShortFormat) : QString();
}

void HostKeys::reload()
{
    m_loaded = false;
    Q_EMIT knownChanged();
}

QVariantList HostKeys::known()
{
    if (m_loaded) {
        return m_known;
    }
    m_loaded = true;
    m_known.clear();
    // The names a hashed entry could be: every saved terminal host.
    QStringList names;
    for (const QVariant &v : Hosts::instance()->hosts()) {
        const QVariantMap h = v.toMap();
        if (h.value(QStringLiteral("kind")).toString() == QLatin1String("terminal")) {
            names.append(entryName(Hosts::instance()->hostName(h), Hosts::instance()->port(h)));
        }
    }
    for (const Entry &e : parse(knownHostsFile())) {
        QVariantMap m;
        m.insert(QStringLiteral("index"), e.index);
        const bool hashed = e.patterns.startsWith(QLatin1String("|1|"));
        QString name = e.patterns;
        if (hashed) {
            name.clear();
            for (const QString &n : std::as_const(names)) {
                if (hashedMatch(e.patterns, n)) {
                    name = n;
                    break;
                }
            }
        }
        m.insert(QStringLiteral("name"), name);
        m.insert(QStringLiteral("hashed"), hashed);
        m.insert(QStringLiteral("marker"), e.marker);
        m.insert(QStringLiteral("keyType"), shortType(e.type));
        const QString fp = fingerprint(e.key);
        m.insert(QStringLiteral("fingerprint"), fp);
        m.insert(QStringLiteral("seen"), seen(fp));
        m_known.append(m);
    }
    return m_known;
}

bool HostKeys::forget(int index)
{
    QFile f(knownHostsFile());
    if (!f.open(QIODevice::ReadOnly)) {
        return false;
    }
    QList<QByteArray> lines = f.readAll().split('\n');
    f.close();
    if (index < 0 || index >= lines.size()) {
        return false;
    }
    lines.removeAt(index);
    QSaveFile out(knownHostsFile());
    if (!out.open(QIODevice::WriteOnly)) {
        return false;
    }
    out.write(lines.join('\n'));
    if (!out.commit()) {
        return false;
    }
    reload();
    return true;
}
