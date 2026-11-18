#include "sftp.h"
#include "askpass.h"

#include <QFile>
#include <QSharedPointer>
#include <QtEndian>

namespace
{
enum : quint8 {
    FxpInit = 1,
    FxpVersion = 2,
    FxpOpen = 3,
    FxpClose = 4,
    FxpRead = 5,
    FxpWrite = 6,
    FxpLstat = 7,
    FxpOpendir = 11,
    FxpReaddir = 12,
    FxpRemove = 13,
    FxpMkdir = 14,
    FxpRmdir = 15,
    FxpRealpath = 16,
    FxpStat = 17,
    FxpRename = 18,
    FxpStatus = 101,
    FxpHandle = 102,
    FxpData = 103,
    FxpName = 104,
    FxpAttrs = 105,
};

enum : quint32 {
    FlagRead = 0x01,
    FlagWrite = 0x02,
    FlagCreate = 0x08,
    FlagTruncate = 0x10,
};

enum : quint32 {
    AttrSize = 0x1,
    AttrUidGid = 0x2,
    AttrPermissions = 0x4,
    AttrTimes = 0x8,
    AttrExtended = 0x80000000,
};

// How much is read or written per request, and how many requests are in
// flight at once: enough to keep a link busy without a reply per chunk.
constexpr quint32 chunk = 32 * 1024;
constexpr int window = 16;

void putU32(QByteArray &b, quint32 v)
{
    char d[4];
    qToBigEndian(v, d);
    b.append(d, 4);
}

void putU64(QByteArray &b, quint64 v)
{
    char d[8];
    qToBigEndian(v, d);
    b.append(d, 8);
}

void putString(QByteArray &b, const QByteArray &s)
{
    putU32(b, quint32(s.size()));
    b.append(s);
}

struct Reader {
    const QByteArray &data;
    int at = 0;
    bool ok = true;

    quint32 u32()
    {
        if (at + 4 > data.size()) {
            ok = false;
            return 0;
        }
        const quint32 v = qFromBigEndian<quint32>(data.constData() + at);
        at += 4;
        return v;
    }
    quint64 u64()
    {
        if (at + 8 > data.size()) {
            ok = false;
            return 0;
        }
        const quint64 v = qFromBigEndian<quint64>(data.constData() + at);
        at += 8;
        return v;
    }
    QByteArray string()
    {
        const quint32 n = u32();
        if (!ok || at + int(n) > data.size()) {
            ok = false;
            return {};
        }
        const QByteArray s = data.mid(at, int(n));
        at += int(n);
        return s;
    }
    Sftp::Entry attrs()
    {
        Sftp::Entry e;
        const quint32 flags = u32();
        if (flags & AttrSize) {
            e.size = qint64(u64());
        }
        if (flags & AttrUidGid) {
            u32();
            u32();
        }
        if (flags & AttrPermissions) {
            e.permissions = u32();
            e.dir = (e.permissions & 0170000) == 0040000;
            e.link = (e.permissions & 0170000) == 0120000;
        }
        if (flags & AttrTimes) {
            u32();
            e.mtime = qint64(u32());
        }
        if (flags & AttrExtended) {
            const quint32 n = u32();
            for (quint32 i = 0; i < n && ok; ++i) {
                string();
                string();
            }
        }
        return e;
    }
};

QByteArray pathBytes(const QString &path)
{
    return path.toUtf8();
}
}

Sftp::Sftp(QObject *parent)
    : QObject(parent)
{
}

Sftp::~Sftp()
{
    stop();
}

