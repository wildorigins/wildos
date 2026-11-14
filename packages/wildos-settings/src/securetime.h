#pragma once

#include <QString>

// sdwdate, Kicksecure's time service: it sets the clock from onion sites
// through Tor a few minutes after start-up, instead of asking a time server
// directly, so timedated knows nothing of it and reports no automatic time.
namespace SecureTime
{
// The service is running or starting (it restarts itself after a failure).
bool running();
// It has set the clock since this start-up: it writes this marker in /run,
// which every start-up empties.
bool setThisBoot();
// Running but not done yet, so a clock that is hours off is still expected
// to be put right without anyone's help.
bool settingNow();

// How far behind the clock is at least, from the signing times apt named
// that are still in the future ("Not live until 2026-11-14T03:33:03Z" from
// sqv). 0 when none is.
qint64 secondsBehind(const QString &aptDetails);
// "8 hours", "25 minutes", "3 days"
QString durationText(qint64 seconds);
}
