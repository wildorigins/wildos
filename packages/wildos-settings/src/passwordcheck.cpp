#include "passwordcheck.h"

#include <QCoreApplication>
#include <QRegularExpression>

#include <pwquality.h>

namespace
{

// libpwquality's own messages are long and technical ("fails the
// dictionary check"), so each failure gets a short plain reason instead.
// Cracklib's reasons only come as text, matched here in English; in another
// language they fall back to the general one.
QString reasonFor(int error, const QString &cracklib)
{
    switch (error) {
    case PWQ_ERROR_CRACKLIB_CHECK:
        if (cracklib.contains(QLatin1String("dictionary word"))) {
            return QCoreApplication::translate("PasswordCheck", "it's based on a dictionary word");
        }
        if (cracklib.contains(QLatin1String("simplistic"))) {
            return QCoreApplication::translate("PasswordCheck", "it follows a simple pattern");
        }
        if (cracklib.contains(QLatin1String("DIFFERENT"))) {
            return QCoreApplication::translate("PasswordCheck", "it uses too few different characters");
        }
        if (cracklib.contains(QLatin1String("whitespace"))) {
            return QCoreApplication::translate("PasswordCheck", "it's all spaces");
        }
        if (cracklib.contains(QLatin1String("short"))) {
            return QCoreApplication::translate("PasswordCheck", "it's short");
        }
        return QCoreApplication::translate("PasswordCheck", "it's easy to guess");
    case PWQ_ERROR_PALINDROME:
        return QCoreApplication::translate("PasswordCheck", "it reads the same backwards");
    case PWQ_ERROR_USER_CHECK:
    case PWQ_ERROR_GECOS_CHECK:
        return QCoreApplication::translate("PasswordCheck", "it contains your name");
    case PWQ_ERROR_SAME_PASSWORD:
    case PWQ_ERROR_CASE_CHANGES_ONLY:
    case PWQ_ERROR_TOO_SIMILAR:
    case PWQ_ERROR_ROTATED:
        return QCoreApplication::translate("PasswordCheck", "it's too close to your current password");
    case PWQ_ERROR_MIN_LENGTH:
        return QCoreApplication::translate("PasswordCheck", "it's short");
    default:
        return QCoreApplication::translate("PasswordCheck", "it's easy to guess");
    }
}

}

QVariantMap PasswordCheck::check(const QString &password, const QString &oldPassword, const QStringList &names) const
{
    if (password.isEmpty()) {
        return {};
    }
    const auto weak = [](const QString &reason) {
        return QVariantMap{{QStringLiteral("level"), QStringLiteral("weak")}, {QStringLiteral("reason"), reason}};
    };

    // libpwquality only knows the user name, and only once the account
    // exists, so a new account's names are checked here too.
    static const QRegularExpression separators(QStringLiteral("[\\s._-]+"));
    for (const QString &name : names) {
        for (const QString &part : name.split(separators, Qt::SkipEmptyParts)) {
            if (part.size() >= 3 && password.contains(part, Qt::CaseInsensitive)) {
                return weak(tr("it contains your name"));
            }
        }
    }

    pwquality_settings_t *settings = pwquality_default_settings();
    if (!settings) {
        return {};
    }
    QByteArray secret = password.toUtf8();
    QByteArray old = oldPassword.toUtf8();
    const QString first = names.value(0).trimmed();
    const QByteArray user = !first.isEmpty() && !first.contains(QLatin1Char(' ')) ? first.toUtf8() : QByteArray();
    void *aux = nullptr;
    const int score = pwquality_check(settings, secret.constData(), old.isEmpty() ? nullptr : old.constData(),
                                      user.isEmpty() ? nullptr : user.constData(), &aux);
    secret.fill('\0');
    old.fill('\0');
    // For a cracklib failure the extra information is cracklib's reason.
    const QString cracklib = score == PWQ_ERROR_CRACKLIB_CHECK && aux ? QString::fromUtf8(static_cast<const char *>(aux)) : QString();
    // Frees whatever libpwquality attached to the result.
    char buffer[PWQ_MAX_ERROR_MESSAGE_LEN];
    pwquality_strerror(buffer, sizeof buffer, score, aux);
    pwquality_free_settings(settings);

    // Without cracklib's word list there is nothing fair to say.
    if (cracklib.contains(QLatin1String("error loading dictionary"))) {
        return {};
    }
    if (score < 0) {
        return weak(reasonFor(score, cracklib));
    }
    return {{QStringLiteral("level"), score >= 80 ? QStringLiteral("strong") : QStringLiteral("fair")}, {QStringLiteral("reason"), QString()}};
}
