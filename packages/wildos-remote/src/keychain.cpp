#include "keychain.h"
#include "askpass.h"
#include "hostkeys.h"
#include "hosts.h"
#include "paths.h"
#include "secrets.h"

#include <QLocale>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrl>

namespace
{
QString labelsFile()
{
    return Paths::data() + QStringLiteral("/keys.json");
}

constexpr QFileDevice::Permissions ownerOnly = QFileDevice::ReadOwner | QFileDevice::WriteOwner;

// Whether a private key file is protected by a passphrase, from its own
// header: OpenSSH's format names its cipher ("none" when it is not), the
// old PEM format says ENCRYPTED.
bool encrypted(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray text = f.read(64 * 1024);
    if (text.contains("ENCRYPTED")) {
        return true;
    }
    const int begin = text.indexOf("-----BEGIN OPENSSH PRIVATE KEY-----");
    const int end = text.indexOf("-----END OPENSSH PRIVATE KEY-----");
    if (begin < 0 || end < 0) {
        return false;
    }
    QByteArray body = text.mid(begin + 35, end - begin - 35);
    body.replace('\n', "").replace('\r', "");
    const QByteArray blob = QByteArray::fromBase64(body);
    const QByteArray magic("openssh-key-v1\0", 15);
    if (!blob.startsWith(magic) || blob.size() < 19) {
        return false;
    }
    const auto *p = reinterpret_cast<const uchar *>(blob.constData()) + 15;
    const quint32 len = (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | quint32(p[3]);
    return blob.mid(19, int(len)) != "none";
}

bool isPrivateKey(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || f.size() > 64 * 1024) {
        return false;
    }
    return f.read(64).contains("PRIVATE KEY-----");
}

QString typeName(const QString &type)
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
    if (type == QLatin1String("ssh-dss")) {
        return QStringLiteral("DSA");
    }
    return type;
}

// An RSA key's size from its public blob: the modulus is the third field.
int rsaBits(const QString &base64)
{
    const QByteArray blob = QByteArray::fromBase64(base64.toLatin1());
    int at = 0;
    auto field = [&](QByteArray *out) {
        if (at + 4 > blob.size()) {
            return false;
        }
        const auto *p = reinterpret_cast<const uchar *>(blob.constData()) + at;
        const int len = int((quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | quint32(p[3]));
        at += 4;
        if (len < 0 || at + len > blob.size()) {
            return false;
        }
        *out = blob.mid(at, len);
        at += len;
        return true;
    };
    QByteArray type, e, n;
    if (!field(&type) || !field(&e) || !field(&n)) {
        return 0;
    }
    while (!n.isEmpty() && n.at(0) == 0) {
        n.remove(0, 1);
    }
    return n.size() * 8;
}

QString slug(const QString &label)
{
    QString s = label.toLower();
    s.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("_"));
    s = s.mid(0, 32);
    while (s.startsWith(QLatin1Char('_'))) {
        s.remove(0, 1);
    }
    while (s.endsWith(QLatin1Char('_'))) {
        s.chop(1);
    }
    return s.isEmpty() ? QStringLiteral("key") : s;
}

QString uniquePath(const QString &base)
{
    QString path = base;
    for (int n = 2; QFileInfo::exists(path) || QFileInfo::exists(path + QStringLiteral(".pub")); ++n) {
        path = base + QStringLiteral("_%1").arg(n);
    }
    return path;
}
}

Keychain *Keychain::instance()
{
    static Keychain *k = new Keychain;
    return k;
}

Keychain *Keychain::create(QQmlEngine *, QJSEngine *)
{
    Keychain *k = instance();
    QJSEngine::setObjectOwnership(k, QJSEngine::CppOwnership);
    return k;
}

Keychain::Keychain(QObject *parent)
    : QObject(parent)
{
    QFile f(labelsFile());
    if (f.open(QIODevice::ReadOnly)) {
        m_labels = QJsonDocument::fromJson(f.readAll()).object().toVariantMap();
    }
    connect(Hosts::instance(), &Hosts::changed, this, &Keychain::refresh);
    refresh();
}

bool Keychain::agent() const
{
    const QString sock = qEnvironmentVariable("SSH_AUTH_SOCK");
    return !sock.isEmpty() && QFileInfo::exists(sock);
}

void Keychain::writeLabels()
{
    QDir().mkpath(Paths::data());
    QSaveFile f(labelsFile());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(QJsonObject::fromVariantMap(m_labels)).toJson());
        f.commit();
    }
}