void Sftp::start(const QStringList &sshArgs, const std::optional<QString> &password)
{
    stop();
    m_process = new QProcess(this);
    m_process->setProgram(QStringLiteral("ssh"));
    QStringList args = sshArgs;
    if (password) {
        // The saved password answers ssh's first password prompt, once.
        // Anything else it asks (a one-time code, a key's passphrase, the
        // password again) is turned down, never shown.
        AskPass::Options options;
        options.askPerson = false;
        options.provider = [secret = *password, given = false](const QString &prompt, int, const std::function<void(const AskPass::Lookup &)> &done) mutable {
            if (!given && AskPass::isPassword(prompt)) {
                given = true;
                done({secret, {}});
            } else {
                done({});
            }
        };
        m_askToken = AskPass::instance()->newToken(options);
        m_process->setProcessEnvironment(AskPass::instance()->environment(m_askToken));
    } else {
        // Never a prompt: the terminal's connection is used, or nothing.
        args.prepend(QStringLiteral("BatchMode=yes"));
        args.prepend(QStringLiteral("-o"));
    }
    // -s is an option, so it goes before the destination (the last of
    // sshArgs, after its "--"); the subsystem's name comes after it, where
    // a command would.
    const qsizetype end = args.lastIndexOf(QStringLiteral("--"));
    args.insert(end >= 0 ? end : args.size() - 1, QStringLiteral("-s"));
    args.append(QStringLiteral("sftp"));
    m_process->setArguments(args);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &Sftp::readyRead);
    connect(m_process, &QProcess::readyReadStandardError, this, [this] {
        m_stderr += QString::fromUtf8(m_process->readAllStandardError());
    });
    connect(m_process, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
        const bool was = m_ready;
        m_ready = false;
        const auto pending = std::exchange(m_pending, {});
        for (const Reply &r : pending) {
            QByteArray status;
            putU32(status, 4);
            putString(status, tr("The connection to the server ended").toUtf8());
            putString(status, "");
            r(FxpStatus, status);
        }
        if (was) {
            Q_EMIT readyChanged();
        }
        Q_EMIT failed(m_stderr.trimmed().isEmpty() ? tr("The file connection ended.") : m_stderr.trimmed());
    });
    m_process->start();
    QByteArray init;
    init.append(char(FxpInit));
    putU32(init, 3);
    QByteArray framed;
    putU32(framed, quint32(init.size()));
    framed.append(init);
    m_process->write(framed);
}

void Sftp::stop()
{
    if (!m_process) {
        return;
    }
    m_process->disconnect(this);
    m_process->closeWriteChannel();
    if (!m_process->waitForFinished(500)) {
        m_process->kill();
        m_process->waitForFinished(500);
    }
    m_process->deleteLater();
    m_process = nullptr;
    if (!m_askToken.isEmpty()) {
        AskPass::instance()->release(std::exchange(m_askToken, {}));
    }
    m_pending.clear();
    m_buffer.clear();
    if (m_ready) {
        m_ready = false;
        Q_EMIT readyChanged();
    }
}

void Sftp::send(quint8 type, const QByteArray &payload, const Reply &reply)
{
    if (!m_process || m_process->state() != QProcess::Running) {
        QByteArray status;
        putU32(status, 4);
        putString(status, tr("Not connected").toUtf8());
        putString(status, "");
        reply(FxpStatus, status);
        return;
    }
    const quint32 id = m_nextId++;
    QByteArray body;
    body.append(char(type));
    putU32(body, id);
    body.append(payload);
    QByteArray framed;
    putU32(framed, quint32(body.size()));
    framed.append(body);
    m_pending.insert(id, reply);
    m_process->write(framed);
}

void Sftp::readyRead()
{
    m_buffer += m_process->readAllStandardOutput();
    while (m_buffer.size() >= 4) {
        const quint32 len = qFromBigEndian<quint32>(m_buffer.constData());
        if (len > 256 * 1024 * 1024) {
            m_process->kill();
            return;
        }
        if (m_buffer.size() < int(len) + 4) {
            return;
        }
        const QByteArray p = m_buffer.mid(4, int(len));
        m_buffer.remove(0, int(len) + 4);
        packet(p);
    }
}

void Sftp::packet(const QByteArray &data)
{
    if (data.isEmpty()) {
        return;
    }
    const quint8 type = quint8(data.at(0));
    if (type == FxpVersion) {
        m_ready = true;
        Q_EMIT readyChanged();
        return;
    }
    if (data.size() < 5) {
        return;
    }
    const quint32 id = qFromBigEndian<quint32>(data.constData() + 1);
    const Reply reply = m_pending.take(id);
    if (reply) {
        reply(type, data.mid(5));
    }
}

QString Sftp::statusText(const QByteArray &body)
{
    Reader r{body};
    const quint32 code = r.u32();
    const QString message = QString::fromUtf8(r.string());
    switch (code) {
    case 0: return {};
    case 2: return tr("That file or folder isn't there any more.");
    case 3: return tr("The server doesn't allow that here.");
    default: return message.isEmpty() ? tr("The server couldn't do that.") : message;
    }
}

void Sftp::realPath(const QString &path, const std::function<void(const QString &, const QString &)> &done)
{
    QByteArray p;
    putString(p, pathBytes(path));
    send(FxpRealpath, p, [done](quint8 type, const QByteArray &body) {
        if (type == FxpName) {
            Reader r{body};
            if (r.u32() >= 1) {
                done(QString::fromUtf8(r.string()), {});
                return;
            }
        }
        done({}, type == FxpStatus ? statusText(body) : tr("Unexpected answer"));
    });
}

