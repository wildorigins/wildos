// Picking this report's issue out of GitHub's list of the newest ones. The
// replies are trimmed copies of what /repos/wildorigins/wildos/issues sends.

#include "issuewatch.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

class IssueWatchTest : public QObject
{
    Q_OBJECT

private:
    const QDateTime opened = QDateTime::fromString(QStringLiteral("2026-11-14T10:00:00Z"), Qt::ISODate);
    const QString zip = QStringLiteral("wildos-report-1000.zip");

    static QByteArray issue(int number, const QString &created, const QString &body, bool pull = false)
    {
        QJsonObject object{{QStringLiteral("number"), number},
                           {QStringLiteral("html_url"),
                            QStringLiteral("https://github.com/wildorigins/wildos/%1/%2").arg(pull ? QStringLiteral("pull") : QStringLiteral("issues")).arg(number)},
                           {QStringLiteral("created_at"), created},
                           {QStringLiteral("body"), body}};
        if (pull) {
            object.insert(QStringLiteral("pull_request"), QJsonObject());
        }
        return QJsonDocument(object).toJson(QJsonDocument::Compact);
    }

    static QByteArray list(const QList<QByteArray> &issues)
    {
        QByteArray out = "[";
        for (qsizetype i = 0; i < issues.size(); ++i) {
            out += (i > 0 ? "," : "") + issues.at(i);
        }
        return out + "]";
    }

private Q_SLOTS:
    void findsTheIssueThatNamesTheZip()
    {
        const QByteArray reply = list({issue(58, QStringLiteral("2026-11-14T10:04:10Z"), QStringLiteral("Something else")),
                                       issue(57, QStringLiteral("2026-11-14T10:03:00Z"), QStringLiteral("Files froze. Drag `wildos-report-1000.zip` here."))});
        const IssueWatch::Posted posted = IssueWatch::find(reply, zip, opened);
        QCOMPARE(posted.number, 57);
        QCOMPARE(posted.url, QStringLiteral("https://github.com/wildorigins/wildos/issues/57"));
    }

    void findsItByTheAttachedFile()
    {
        const QByteArray reply = list({issue(57, QStringLiteral("2026-11-14T10:03:00Z"),
                                             QStringLiteral("[wildos-report-1000.zip](https://github.com/user-attachments/files/1/wildos-report-1000.zip)"))});
        QCOMPARE(IssueWatch::find(reply, zip, opened).number, 57);
    }

    void ignoresAnOlderIssueWithTheSameName()
    {
        const QByteArray reply = list({issue(40, QStringLiteral("2026-11-13T10:03:00Z"), QStringLiteral("wildos-report-1000.zip"))});
        QCOMPARE(IssueWatch::find(reply, zip, opened).number, 0);
    }

    void allowsAClockSlightlyAhead()
    {
        const QByteArray reply = list({issue(57, QStringLiteral("2026-11-14T09:59:00Z"), QStringLiteral("wildos-report-1000.zip"))});
        QCOMPARE(IssueWatch::find(reply, zip, opened).number, 57);
    }

    void ignoresPullRequests()
    {
        const QByteArray reply = list({issue(59, QStringLiteral("2026-11-14T10:03:00Z"), QStringLiteral("wildos-report-1000.zip"), true)});
        QCOMPARE(IssueWatch::find(reply, zip, opened).number, 0);
    }

    void ignoresAnotherNumberedZip()
    {
        const QByteArray reply = list({issue(57, QStringLiteral("2026-11-14T10:03:00Z"), QStringLiteral("wildos-report-1000-2.zip"))});
        QCOMPARE(IssueWatch::find(reply, zip, opened).number, 0);
    }

    void survivesNonsense()
    {
        QCOMPARE(IssueWatch::find("{\"message\":\"API rate limit exceeded\"}", zip, opened).number, 0);
        QCOMPARE(IssueWatch::find("not json", zip, opened).number, 0);
        QCOMPARE(IssueWatch::find(list({}), QString(), opened).number, 0);
    }
};

QTEST_GUILESS_MAIN(IssueWatchTest)
#include "issuewatch_test.moc"