void Keychain::call(const QJSValue &callback, const QVariantList &args)
{
    if (!callback.isCallable()) {
        return;
    }
    QJSEngine *engine = qjsEngine(this);
    QJSValueList values;
    for (const QVariant &a : args) {
        values.append(engine ? engine->toScriptValue(a) : QJSValue());
    }
    QJSValue(callback).call(values);
}

QString Keychain::label(const QString &path) const
{
    const QString l = m_labels.value(path).toMap().value(QStringLiteral("label")).toString();
    return l.isEmpty() ? QFileInfo(path).fileName() : l;
}

QVariantMap Keychain::key(const QString &path) const
{
    for (const QVariant &v : m_keys) {
        if (v.toMap().value(QStringLiteral("path")).toString() == path) {
            return v.toMap();
        }
    }
    return {};
}

void Keychain::loadAgent(const std::function<void()> &done)
{
    if (!agent()) {
        m_agentFingerprints.clear();
        done();
        return;
    }
    auto *p = new QProcess(this);
    p->setProgram(QStringLiteral("ssh-add"));
    p->setArguments({QStringLiteral("-l"), QStringLiteral("-E"), QStringLiteral("sha256")});
    connect(p, &QProcess::finished, this, [this, p, done](int, QProcess::ExitStatus) {
        m_agentFingerprints.clear();
        for (const QByteArray &line : p->readAllStandardOutput().split('\n')) {
            const QStringList f = QString::fromUtf8(line).split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (f.size() >= 2 && f[1].startsWith(QLatin1String("SHA256:"))) {
                m_agentFingerprints.insert(f[1]);
            }
        }
        p->deleteLater();
        done();
    });
    p->start();
}

void Keychain::refresh()
{
    loadAgent([this] {
        QVariantList keys;
        const QDir dir(Paths::ssh());
        const QVariantList hosts = Hosts::instance()->hosts();
        for (const QFileInfo &pub : dir.entryInfoList({QStringLiteral("*.pub")}, QDir::Files, QDir::Name)) {
            const QString path = pub.absolutePath() + QLatin1Char('/') + pub.completeBaseName();
            if (!QFileInfo::exists(path) || !isPrivateKey(path)) {
                continue;
            }
            QFile f(pub.absoluteFilePath());
            if (!f.open(QIODevice::ReadOnly)) {
                continue;
            }
            const QString publicKey = QString::fromUtf8(f.readAll()).trimmed();
            const QStringList parts = publicKey.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (parts.size() < 2) {
                continue;
            }
            const QString type = typeName(parts[0]);
            const QString fp = HostKeys::fingerprint(parts[1]);
            QVariantMap k;
            k.insert(QStringLiteral("path"), path);
            k.insert(QStringLiteral("file"), QFileInfo(path).fileName());
            const QVariantMap meta = m_labels.value(path).toMap();
            QString lab = meta.value(QStringLiteral("label")).toString();
            if (lab.isEmpty()) {
                lab = parts.size() > 2 ? parts.mid(2).join(QLatin1Char(' ')) : QFileInfo(path).fileName();
            }
            k.insert(QStringLiteral("label"), lab);
            k.insert(QStringLiteral("type"), type);
            k.insert(QStringLiteral("bits"), type == QLatin1String("RSA") ? rsaBits(parts[1]) : 0);
            k.insert(QStringLiteral("fingerprint"), fp);
            k.insert(QStringLiteral("publicKey"), publicKey);
            const bool sk = type.endsWith(QLatin1String("-SK"));
            k.insert(QStringLiteral("securityKey"), sk);
            k.insert(QStringLiteral("encrypted"), !sk && encrypted(path));
            k.insert(QStringLiteral("inAgent"), m_agentFingerprints.contains(fp));
            k.insert(QStringLiteral("passphraseSaved"), meta.value(QStringLiteral("passphraseSaved")).toBool());
            QDateTime made = QDateTime::fromString(meta.value(QStringLiteral("made")).toString(), Qt::ISODate);
            if (!made.isValid()) {
                made = QFileInfo(path).birthTime().isValid() ? QFileInfo(path).birthTime() : QFileInfo(path).lastModified();
            }
            k.insert(QStringLiteral("made"), QLocale::system().toString(made.date(), QLocale::ShortFormat));
            k.insert(QStringLiteral("legacy"), type == QLatin1String("RSA") || type == QLatin1String("DSA"));
            // Only the hosts set to use this key; a host left to ssh's own
            // choice may be offered any key in the folder.
            QStringList usedBy;
            for (const QVariant &v : hosts) {
                const QVariantMap h = v.toMap();
                if (h.value(QStringLiteral("ssh")).toMap().value(QStringLiteral("identity")).toString() == path) {
                    usedBy.append(h.value(QStringLiteral("name")).toString());
                }
            }
            k.insert(QStringLiteral("usedBy"), usedBy);
            // Fix a key ssh would refuse to use for being readable by others.
            if (QFileInfo(path).permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther | QFileDevice::WriteGroup | QFileDevice::WriteOther)) {
                QFile::setPermissions(path, ownerOnly);
            }
            keys.append(k);
        }
        m_keys = keys;
        Q_EMIT changed();
    });
}

