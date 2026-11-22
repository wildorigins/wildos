#include "osklangs.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QDir>
#include <QFile>
#include <QHash>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>

namespace
{
const QString schema = QStringLiteral("org.maliit.keyboard.maliit");

// XKB layout (and variant, where it changes the language) to the name of
// Maliit's language folder.
QString languageFor(const QString &layout, const QString &variant)
{
    static const QHash<QString, QString> layouts = {
        {QStringLiteral("us"), QStringLiteral("en")},      {QStringLiteral("gb"), QStringLiteral("en")},
        {QStringLiteral("au"), QStringLiteral("en")},      {QStringLiteral("ie"), QStringLiteral("en")},
        {QStringLiteral("nz"), QStringLiteral("en")},      {QStringLiteral("za"), QStringLiteral("en")},
        {QStringLiteral("in"), QStringLiteral("en")},      {QStringLiteral("ara"), QStringLiteral("ar")},
        {QStringLiteral("eg"), QStringLiteral("ar")},      {QStringLiteral("iq"), QStringLiteral("ar")},
        {QStringLiteral("ma"), QStringLiteral("ar")},      {QStringLiteral("sy"), QStringLiteral("ar")},
        {QStringLiteral("dz"), QStringLiteral("ar")},      {QStringLiteral("az"), QStringLiteral("az")},
        {QStringLiteral("by"), QStringLiteral("be")},      {QStringLiteral("bg"), QStringLiteral("bg")},
        {QStringLiteral("ba"), QStringLiteral("bs")},      {QStringLiteral("cz"), QStringLiteral("cs")},
        {QStringLiteral("dk"), QStringLiteral("da")},      {QStringLiteral("de"), QStringLiteral("de")},
        {QStringLiteral("at"), QStringLiteral("de")},      {QStringLiteral("ch"), QStringLiteral("de")},
        {QStringLiteral("gr"), QStringLiteral("el")},      {QStringLiteral("epo"), QStringLiteral("eo")},
        {QStringLiteral("es"), QStringLiteral("es")},      {QStringLiteral("latam"), QStringLiteral("es")},
        {QStringLiteral("ir"), QStringLiteral("fa")},      {QStringLiteral("af"), QStringLiteral("fa")},
        {QStringLiteral("fi"), QStringLiteral("fi")},      {QStringLiteral("fr"), QStringLiteral("fr")},
        {QStringLiteral("be"), QStringLiteral("fr")},      {QStringLiteral("ca"), QStringLiteral("fr")},
        {QStringLiteral("il"), QStringLiteral("he")},      {QStringLiteral("hr"), QStringLiteral("hr")},
        {QStringLiteral("hu"), QStringLiteral("hu")},      {QStringLiteral("is"), QStringLiteral("is")},
        {QStringLiteral("it"), QStringLiteral("it")},      {QStringLiteral("kr"), QStringLiteral("ko")},
        {QStringLiteral("lt"), QStringLiteral("lt")},      {QStringLiteral("lv"), QStringLiteral("lv")},
        {QStringLiteral("mk"), QStringLiteral("mk")},      {QStringLiteral("no"), QStringLiteral("nb")},
        {QStringLiteral("nl"), QStringLiteral("nl")},      {QStringLiteral("pl"), QStringLiteral("pl")},
        {QStringLiteral("pt"), QStringLiteral("pt")},      {QStringLiteral("br"), QStringLiteral("pt")},
        {QStringLiteral("ro"), QStringLiteral("ro")},      {QStringLiteral("ru"), QStringLiteral("ru")},
        {QStringLiteral("si"), QStringLiteral("sl")},      {QStringLiteral("rs"), QStringLiteral("sr")},
        {QStringLiteral("se"), QStringLiteral("sv")},      {QStringLiteral("th"), QStringLiteral("th")},
        {QStringLiteral("tr"), QStringLiteral("tr")},      {QStringLiteral("ua"), QStringLiteral("uk")},
        {QStringLiteral("cn"), QStringLiteral("zh-hans")}, {QStringLiteral("tw"), QStringLiteral("zh-hant")},
    };
    if (layout == QLatin1String("ch") && variant.startsWith(QLatin1String("fr"))) {
        return QStringLiteral("fr-ch");
    }
    if (layout == QLatin1String("es") && variant == QLatin1String("cat")) {
        return QStringLiteral("ca");
    }
    if (layout == QLatin1String("gb") && variant == QLatin1String("gla")) {
        return QStringLiteral("gd");
    }
    if (layout == QLatin1String("us") && variant == QLatin1String("dvorak")) {
        return QStringLiteral("en@dv");
    }
    return layouts.value(layout);
}

// What KWin uses: the person's own layouts if set, else the computer's.
QList<QPair<QString, QString>> layouts()
{
    const KConfigGroup group(KSharedConfig::openConfig(QStringLiteral("kxkbrc"), KConfig::NoGlobals), QStringLiteral("Layout"));
    QStringList names;
    QStringList variants;
    if (group.readEntry("Use", false)) {
        names = group.readEntry("LayoutList", QString()).split(QLatin1Char(','));
        variants = group.readEntry("VariantList", QString()).split(QLatin1Char(','));
    }
    if (names.join(QString()).trimmed().isEmpty()) {
        QFile file(QStringLiteral("/etc/default/keyboard"));
        if (file.open(QIODevice::ReadOnly)) {
            static const QRegularExpression line(QStringLiteral("^\\s*(XKBLAYOUT|XKBVARIANT)=\"?([^\"]*)\"?"));
            for (const QString &text : QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'))) {
                const auto match = line.match(text);
                if (match.hasMatch()) {
                    (match.captured(1) == QLatin1String("XKBLAYOUT") ? names : variants) = match.captured(2).split(QLatin1Char(','));
                }
            }
        }
    }
    QList<QPair<QString, QString>> list;
    for (int i = 0; i < names.size(); ++i) {
        list.append({names.at(i).trimmed(), variants.value(i).trimmed()});
    }
    return list;
}

QString gsettingsGet(const QString &key)
{
    QProcess process;
    process.start(QStringLiteral("gsettings"), {QStringLiteral("get"), schema, key});
    process.waitForFinished(3000);
    return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}

void gsettingsSet(const QString &key, const QString &value)
{
    QProcess::execute(QStringLiteral("gsettings"), {QStringLiteral("set"), schema, key, value});
}

QString quoted(const QString &name)
{
    return QLatin1Char('\'') + name + QLatin1Char('\'');
}

KConfigGroup wildos()
{
    KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("wildosrc"), KConfig::NoGlobals);
    config->reparseConfiguration();
    return config->group(QStringLiteral("Accessibility"));
}

