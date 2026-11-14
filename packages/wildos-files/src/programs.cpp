#include "programs.h"

#include <QFile>
#include <QMimeType>
#include <QUrl>

// Whether an ELF file asks for a program loader (PT_INTERP). Programs
// built as position independent executables, which is most of them now,
// share their MIME type with libraries, and this is what tells them apart.
static bool namesInterpreter(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray head = file.read(64);
    if (head.size() < 52 || !head.startsWith("\x7f" "ELF")) {
        return false;
    }
    const bool wide = head.at(4) == 2;
    const bool little = head.at(5) == 1;
    auto number = [little](const QByteArray &bytes, qsizetype at, int size) {
        quint64 value = 0;
        for (int i = 0; i < size; ++i) {
            value = (value << 8) | quint8(bytes.at(at + (little ? size - 1 - i : i)));
        }
        return value;
    };
    if (wide && head.size() < 64) {
        return false;
    }
    const quint64 offset = wide ? number(head, 32, 8) : number(head, 28, 4);
    const qsizetype entrySize = qsizetype(number(head, wide ? 54 : 42, 2));
    const qsizetype entries = qsizetype(number(head, wide ? 56 : 44, 2));
    if (entrySize < 4 || entrySize > 64 || entries == 0 || entries > 4096 || offset > quint64(file.size()) || !file.seek(qint64(offset))) {
        return false;
    }
    const QByteArray table = file.read(entrySize * entries);
    for (qsizetype at = 0; at + entrySize <= table.size(); at += entrySize) {
        if (number(table, at, 4) == 3) {
            return true;
        }
    }
    return false;
}

quint16 elfMachine(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return 0;
    }
    const QByteArray head = file.read(20);
    if (head.size() < 20 || quint8(head.at(0)) != 0x7f || head.mid(1, 3) != "ELF") {
        return 0;
    }
    const quint16 a = quint8(head.at(18));
    const quint16 b = quint8(head.at(19));
    return head.at(5) == 1 ? quint16(a | (b << 8)) : quint16(b | (a << 8));
}

bool isLinuxProgram(const QUrl &url, const QMimeType &type)
{
    if (!url.isLocalFile()) {
        return false;
    }
    const QString name = type.name();
    if (name == QLatin1String("application/x-executable") || type.inherits(QStringLiteral("application/vnd.appimage"))
        || type.inherits(QStringLiteral("application/x-iso9660-appimage"))) {
        return true;
    }
    return name == QLatin1String("application/x-sharedlib") && namesInterpreter(url.toLocalFile());
}
