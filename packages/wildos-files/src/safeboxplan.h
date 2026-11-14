#pragma once

#include <KService>
#include <QString>

// What "Open in safe box" would do with a file: run it, when it is a Linux
// program, or open it in the app that opens it on a double click. Apps that
// bring their own sandbox (Flatpak) or need Windows mode can't run inside
// one, so the next app for the type is taken, or the file is turned down
// with a sentence saying why. Shared by Files, which decides whether to
// offer it, and wildos-safebox, which does it.
struct SafeBoxPlan {
    bool ok = false;
    bool program = false;
    QString message;
    KService::Ptr app;
    QString appName;
};

SafeBoxPlan safeBoxPlan(const QString &path);
