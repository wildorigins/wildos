.pragma library

// PAM modules talk in their own jargon ("faillock: Error opening the tally
// file", "Authentication token manipulation error"), so nothing they say is
// ever shown as it is. Each message is either recognised as a reason the
// person cannot sign in right now, or dropped.

const wrongPassword = qsTr("That password did not work");
const wrongPasswordOrPin = qsTr("That password or PIN did not work");
const unavailable = qsTr("You can't sign in right now. Try again in a moment.");

// pam_wildos_pin, after too many wrong PINs: the password still works.
const pinWait = /wrong PINs\b.*\bin (a|\d+) minutes?/i;

const blockingPatterns = [
    /lock(ed)?\b.*\b(account|user)|account.*\block/i,
    /too many|maximum number of|retry later|try again later|temporarily/i,
    /expired|disabled|not allowed|access denied|denied access/i,
];

// Returns the plain sentence to show for a PAM error or info message, or an
// empty string when it should not be shown at all.
function translate(message) {
    if (!message)
        return "";
    const text = String(message);
    const wait = pinWait.exec(text);
    if (wait) {
        return wait[1] === "a" ? qsTr("Too many wrong PINs. Use your password, or try your PIN again in a minute.")
            : qsTr("Too many wrong PINs. Use your password, or try your PIN again in %1 minutes.").arg(wait[1]);
    }
    for (const pattern of blockingPatterns) {
        if (pattern.test(text))
            return unavailable;
    }
    return "";
}
