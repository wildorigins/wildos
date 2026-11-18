.pragma library

// The system service (winmode/setup.py, winmode/isocheck.py) speaks English
// over D-Bus, and its words stay as they are there: other tools read them.
// Its fixed lines are listed here so lupdate finds them, and tr() shows each
// one in the language chosen in Settings. A line the service builds with
// numbers in it (time left, how much has downloaded) is matched by its
// shape; anything unknown, an unexpected error's own words, say, is shown as
// it comes. The taskbar's Windows mode applet looks up the same context.
const context = "WinmodeService";

const known = [
    // The steps (stage_title, stage_detail) and what each says while it runs.
    QT_TRANSLATE_NOOP("WinmodeService", "Copy Windows 11 from your file"),
    QT_TRANSLATE_NOOP("WinmodeService", "Download Windows 11 from Microsoft"),
    QT_TRANSLATE_NOOP("WinmodeService", "Install Windows"),
    QT_TRANSLATE_NOOP("WinmodeService", "Activate with your product key"),
    QT_TRANSLATE_NOOP("WinmodeService", "Activation"),
    QT_TRANSLATE_NOOP("WinmodeService", "Activate with this PC's licence"),
    QT_TRANSLATE_NOOP("WinmodeService", "Tune for speed and privacy"),
    QT_TRANSLATE_NOOP("WinmodeService", "Install Microsoft 365"),
    QT_TRANSLATE_NOOP("WinmodeService", "Checked that it's Windows 11 Pro"),
    QT_TRANSLATE_NOOP("WinmodeService", "About 6 GB, checked against Microsoft's signature"),
    QT_TRANSLATE_NOOP("WinmodeService", "Automatic, no questions to answer"),
    QT_TRANSLATE_NOOP("WinmodeService", "Skipped, as you chose. Add a key any time in Settings."),
    QT_TRANSLATE_NOOP("WinmodeService", "Removes adverts, tracking and apps you won't use"),
    QT_TRANSLATE_NOOP("WinmodeService", "Word, Excel, PowerPoint, Outlook, OneNote"),
    QT_TRANSLATE_NOOP("WinmodeService", "Getting the drivers Windows needs"),
    QT_TRANSLATE_NOOP("WinmodeService", "Getting ready to download the parts Windows mode runs on"),
    QT_TRANSLATE_NOOP("WinmodeService", "Asking Microsoft for Windows 11"),
    QT_TRANSLATE_NOOP("WinmodeService", "Checking it against Microsoft's signature"),
    QT_TRANSLATE_NOOP("WinmodeService", "Copying Windows 11 from your file"),
    QT_TRANSLATE_NOOP("WinmodeService", "Checking it's Windows 11 Pro"),
    QT_TRANSLATE_NOOP("WinmodeService", "Drivers"),
    QT_TRANSLATE_NOOP("WinmodeService", "Windows 11 Home"),
    QT_TRANSLATE_NOOP("WinmodeService", "About a minute left"),

    // Setup putting a problem right by itself.
    QT_TRANSLATE_NOOP("WinmodeService", "Setup hit a problem and is trying that step again."),
    QT_TRANSLATE_NOOP("WinmodeService", "Setup hit the same problem twice, so it's going back a step and doing that again."),

    // Problems setup stops on.
    QT_TRANSLATE_NOOP("WinmodeService", "Waiting for the internet. Setup carries on by itself as soon as you're back online."),
    QT_TRANSLATE_NOOP("WinmodeService", "Windows mode couldn't finish setting up. Try again, and if it happens twice, restart the PC first."),
    QT_TRANSLATE_NOOP("WinmodeService", "Your Windows 11 file couldn't be read. Check the drive it's on is connected, then try again."),
    QT_TRANSLATE_NOOP("WinmodeService", "The Windows download didn't match Microsoft's signature twice, so it was deleted."),
    QT_TRANSLATE_NOOP("WinmodeService", "Windows mode couldn't download the parts it runs on."),
    QT_TRANSLATE_NOOP("WinmodeService", "The driver download moved somewhere unexpected."),
    QT_TRANSLATE_NOOP("WinmodeService", "The drivers download came back as something other than the drivers. Try again in a while."),
    QT_TRANSLATE_NOOP("WinmodeService", "Wild OS is installing updates. Try again when they've finished."),
    QT_TRANSLATE_NOOP("WinmodeService", "An earlier software install on this PC was left half done, and Windows mode couldn't finish it. Send it with Report a problem so we can put it right."),
    QT_TRANSLATE_NOOP("WinmodeService", "This PC's licence couldn't be read. Go back and add a product key instead."),
    QT_TRANSLATE_NOOP("WinmodeService", "Windows didn't finish installing. Try again, and if it happens twice, restart the PC first."),
    QT_TRANSLATE_NOOP("WinmodeService", "Windows switched itself off while installing. Try again."),
    QT_TRANSLATE_NOOP("WinmodeService", "Windows Setup didn't start. Try again, and if it happens twice, restart the PC first."),
    QT_TRANSLATE_NOOP("WinmodeService", "Windows didn't start. Try again, and if it happens twice, restart the PC first."),
    QT_TRANSLATE_NOOP("WinmodeService", "Windows didn't finish getting ready. Try again."),
    QT_TRANSLATE_NOOP("WinmodeService", "Windows couldn't finish getting ready. Try again."),
    QT_TRANSLATE_NOOP("WinmodeService", "Microsoft 365 couldn't be downloaded. Add it later from Settings."),
    QT_TRANSLATE_NOOP("WinmodeService", "Microsoft 365 didn't finish installing. Add it later from Settings."),
    QT_TRANSLATE_NOOP("WinmodeService", "Microsoft 365 didn't install. Add it later from Settings."),
    QT_TRANSLATE_NOOP("WinmodeService", "The download kept stopping, although this PC is online. The server may be busy."),
    QT_TRANSLATE_NOOP("WinmodeService", "The product key couldn't be read. Start over and type it again."),
    QT_TRANSLATE_NOOP("WinmodeService", "This licence is for Windows 11 Home, which can't show Windows apps on Wild OS."),
    // These four come with the error's own words after them in brackets.
    QT_TRANSLATE_NOOP("WinmodeService", "The download stopped."),
    QT_TRANSLATE_NOOP("WinmodeService", "Microsoft didn't allow the download from here."),
    QT_TRANSLATE_NOOP("WinmodeService", "Microsoft's download page didn't answer as expected."),
    QT_TRANSLATE_NOOP("WinmodeService", "That file couldn't be checked."),

    // Why setup didn't start, or a Windows 11 file was turned down.
    QT_TRANSLATE_NOOP("WinmodeService", "The last setup is still stopping. Try again in a minute."),
    QT_TRANSLATE_NOOP("WinmodeService", "Windows mode is already being set up."),
    QT_TRANSLATE_NOOP("WinmodeService", "Windows mode is set up on this PC already."),
    QT_TRANSLATE_NOOP("WinmodeService", "Accept Microsoft's licence terms first."),
    QT_TRANSLATE_NOOP("WinmodeService", "That licence choice isn't one Windows mode knows."),
    QT_TRANSLATE_NOOP("WinmodeService", "That doesn't look like a Windows product key. Check it and try again."),
    QT_TRANSLATE_NOOP("WinmodeService", "That file isn't there any more."),
    QT_TRANSLATE_NOOP("WinmodeService", "That isn't a Windows 11 ISO."),
    QT_TRANSLATE_NOOP("WinmodeService", "That file isn't a Windows 11 ISO from Microsoft, so it wasn't used."),
    QT_TRANSLATE_NOOP("WinmodeService", "That file couldn't be found."),
    QT_TRANSLATE_NOOP("WinmodeService", "Your Windows 11 file couldn't be checked. Try again."),
    QT_TRANSLATE_NOOP("WinmodeService", "This file isn't there any more."),
    QT_TRANSLATE_NOOP("WinmodeService", "Windows mode can't open this file."),
    QT_TRANSLATE_NOOP("WinmodeService", "This file isn't a copy of Windows."),
    QT_TRANSLATE_NOOP("WinmodeService", "This file is incomplete, so it may not have finished downloading."),
    QT_TRANSLATE_NOOP("WinmodeService", "This file couldn't be read as Windows 11."),
    QT_TRANSLATE_NOOP("WinmodeService", "This file is too small to be a full copy of Windows 11."),
    QT_TRANSLATE_NOOP("WinmodeService", "This file is an older version of Windows. Windows mode needs Windows 11."),
    QT_TRANSLATE_NOOP("WinmodeService", "This file is Windows 10. Windows mode needs Windows 11."),
    QT_TRANSLATE_NOOP("WinmodeService", "This file is Windows Server. Windows mode needs Windows 11."),
    QT_TRANSLATE_NOOP("WinmodeService", "This file is Windows 11 for ARM PCs. Windows mode needs the version for Intel and AMD PCs."),
    QT_TRANSLATE_NOOP("WinmodeService", "This file isn't the 64-bit version of Windows 11, which Windows mode needs."),
    QT_TRANSLATE_NOOP("WinmodeService", "This file doesn't include Windows 11 Pro."),

    // Activation's outcome, a reason followed by activationSuffix.
    QT_TRANSLATE_NOOP("WinmodeService", "Not activated yet."),
    QT_TRANSLATE_NOOP("WinmodeService", "This PC's licence is for another edition of Windows, usually Home, which Windows mode can't use."),
    QT_TRANSLATE_NOOP("WinmodeService", "Windows didn't accept that key. It may be for Home, which Windows mode can't use."),
    QT_TRANSLATE_NOOP("WinmodeService", "Microsoft says this key is for another edition of Windows, usually Home. Windows mode runs Windows 11 Pro, so it needs a Windows 11 Pro key."),
    QT_TRANSLATE_NOOP("WinmodeService", "Microsoft says this key is already in use on another PC, or on an earlier Windows mode. Microsoft may let you move it: open the Windows desktop, then Settings, Activation, and choose Troubleshoot."),
    QT_TRANSLATE_NOOP("WinmodeService", "Microsoft won't activate with this key. It may be blocked or not genuine. Check where it came from."),
    QT_TRANSLATE_NOOP("WinmodeService", "This is a volume licence key that activates through an organisation's own server, which Windows mode can't reach."),
    QT_TRANSLATE_NOOP("WinmodeService", "Microsoft found no licence for this PC with this key. Check the key is for Windows 11 Pro.")
];