// Maliit names a few languages its own way.
QLocale localeFor(const QString &code)
{
    if (code == QLatin1String("fr-ch")) {
        return QLocale(QLocale::French, QLocale::Switzerland);
    }
    if (code == QLatin1String("zh-hans")) {
        return QLocale(QLocale::Chinese, QLocale::SimplifiedHanScript, QLocale::China);
    }
    if (code == QLatin1String("zh-hant")) {
        return QLocale(QLocale::Chinese, QLocale::TraditionalHanScript, QLocale::Taiwan);
    }
    if (code == QLatin1String("sr")) {
        return QLocale(QLocale::Serbian, QLocale::CyrillicScript, QLocale::Serbia);
    }
    return QLocale(code.section(QLatin1Char('@'), 0, 0));
}
}

namespace OskLangs
{
QStringList installed()
{
    for (const QString &dir : {QStringLiteral("/usr/lib/x86_64-linux-gnu/maliit/keyboard2/languages"), QStringLiteral("/usr/lib/aarch64-linux-gnu/maliit/keyboard2/languages"),
                               QStringLiteral("/usr/lib/maliit/keyboard2/languages")}) {
        QStringList names = QDir(dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        // The emoji board is reached from the language menu, not as a language.
        names.removeAll(QStringLiteral("emoji"));
        if (!names.isEmpty()) {
            return names;
        }
    }
    return {};
}

QStringList enabled()
{
    static const QRegularExpression item(QStringLiteral("'([^']*)'"));
    QStringList list;
    auto it = item.globalMatch(gsettingsGet(QStringLiteral("enabled-languages")));
    while (it.hasNext()) {
        list.append(it.next().captured(1));
    }
    return list;
}

void setEnabled(const QStringList &languages)
{
    QStringList items;
    for (const QString &language : languages) {
        items.append(quoted(language));
    }
    const QString value = QLatin1Char('[') + items.join(QStringLiteral(", ")) + QLatin1Char(']');
    // Only a real change is written, so dconf is not touched every sign-in.
    if (gsettingsGet(QStringLiteral("enabled-languages")) != value) {
        gsettingsSet(QStringLiteral("enabled-languages"), value);
    }
    if (!languages.isEmpty() && !languages.contains(active())) {
        setActive(languages.first());
    }
}

QString active()
{
    const QString value = gsettingsGet(QStringLiteral("active-language"));
    return value.size() >= 2 ? value.mid(1, value.size() - 2) : QString();
}

void setActive(const QString &language)
{
    if (active() != language) {
        gsettingsSet(QStringLiteral("active-language"), quoted(language));
    }
}

QStringList byLayout()
{
    const QStringList have = installed();
    QStringList result;
    for (const auto &[layout, variant] : layouts()) {
        const QString language = languageFor(layout, variant);
        result.append(have.contains(language) ? language : QString());
    }
    return result;
}

QStringList fromLayouts()
{
    QStringList result;
    for (const QString &language : byLayout()) {
        if (!language.isEmpty() && !result.contains(language)) {
            result.append(language);
        }
    }
    return result;
}

bool chosen()
{
    return wildos().readEntry("OskLanguagesChosen", false);
}

void setChosen()
{
    KConfigGroup group = wildos();
    group.writeEntry("OskLanguagesChosen", true);
    group.sync();
}

QString nativeName(const QString &code)
{
    if (code == QLatin1String("en@dv")) {
        return QStringLiteral("English (Dvorak)");
    }
    const QLocale locale = localeFor(code);
    QString name = locale.nativeLanguageName();
    if (!name.isEmpty()) {
        name[0] = name[0].toUpper();
    }
    if (code == QLatin1String("fr-ch") || code.startsWith(QLatin1String("zh-"))) {
        name += QStringLiteral(" (") + locale.nativeTerritoryName() + QLatin1Char(')');
    }
    return name.isEmpty() ? code : name;
}

QString englishName(const QString &code)
{
    if (code == QLatin1String("en@dv")) {
        return QStringLiteral("English (Dvorak)");
    }
    const QLocale locale = localeFor(code);
    QString name = QLocale::languageToString(locale.language());
    if (code == QLatin1String("fr-ch") || code.startsWith(QLatin1String("zh-"))) {
        name += QStringLiteral(" (") + QLocale::territoryToString(locale.territory()) + QLatin1Char(')');
    }
    return name;
}
}
