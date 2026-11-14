#pragma once

#include <QtGlobal>

class QMimeType;
class QString;
class QUrl;

// Telling programs from documents, shared by Files and the safe box.

// The processor an ELF file is built for (e_machine), or 0 when it is not
// ELF. A download for another processor, such as an ARM AppImage on a PC,
// fails with "Exec format error" only after KIO has counted the launch as a
// success, so nobody would hear about it unless it is caught before then.
quint16 elfMachine(const QString &path);

#if defined(__x86_64__)
constexpr quint16 hostMachine = 62;
#elif defined(__aarch64__)
constexpr quint16 hostMachine = 183;
#else
constexpr quint16 hostMachine = 0;
#endif

// A Linux program or AppImage that a double click would run. Scripts are
// left out: they open in their app, where people can read them.
bool isLinuxProgram(const QUrl &url, const QMimeType &type);