const activationSuffix = QT_TRANSLATE_NOOP("WinmodeService", "Windows works without activating; add a Windows 11 Pro key any time in Settings.");

// "6.1 GB" as size_text writes it, in this language's units and decimals.
function size(text) {
    return text.replace(/\b(\d+(?:\.\d+)?) (GB|MB)\b/g, (whole, number, unit) => {
        const shown = Number(number).toLocaleString(Qt.locale(), "f", number.indexOf(".") >= 0 ? 1 : 0);
        return unit === "GB" ? qsTranslate("WinmodeService", "%1 GB").arg(shown) : qsTranslate("WinmodeService", "%1 MB").arg(shown);
    });
}

function tr(text) {
    if (!text) {
        return "";
    }
    const shown = qsTranslate(context, text);
    if (shown !== text) {
        return shown;
    }
    let m = /^About (\d+) minutes left$/.exec(text);
    if (m) {
        return qsTranslate("WinmodeService", "About %1 minutes left").arg(m[1]);
    }
    m = /^(Downloading|Installing) the parts Windows mode runs on, (\d+)%$/.exec(text);
    if (m) {
        return (m[1] === "Downloading" ? qsTranslate("WinmodeService", "Downloading the parts Windows mode runs on, %1%")
                                       : qsTranslate("WinmodeService", "Installing the parts Windows mode runs on, %1%")).arg(m[2]);
    }
    // A download under way: "Windows 11: 2.4 GB of 6.1 GB".
    m = /^(.+): (\d+(?:\.\d+)? [GM]B) of (\d+(?:\.\d+)? [GM]B)$/.exec(text);
    if (m) {
        return qsTranslate("WinmodeService", "%1: %2 of %3").arg(qsTranslate(context, m[1])).arg(size(m[2])).arg(size(m[3]));
    }
    m = /^Windows mode needs (\d+(?:\.\d+)? [GM]B) free and this PC has (\d+(?:\.\d+)? [GM]B)\.$/.exec(text);
    if (m) {
        return qsTranslate("WinmodeService", "Windows mode needs %1 free and this PC has %2.").arg(size(m[1])).arg(size(m[2]));
    }
    m = /^Windows didn't activate\. Microsoft's answer was 0x([0-9A-F]+); check the key, or try again later\.$/.exec(text);
    if (m) {
        return qsTranslate("WinmodeService", "Windows didn't activate. Microsoft's answer was 0x%1; check the key, or try again later.").arg(m[1]);
    }
    if (text.endsWith(" " + activationSuffix)) {
        return tr(text.slice(0, -activationSuffix.length - 1)) + " " + qsTranslate(context, activationSuffix);
    }
    // A known line with the error's own words after it: "The download stopped. (503)".
    m = /^(.+\.) (\(.*\))$/.exec(text);
    if (m && known.indexOf(m[1]) >= 0) {
        return qsTranslate(context, m[1]) + " " + m[2];
    }
    return text;
}
