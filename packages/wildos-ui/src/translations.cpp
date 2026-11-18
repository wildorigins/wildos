// Loads the translations every Wild OS package installs into
// /usr/share/wildos/translations as <package>_<language>.qm, for the
// language chosen in Settings. Every Wild OS app, the taskbar and Start
// import WildOS.UI, so loading them here covers all of them without each
// app's main() having to do it.

#include <QCoreApplication>
#include <QDir>
#include <QLocale>
#include <QSet>
#include <QTranslator>

namespace {

constexpr auto kDir = "/usr/share/wildos/translations";

void loadWildOSTranslations()
{
    auto *app = QCoreApplication::instance();
    if (!app || app->property("wildosTranslationsLoaded").toBool())
        return;
    app->setProperty("wildosTranslationsLoaded", true);

    // The display language (LANGUAGE, from Settings) can differ from the
    // regional format (LANG), so this goes by uiLanguages, not language().
    const QLocale locale;
    const QStringList languages = locale.uiLanguages();
    if (languages.isEmpty() || languages.first().startsWith(QLatin1String("en"))
        || languages.first() == QLatin1String("C"))
        return;

    QSet<QString> packages;
    const auto files = QDir(QString::fromLatin1(kDir)).entryList({QStringLiteral("*.qm")}, QDir::Files);
    for (const QString &file : files) {
        const int cut = file.indexOf(QLatin1Char('_'));
        if (cut > 0)
            packages.insert(file.left(cut));
    }
    for (const QString &package : std::as_const(packages)) {
        auto *translator = new QTranslator(app);
        // Tries fr_CA, then fr, in the order the session's languages are listed.
        if (translator->load(locale, package, QStringLiteral("_"), QString::fromLatin1(kDir)))
            QCoreApplication::installTranslator(translator);
        else
            delete translator;
    }
}

}

// Runs when the plugin is loaded, which is after the application object
// exists and before any of the app's QML is created.
Q_COREAPP_STARTUP_FUNCTION(loadWildOSTranslations)
