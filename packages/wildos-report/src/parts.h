#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>

// The parts of Wild OS someone can report a problem with: the first
// question in the app, and what `--part` and wildos-report:<part> name.
struct Part {
    QString id;
    QString name;
    // The programs that make it up, by process name, to pick their own log
    // lines and crashes. Empty: the whole system.
    QStringList programs;
    // Its package, for the version in About this PC.
    QString package;
};

namespace Parts
{
const QList<Part> &all();
// The part with this id, or "other" for anything unknown.
const Part &byId(const QString &id);
// The part a program belongs to, by process name or path; "other" if none.
const Part &byProgram(const QString &program);
// The name in the person's language; Part::name stays English for the
// report itself.
QString shownName(const Part &part);
// [{ value, text }] for the drop-down.
QVariantList choices();
bool known(const QString &id);
}