void Keychain::generate(const QString &label, const QString &passphrase, bool savePassphrase, const QJSValue &callback)
{
    const QString name = label.trimmed().isEmpty() ? QStringLiteral("Wild OS") : label.trimmed();
    const QString path = uniquePath(Paths::ssh() + QStringLiteral("/id_ed25519_") + slug(name));
    auto *p = new QProcess(this);
    // The passphrase goes to ssh-keygen through askpass, never as an
    // argument.
    const QString token = AskPass::instance()->newToken([passphrase](const QString &, int) -> std::optional<QString> {
        return passphrase;
    });
    p->setProcessEnvironment(AskPass::instance()->environment(token));
    p->setProgram(QStringLiteral("ssh-keygen"));
    p->setArguments({QStringLiteral("-q"), QStringLiteral("-t"), QStringLiteral("ed25519"), QStringLiteral("-a"), QStringLiteral("100"),
                     QStringLiteral("-C"), name, QStringLiteral("-f"), path});
    p->setStandardInputFile(QProcess::nullDevice());
    connect(p, &QProcess::finished, this, [=, this](int code, QProcess::ExitStatus) {
        AskPass::instance()->release(token);
        const QString err = QString::fromUtf8(p->readAllStandardError()).trimmed();
        p->deleteLater();
        if (code != 0 || !QFileInfo::exists(path)) {
            call(callback, {false, err.isEmpty() ? tr("The key couldn't be made.") : err, QString()});
            return;
        }
        QFile::setPermissions(path, ownerOnly);
        QVariantMap meta;
        meta.insert(QStringLiteral("label"), name);
        meta.insert(QStringLiteral("made"), QDateTime::currentDateTime().toString(Qt::ISODate));
        m_labels.insert(path, meta);
        writeLabels();
        if (savePassphrase && !passphrase.isEmpty()) {
            QFile pub(path + QStringLiteral(".pub"));
            pub.open(QIODevice::ReadOnly);
            const QString fp = HostKeys::fingerprint(QString::fromUtf8(pub.readAll()));
            Secrets::instance()->write(Secrets::passphraseKey(fp), passphrase, [this, path, callback](bool ok) {
                QVariantMap m = m_labels.value(path).toMap();
                m.insert(QStringLiteral("passphraseSaved"), ok);
                m_labels.insert(path, m);
                writeLabels();
                refresh();
                call(callback, {true, ok ? QString() : tr("The key is made, but the keyring didn't take its passphrase."), path});
            });
            return;
        }
        refresh();
        call(callback, {true, QString(), path});
    });
    p->start();
}