void Sftp::stat(const QString &path, const std::function<void(const Entry &, const QString &)> &done)
{
    QByteArray p;
    putString(p, pathBytes(path));
    send(FxpStat, p, [done, path](quint8 type, const QByteArray &body) {
        if (type == FxpAttrs) {
            Reader r{body};
            Entry e = r.attrs();
            e.name = path.section(QLatin1Char('/'), -1);
            done(e, {});
            return;
        }
        done({}, type == FxpStatus ? statusText(body) : tr("Unexpected answer"));
    });
}

void Sftp::list(const QString &path, const Listed &done)
{
    QByteArray p;
    putString(p, pathBytes(path));
    send(FxpOpendir, p, [this, done](quint8 type, const QByteArray &body) {
        if (type != FxpHandle) {
            done({}, type == FxpStatus ? statusText(body) : tr("Unexpected answer"));
            return;
        }
        Reader r{body};
        const QByteArray handle = r.string();
        auto entries = QSharedPointer<QList<Entry>>::create();
        auto next = QSharedPointer<std::function<void()>>::create();
        *next = [this, handle, entries, next, done]() {
            QByteArray q;
            putString(q, handle);
            send(FxpReaddir, q, [this, handle, entries, next, done](quint8 type, const QByteArray &body) {
                if (type == FxpName) {
                    Reader r{body};
                    const quint32 n = r.u32();
                    for (quint32 i = 0; i < n && r.ok; ++i) {
                        const QString name = QString::fromUtf8(r.string());
                        r.string();
                        Entry e = r.attrs();
                        e.name = name;
                        if (name != QLatin1String(".") && name != QLatin1String("..")) {
                            entries->append(e);
                        }
                    }
                    (*next)();
                    return;
                }
                closeHandle(handle);
                // End of the folder is a status of EOF (1).
                Reader r{body};
                const quint32 code = type == FxpStatus ? r.u32() : 4;
                done(*entries, code == 1 ? QString() : statusText(body));
                // Breaks the cycle between the request and itself.
                *next = nullptr;
            });
        };
        (*next)();
    });
}

void Sftp::mkdir(const QString &path, const Done &done)
{
    QByteArray p;
    putString(p, pathBytes(path));
    putU32(p, 0);
    send(FxpMkdir, p, [done](quint8 type, const QByteArray &body) {
        done(type == FxpStatus ? statusText(body) : tr("Unexpected answer"));
    });
}

void Sftp::remove(const QString &path, const Done &done)
{
    QByteArray p;
    putString(p, pathBytes(path));
    send(FxpRemove, p, [done](quint8 type, const QByteArray &body) {
        done(type == FxpStatus ? statusText(body) : tr("Unexpected answer"));
    });
}

void Sftp::rmdir(const QString &path, const Done &done)
{
    QByteArray p;
    putString(p, pathBytes(path));
    send(FxpRmdir, p, [done](quint8 type, const QByteArray &body) {
        done(type == FxpStatus ? statusText(body) : tr("Unexpected answer"));
    });
}

void Sftp::rename(const QString &from, const QString &to, const Done &done)
{
    QByteArray p;
    putString(p, pathBytes(from));
    putString(p, pathBytes(to));
    send(FxpRename, p, [done](quint8 type, const QByteArray &body) {
        done(type == FxpStatus ? statusText(body) : tr("Unexpected answer"));
    });
}

void Sftp::openHandle(const QString &path, quint32 flags, const std::function<void(const QByteArray &, const QString &)> &done)
{
    QByteArray p;
    putString(p, pathBytes(path));
    putU32(p, flags);
    if (flags & FlagCreate) {
        // rw-r--r--, the usual for a new file; the server's umask applies.
        putU32(p, AttrPermissions);
        putU32(p, 0644);
    } else {
        putU32(p, 0);
    }
    send(FxpOpen, p, [done](quint8 type, const QByteArray &body) {
        if (type == FxpHandle) {
            Reader r{body};
            done(r.string(), {});
            return;
        }
        done({}, type == FxpStatus ? statusText(body) : tr("Unexpected answer"));
    });
}

void Sftp::closeHandle(const QByteArray &handle)
{
    QByteArray p;
    putString(p, handle);
    send(FxpClose, p, [](quint8, const QByteArray &) {});
}

