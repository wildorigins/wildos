#include "accounts.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <cstdio>
#include <QStandardPaths>
#include <QUuid>

#include <qt6keychain/keychain.h>

namespace
{
const QString keychainService = QStringLiteral("Wild OS Mail");

QString passwordFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/passwords.json");
}

QJsonObject readFallback()
{
    QFile file(passwordFile());
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
}

// Written to a new file that is only ever readable by this account (it is
// created that way, never loosened and tightened), then renamed over the old
// one, so there is no moment with a half-written or readable file.
void writeFallback(const QJsonObject &passwords)
{
    const QString path = passwordFile();
    if (passwords.isEmpty()) {
        QFile::remove(path);
        return;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    const QString temp = path + QStringLiteral(".new");
    QFile::remove(temp);
    QFile file(temp);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly, QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        return;
    }
    file.write(QJsonDocument(passwords).toJson(QJsonDocument::Compact));
    file.flush();
    file.close();
    ::rename(QFile::encodeName(temp).constData(), QFile::encodeName(path).constData());
}

QString accountsFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QStringLiteral("/accounts.json");
}
}

QVariantMap Account::toMap() const
{
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), name},
        {QStringLiteral("email"), email},
        {QStringLiteral("fullName"), fullName},
        {QStringLiteral("provider"), provider},
        {QStringLiteral("imapHost"), imapHost},
        {QStringLiteral("imapPort"), imapPort},
        {QStringLiteral("imapSecurity"), imapSecurity},
        {QStringLiteral("smtpHost"), smtpHost},
        {QStringLiteral("smtpPort"), smtpPort},
        {QStringLiteral("smtpSecurity"), smtpSecurity},
        {QStringLiteral("user"), user},
        {QStringLiteral("auth"), auth},
        {QStringLiteral("keepAll"), keepAll},
        {QStringLiteral("signature"), signature},
    };
}

Account Account::fromMap(const QVariantMap &map)
{
    Account a;
    a.id = map.value(QStringLiteral("id")).toString();
    a.name = map.value(QStringLiteral("name")).toString();
    a.email = map.value(QStringLiteral("email")).toString();
    a.fullName = map.value(QStringLiteral("fullName")).toString();
    a.provider = map.value(QStringLiteral("provider"), QStringLiteral("other")).toString();
    a.imapHost = map.value(QStringLiteral("imapHost")).toString();
    a.imapPort = map.value(QStringLiteral("imapPort"), 993).toInt();
    a.imapSecurity = map.value(QStringLiteral("imapSecurity"), QStringLiteral("ssl")).toString();
    a.smtpHost = map.value(QStringLiteral("smtpHost")).toString();
    a.smtpPort = map.value(QStringLiteral("smtpPort"), 465).toInt();
    a.smtpSecurity = map.value(QStringLiteral("smtpSecurity"), QStringLiteral("ssl")).toString();
    a.user = map.value(QStringLiteral("user")).toString();
    a.auth = map.value(QStringLiteral("auth"), QStringLiteral("password")).toString();
    a.keepAll = map.value(QStringLiteral("keepAll"), true).toBool();
    a.signature = map.value(QStringLiteral("signature")).toString();
    if (a.name.isEmpty()) {
        a.name = a.email;
    }
    if (a.user.isEmpty()) {
        a.user = a.email;
    }
    return a;
}

AccountStore::AccountStore(QObject *parent)
    : QObject(parent)
{
    load();
}

const Account *AccountStore::find(const QString &id) const
{
    for (const Account &a : m_accounts) {
        if (a.id == id) {
            return &a;
        }
    }
    return nullptr;
}

void AccountStore::add(const Account &account)
{
    m_accounts.append(account);
    save();
    Q_EMIT changed();
}

void AccountStore::update(const Account &account)
{
    for (Account &a : m_accounts) {
        if (a.id == account.id) {
            a = account;
        }
    }
    save();
    Q_EMIT changed();
}

void AccountStore::remove(const QString &id)
{
    m_accounts.removeIf([&](const Account &a) {
        return a.id == id;
    });
    save();
    QJsonObject fallback = readFallback();
    fallback.remove(id);
    writeFallback(fallback);
    if (!walletExists()) {
        Q_EMIT changed();
        return;
    }
    auto *job = new QKeychain::DeletePasswordJob(keychainService);
    job->setKey(id);
    job->start();
    Q_EMIT changed();
}

void AccountStore::move(const QString &id, int to)
{
    int from = -1;
    for (int i = 0; i < m_accounts.size(); ++i) {
        if (m_accounts.at(i).id == id) {
            from = i;
        }
    }
    if (from < 0) {
        return;
    }
    to = qBound(0, to, int(m_accounts.size()) - 1);
    if (from == to) {
        return;
    }
    m_accounts.move(from, to);
    save();
    Q_EMIT changed();
}

bool AccountStore::walletExists()
{
    return QFile::exists(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kwalletd/kdewallet.kwl"));
}

void AccountStore::readPassword(const QString &id, std::function<void(const QString &)> done)
{
    QJsonObject fallback = readFallback();
    if (fallback.contains(id)) {
        const QString password = QString::fromUtf8(QByteArray::fromBase64(fallback.value(id).toString().toLatin1()));
        // A wallet exists now: the password moves into it, and the file
        // copy goes once the wallet has it.
        if (walletExists()) {
            writePassword(id, password);
        }
        done(password);
        return;
    }
    if (!walletExists()) {
        done({});
        return;
    }
    auto *job = new QKeychain::ReadPasswordJob(keychainService, this);
    job->setKey(id);
    connect(job, &QKeychain::Job::finished, this, [job, done]() {
        done(job->error() == QKeychain::NoError ? job->textData() : QString());
    });
    job->start();
}

void AccountStore::writePassword(const QString &id, const QString &password, std::function<void(bool)> done)
{
    if (!walletExists()) {
        QJsonObject fallback = readFallback();
        fallback.insert(id, QString::fromLatin1(password.toUtf8().toBase64()));
        writeFallback(fallback);
        if (done) {
            done(true);
        }
        return;
    }
    auto *job = new QKeychain::WritePasswordJob(keychainService, this);
    job->setKey(id);
    job->setTextData(password);
    connect(job, &QKeychain::Job::finished, this, [job, id, done]() {
        const bool ok = job->error() == QKeychain::NoError;
        // Safely in the wallet: no copy stays in the file.
        if (ok) {
            QJsonObject fallback = readFallback();
            if (fallback.contains(id)) {
                fallback.remove(id);
                writeFallback(fallback);
            }
        }
        if (done) {
            done(ok);
        }
    });
    job->start();
}

QString AccountStore::newId()
{
    return QUuid::createUuid().toString(QUuid::Id128).left(12);
}

void AccountStore::load()
{
    QFile file(accountsFile());
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonArray list = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("accounts")).toArray();
    for (const QJsonValue &value : list) {
        m_accounts.append(Account::fromMap(value.toObject().toVariantMap()));
    }
}

void AccountStore::save() const
{
    QDir().mkpath(QFileInfo(accountsFile()).absolutePath());
    QJsonArray list;
    for (const Account &a : m_accounts) {
        list.append(QJsonObject::fromVariantMap(a.toMap()));
    }
    QSaveFile file(accountsFile());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(QJsonObject{{QStringLiteral("accounts"), list}}).toJson());
        file.commit();
    }
}