void Keychain::importKey(const QUrl &file, const QJSValue &callback)
{
    const QString source = file.toLocalFile();
    if (!isPrivateKey(source)) {
        call(callback, {false, tr("That file isn't an SSH private key."), QString()});
        return;
    }
    const QString path = uniquePath(Paths::ssh() + QLatin1Char('/') + QFileInfo(source).fileName());
    if (!QFile::copy(source, path)) {
        call(callback, {false, tr("The key couldn't be copied into your .ssh folder."), QString()});
        return;
    }
    QFile::setPermissions(path, ownerOnly);
    QVariantMap meta;
    meta.insert(QStringLiteral("label"), QFileInfo(source).fileName());
    meta.insert(QStringLiteral("made"), QFileInfo(source).lastModified().toString(Qt::ISODate));
    m_labels.insert(path, meta);
    writeLabels();
    if (QFileInfo::exists(source + QStringLiteral(".pub"))) {
        QFile::copy(source + QStringLiteral(".pub"), path + QStringLiteral(".pub"));
        refresh();
        call(callback, {true, QString(), path});
        return;
    }
    // Without its public half: ssh-keygen works it out, asking for the
    // passphrase if the key has one.
    auto *p = new QProcess(this);
    const QString token = AskPass::instance()->newToken({}, tr("Import %1").arg(QFileInfo(source).fileName()));
    p->setProcessEnvironment(AskPass::instance()->environment(token));
    p->setStandardInputFile(QProcess::nullDevice());
    p->setProgram(QStringLiteral("ssh-keygen"));
    p->setArguments({QStringLiteral("-y"), QStringLiteral("-f"), path});
    connect(p, &QProcess::finished, this, [=, this](int code, QProcess::ExitStatus) {
        AskPass::instance()->release(token);
        const QByteArray pub = p->readAllStandardOutput();
        p->deleteLater();
        if (code != 0 || pub.isEmpty()) {
            QFile::remove(path);
            m_labels.remove(path);
            writeLabels();
            call(callback, {false, tr("The key couldn't be opened. Check its passphrase."), QString()});
            return;
        }
        QFile out(path + QStringLiteral(".pub"));
        if (out.open(QIODevice::WriteOnly)) {
            out.write(pub);
        }
        refresh();
        call(callback, {true, QString(), path});
    });
    p->start();
}

void Keychain::exportKey(const QString &path, const QUrl &folder, const QJSValue &callback)
{
    const QString dir = folder.toLocalFile();
    const QString target = uniquePath(dir + QLatin1Char('/') + QFileInfo(path).fileName());
    const bool ok = QFile::copy(path, target) && QFile::setPermissions(target, ownerOnly) && QFile::copy(path + QStringLiteral(".pub"), target + QStringLiteral(".pub"));
    call(callback, {ok, ok ? target : tr("The key couldn't be copied there."), target});
}

bool Keychain::remove(const QString &path)
{
    const QVariantMap k = key(path);
    if (k.isEmpty()) {
        return false;
    }
    if (!QFile::moveToTrash(path)) {
        return false;
    }
    QFile::moveToTrash(path + QStringLiteral(".pub"));
    Secrets::instance()->remove(Secrets::passphraseKey(k.value(QStringLiteral("fingerprint")).toString()));
    m_labels.remove(path);
    writeLabels();
    refresh();
    return true;
}

void Keychain::rename(const QString &path, const QString &label)
{
    QVariantMap m = m_labels.value(path).toMap();
    m.insert(QStringLiteral("label"), label.trimmed());
    m_labels.insert(path, m);
    writeLabels();
    refresh();
}

void Keychain::copyPublic(const QString &path)
{
    QGuiApplication::clipboard()->setText(key(path).value(QStringLiteral("publicKey")).toString());
}

void Keychain::changePassphrase(const QString &path, const QString &newPassphrase, const QJSValue &callback)
{
    auto *p = new QProcess(this);
    // ssh-keygen asks for the old passphrase first (only if there is one),
    // then the new one twice; the old one comes from the person.
    const QString token = AskPass::instance()->newToken(
        [newPassphrase](const QString &prompt, int) -> std::optional<QString> {
            if (prompt.contains(QLatin1String("new passphrase"), Qt::CaseInsensitive) || prompt.contains(QLatin1String("same passphrase"), Qt::CaseInsensitive)) {
                return newPassphrase;
            }
            return std::nullopt;
        },
        tr("Change the passphrase of %1").arg(label(path)));
    p->setProcessEnvironment(AskPass::instance()->environment(token));
    p->setStandardInputFile(QProcess::nullDevice());
    p->setProgram(QStringLiteral("ssh-keygen"));
    p->setArguments({QStringLiteral("-q"), QStringLiteral("-p"), QStringLiteral("-a"), QStringLiteral("100"), QStringLiteral("-f"), path});
    connect(p, &QProcess::finished, this, [=, this](int code, QProcess::ExitStatus) {
        AskPass::instance()->release(token);
        p->deleteLater();
        const QVariantMap k = key(path);
        // An old saved passphrase no longer opens it.
        if (code == 0 && k.value(QStringLiteral("passphraseSaved")).toBool()) {
            forgetPassphrase(path);
        }
        refresh();
        call(callback, {code == 0, code == 0 ? QString() : tr("The passphrase wasn't changed.")});
    });
    p->start();
}