void Sftp::upload(const QString &local, const QString &remote, const Progress &progress, const Done &done)
{
    auto file = QSharedPointer<QFile>::create(local);
    if (!file->open(QIODevice::ReadOnly)) {
        done(tr("%1 couldn't be read.").arg(local.section(QLatin1Char('/'), -1)));
        return;
    }
    openHandle(remote, FlagWrite | FlagCreate | FlagTruncate, [this, file, progress, done](const QByteArray &handle, const QString &error) {
        if (!error.isEmpty()) {
            done(error);
            return;
        }
        struct State {
            quint64 offset = 0;
            qint64 acknowledged = 0;
            int inFlight = 0;
            bool failed = false;
            bool finished = false;
        };
        auto state = QSharedPointer<State>::create();
        auto pump = QSharedPointer<std::function<void()>>::create();
        *pump = [this, file, handle, state, pump, progress, done]() {
            while (!state->failed && state->inFlight < window && !file->atEnd()) {
                const QByteArray data = file->read(chunk);
                if (data.isEmpty()) {
                    break;
                }
                QByteArray p;
                putString(p, handle);
                putU64(p, state->offset);
                putString(p, data);
                state->offset += quint64(data.size());
                ++state->inFlight;
                const qint64 size = data.size();
                send(FxpWrite, p, [this, state, pump, size, progress, done, handle](quint8 type, const QByteArray &body) {
                    --state->inFlight;
                    const QString err = type == FxpStatus ? statusText(body) : tr("Unexpected answer");
                    if (!err.isEmpty() && !state->failed) {
                        state->failed = true;
                        closeHandle(handle);
                        done(err);
                        *pump = nullptr;
                        return;
                    }
                    state->acknowledged += size;
                    if (progress) {
                        progress(state->acknowledged);
                    }
                    if (*pump) {
                        (*pump)();
                    }
                });
            }
            if (!state->failed && !state->finished && state->inFlight == 0 && file->atEnd()) {
                state->finished = true;
                closeHandle(handle);
                done({});
                *pump = nullptr;
            }
        };
        (*pump)();
    });
}

void Sftp::download(const QString &remote, const QString &local, const Progress &progress, const Done &done)
{
    auto file = QSharedPointer<QFile>::create(local);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        done(tr("%1 couldn't be written.").arg(local.section(QLatin1Char('/'), -1)));
        return;
    }
    openHandle(remote, FlagRead, [this, file, progress, done](const QByteArray &handle, const QString &error) {
        if (!error.isEmpty()) {
            file->remove();
            done(error);
            return;
        }
        struct State {
            quint64 next = 0;
            qint64 written = 0;
            int inFlight = 0;
            bool eof = false;
            bool failed = false;
            bool finished = false;
        };
        auto state = QSharedPointer<State>::create();
        auto pump = QSharedPointer<std::function<void()>>::create();
        *pump = [this, file, handle, state, pump, progress, done]() {
            while (!state->failed && !state->eof && state->inFlight < window) {
                QByteArray p;
                putString(p, handle);
                const quint64 offset = state->next;
                putU64(p, offset);
                putU32(p, chunk);
                state->next += chunk;
                ++state->inFlight;
                send(FxpRead, p, [this, file, state, pump, offset, progress, done, handle](quint8 type, const QByteArray &body) {
                    --state->inFlight;
                    if (state->failed) {
                        return;
                    }
                    if (type == FxpData) {
                        Reader r{body};
                        const QByteArray data = r.string();
                        // Replies may arrive in any order; each lands where
                        // it belongs.
                        file->seek(qint64(offset));
                        file->write(data);
                        state->written += data.size();
                        if (progress) {
                            progress(state->written);
                        }
                        // A short read from a regular file is its end.
                        if (quint32(data.size()) < chunk) {
                            state->eof = true;
                        }
                    } else {
                        Reader r{body};
                        const quint32 code = type == FxpStatus ? r.u32() : 4;
                        if (code == 1) {
                            state->eof = true;
                        } else {
                            state->failed = true;
                            closeHandle(handle);
                            file->remove();
                            done(statusText(body));
                            *pump = nullptr;
                            return;
                        }
                    }
                    if (*pump) {
                        (*pump)();
                    }
                });
            }
            if (!state->failed && !state->finished && state->eof && state->inFlight == 0) {
                state->finished = true;
                closeHandle(handle);
                file->close();
                done({});
                *pump = nullptr;
            }
        };
        (*pump)();
    });
}
