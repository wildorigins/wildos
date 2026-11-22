#pragma once

#include <QStringList>

// The on-screen keyboard's languages (Maliit's enabled-languages and
// active-language). Shared by Settings, where they are chosen, and
// wildos-access, which fills them from the keyboard layouts until then.
namespace OskLangs
{
// Maliit's language folders, such as "en", "ar" or "fr-ch".
QStringList installed();
QStringList enabled();
void setEnabled(const QStringList &languages);
QString active();
void setActive(const QString &language);

// The Maliit language for each keyboard layout, in layout order, empty
// where Maliit has none.
QStringList byLayout();
// The same without gaps or repeats.
QStringList fromLayouts();

// Set once someone picks languages in Settings; from then on layouts only
// add languages, never take theirs away.
bool chosen();
void setChosen();

QString nativeName(const QString &code);
QString englishName(const QString &code);
}