void Keychain::savePassphrase(const QString &path, const QString &passphrase, const QJSValue &callback)
{
    auto *p = new QProcess(this);
    const QString token = AskPass::instance()->newToken([passphrase](const QString &, int count) -> std::optional<QString> {
        // Asked a second time means the first was wrong: give up.
        return count == 1 ? std::optional<QString>(passphrase) : std::optional<QString>(QString());
    });
    p->setProcessEnvironment(AskPass::instance()->environment(token));
    p->setStandardInputFile(QProcess::nullDevice());
    p->setProgram(QStringLiteral("ssh-keygen"));
    p->setArguments({QStringLiteral("-y"), QStringLiteral("-f"), path});
    connect(p, &QProcess::finished, this, [=, this](int code, QProcess::ExitStatus) {
        AskPass::instance()->release(token);
        p->deleteLater();
        if (code != 0) {
            call(callback, {false, tr("That passphrase doesn't open this key.")});
            return;
        }
        const QString fp = key(path).value(QStringLiteral("fingerprint")).toString();
        Secrets::instance()->write(Secrets::passphraseKey(fp), passphrase, [this, path, callback](bool ok) {
            QVariantMap m = m_labels.value(path).toMap();
            m.insert(QStringLiteral("passphraseSaved"), ok);
            m_labels.insert(path, m);
            writeLabels();
            refresh();
            call(callback, {ok, ok ? QString() : tr("The keyring didn't take the passphrase.")});
        });
    });
    p->start();
}

void Keychain::forgetPassphrase(const QString &path)
{
    Secrets::instance()->remove(Secrets::passphraseKey(key(path).value(QStringLiteral("fingerprint")).toString()));
    QVariantMap m = m_labels.value(path).toMap();
    m.insert(QStringLiteral("passphraseSaved"), false);
    m_labels.insert(path, m);
    writeLabels();
    refresh();
}

void Keychain::addToAgent(const QString &path, const QJSValue &callback)
{
    if (!agent()) {
        call(callback, {false, tr("No key agent is running in this session.")});
        return;
    }
    const QVariantMap k = key(path);
    auto run = [=, this](const QString &saved) {
        auto *p = new QProcess(this);
        const QString token = AskPass::instance()->newToken(
            [saved](const QString &, int count) -> std::optional<QString> {
                if (!saved.isEmpty() && count == 1) {
                    return saved;
                }
                return std::nullopt;
            },
            tr("Unlock %1").arg(label(path)));
        p->setProcessEnvironment(AskPass::instance()->environment(token));
        p->setStandardInputFile(QProcess::nullDevice());
        p->setProgram(QStringLiteral("ssh-add"));
        p->setArguments({QStringLiteral("--"), path});
        connect(p, &QProcess::finished, this, [=, this](int code, QProcess::ExitStatus) {
            AskPass::instance()->release(token);
            p->deleteLater();
            refresh();
            call(callback, {code == 0, code == 0 ? QString() : tr("The key wasn't added to the agent.")});
        });
        p->start();
    };
    if (k.value(QStringLiteral("passphraseSaved")).toBool()) {
        Secrets::instance()->read(Secrets::passphraseKey(k.value(QStringLiteral("fingerprint")).toString()),
                                  [run](const QString &secret, bool) { run(secret); });
    } else {
        run(QString());
    }
}

void Keychain::prepare(const QString &path, const std::function<void()> &done)
{
    const QVariantMap k = key(path);
    if (k.isEmpty() || !agent() || k.value(QStringLiteral("inAgent")).toBool() || !k.value(QStringLiteral("passphraseSaved")).toBool()) {
        done();
        return;
    }
    Secrets::instance()->read(Secrets::passphraseKey(k.value(QStringLiteral("fingerprint")).toString()), [=, this](const QString &secret, bool found) {
        if (!found) {
            done();
            return;
        }
        auto *p = new QProcess(this);
        const QString token = AskPass::instance()->newToken([secret](const QString &, int count) -> std::optional<QString> {
            return count == 1 ? std::optional<QString>(secret) : std::optional<QString>(QString());
        });
        p->setProcessEnvironment(AskPass::instance()->environment(token));
        p->setStandardInputFile(QProcess::nullDevice());
        p->setProgram(QStringLiteral("ssh-add"));
        p->setArguments({QStringLiteral("--"), path});
        connect(p, &QProcess::finished, this, [=, this](int, QProcess::ExitStatus) {
            AskPass::instance()->release(token);
            p->deleteLater();
            refresh();
            done();
        });
        p->start();
    });
}
