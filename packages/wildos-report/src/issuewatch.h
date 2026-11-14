#pragma once

#include <QDateTime>
#include <QString>

// Finding the issue someone posted after Report a problem opened GitHub for
// them, in GitHub's list of the newest issues. Nobody signs in here, so the
// only thing tying an issue to this report is what we put in it: the zip's
// name, which the issue body names and the attached file is called by.
namespace IssueWatch
{
struct Posted {
    int number = 0;
    QString url;
};

// `json` is a reply from /repos/wildorigins/wildos/issues. An issue counts
// when its body names `zipName` and it was opened after `since`, less a
// couple of minutes for a clock that is slightly out.
Posted find(const QByteArray &json, const QString &zipName, const QDateTime &since);
}
