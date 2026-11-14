#include "packagesizes.h"

#include <QProcess>
#include <QProcessEnvironment>
#include <QSet>

namespace
{
// Every package a Depends-style field names, alternatives included, without
// versions or architectures.
QStringList names(const QString &field)
{
    QStringList out;
    for (const QString &group : field.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        for (const QString &alt : group.split(QLatin1Char('|'), Qt::SkipEmptyParts)) {
            const QString name = alt.trimmed().section(QLatin1Char(' '), 0, 0).section(QLatin1Char('('), 0, 0).section(QLatin1Char(':'), 0, 0);
            if (!name.isEmpty()) {
                out << name;
            }
        }
    }
    return out;
}
}

PackageSizes::PackageSizes(QObject *parent)
    : QObject(parent)
{
}

QProcess *PackageSizes::run(const QString &program, const QStringList &args)
{
    auto proc = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C.UTF-8"));
    proc->setProcessEnvironment(env);
    proc->setProcessChannelMode(QProcess::SeparateChannels);
    proc->start(program, args);
    return proc;
}

void PackageSizes::refresh(const QStringList &packages)
{
    m_wanted = packages;
    if (m_running) {
        m_again = true;
        return;
    }
    m_running = true;
    m_again = false;
    m_next.clear();
    m_missing.clear();
    m_brings.clear();
    readInstalled();
}

void PackageSizes::readInstalled()
{
    QProcess *proc = run(QStringLiteral("dpkg-query"),
                         {QStringLiteral("-W"), QStringLiteral("-f=${Package}\t${db:Status-Abbrev}\t${Installed-Size}\t${Pre-Depends}, ${Depends}\t${Recommends}\t${Provides}\n")});
    connect(proc, &QProcess::finished, this, [this, proc] {
        proc->deleteLater();
        QHash<QString, qint64> kib;
        QHash<QString, QStringList> deps;
        QHash<QString, QStringList> needs;
        QHash<QString, QStringList> providers;
        const QStringList lines = QString::fromUtf8(proc->readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            const QStringList f = line.split(QLatin1Char('\t'));
            if (f.size() < 6 || f.at(1).size() < 2 || f.at(1).at(1) != QLatin1Char('i')) {
                continue;
            }
            const QString &name = f.at(0);
            // A package installed for two architectures (the 32-bit switch)
            // takes room twice.
            kib[name] += f.at(2).toLongLong();
            const QStringList own = names(f.at(3));
            deps[name] << own;
            needs[name] << own << names(f.at(4));
            for (const QString &virt : names(f.at(5))) {
                providers[virt] << name;
            }
        }
        auto resolve = [&kib, &providers](const QString &dep) {
            return kib.contains(dep) ? QStringList{dep} : providers.value(dep);
        };
        // What keeps each package from being taken away: everything installed
        // that depends on or recommends it, as apt's own autoremove counts.
        QHash<QString, QSet<QString>> neededBy;
        for (auto it = needs.cbegin(); it != needs.cend(); ++it) {
            for (const QString &dep : it.value()) {
                for (const QString &real : resolve(dep)) {
                    if (real != it.key()) {
                        neededBy[real].insert(it.key());
                    }
                }
            }
        }
        for (const QString &package : std::as_const(m_wanted)) {
            if (!kib.contains(package)) {
                m_missing << package;
                continue;
            }
            QSet<QString> owned{package};
            bool grew = true;
            while (grew) {
                grew = false;
                const QSet<QString> now = owned;
                for (const QString &o : now) {
                    for (const QString &dep : deps.value(o)) {
                        for (const QString &real : resolve(dep)) {
                            if (!owned.contains(real) && owned.contains(neededBy.value(real))) {
                                owned.insert(real);
                                grew = true;
                            }
                        }
                    }
                }
            }
            qint64 total = 0;
            for (const QString &o : std::as_const(owned)) {
                total += kib.value(o);
            }
            m_next.insert(package, total * 1024);
        }
        simulateNext();
    });
}

void PackageSizes::simulateNext()
{
    const QString package = [this] {
        for (const QString &p : std::as_const(m_missing)) {
            if (!m_brings.contains(p)) {
                return p;
            }
        }
        return QString();
    }();
    if (package.isEmpty()) {
        readCandidates();
        return;
    }
    // A simulation needs no password and changes nothing.
    QProcess *proc = run(QStringLiteral("apt-get"), {QStringLiteral("-s"), QStringLiteral("-q"), QStringLiteral("--no-install-recommends"),
                                                     QStringLiteral("install"), package});
    connect(proc, &QProcess::finished, this, [this, proc, package] {
        proc->deleteLater();
        QStringList brings;
        const QStringList lines = QString::fromUtf8(proc->readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            if (line.startsWith(QLatin1String("Inst "))) {
                brings << line.section(QLatin1Char(' '), 1, 1).section(QLatin1Char(':'), 0, 0);
            }
        }
        m_brings.insert(package, brings);
        simulateNext();
    });
}

void PackageSizes::readCandidates()
{
    QSet<QString> all;
    for (const QStringList &brings : std::as_const(m_brings)) {
        for (const QString &p : brings) {
            all.insert(p);
        }
    }
    if (all.isEmpty()) {
        finish();
        return;
    }
    QStringList args{QStringLiteral("show"), QStringLiteral("--no-all-versions")};
    args << QStringList(all.cbegin(), all.cend());
    QProcess *proc = run(QStringLiteral("apt-cache"), args);
    connect(proc, &QProcess::finished, this, [this, proc] {
        proc->deleteLater();
        QHash<QString, qint64> kib;
        QString current;
        const QStringList lines = QString::fromUtf8(proc->readAllStandardOutput()).split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            if (line.startsWith(QLatin1String("Package: "))) {
                current = line.mid(9).trimmed();
            } else if (line.startsWith(QLatin1String("Installed-Size: ")) && !kib.contains(current)) {
                kib.insert(current, line.mid(16).trimmed().toLongLong());
            }
        }
        for (auto it = m_brings.cbegin(); it != m_brings.cend(); ++it) {
            if (it.value().isEmpty()) {
                continue;
            }
            qint64 total = 0;
            for (const QString &p : it.value()) {
                total += kib.value(p);
            }
            m_next.insert(it.key(), total * 1024);
        }
        finish();
    });
}

void PackageSizes::finish()
{
    m_running = false;
    if (m_next != m_sizes) {
        m_sizes = m_next;
        Q_EMIT changed();
    }
    if (m_again) {
        refresh(m_wanted);
    }
}
