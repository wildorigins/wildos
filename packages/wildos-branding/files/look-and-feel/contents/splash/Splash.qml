/*
    SPDX-FileCopyrightText: 2026 Wild OS
    SPDX-License-Identifier: GPL-3.0-or-later
*/

import QtQuick
import org.kde.plasma.workspace.dbus as DBus

// Shown by ksplashqml between signing in and the desktop.
//
// After the sign-in screen it holds that screen's photo, so signing in reads
// as the controls clearing away and the desktop arriving on the same picture.
// When the system signs in by itself straight after boot (the live system, or
// automatic sign-in after unlocking the disk), there was no sign-in screen to
// finish the start-up animation, so this opens on Plymouth's last frame and
// plays the bloom into the wallpaper instead, with the same component the
// sign-in screen uses.
//
// ksplashqml closes the moment Plasma reports ready, about a second after it
// appears, which cut the animation off part way. The ready report now waits
// for this splash to say it has finished (libexec/splash-hold), and the splash
// then fades to the desktop, already drawn underneath.
Item {
    id: root

    // Set by ksplashqml: 1 when the splash appears, then one step per session
    // milestone. ksplashqml closes the window when it reaches 7.
    property int stage

    // By stage 6 Plasma has drawn the desktop, so the splash clears early and
    // the last step lands on the desktop instead of a hard cut.
    readonly property int readyStage: 6
    readonly property string wallpaper: "/usr/share/wallpapers/WildOS/contents/images/3840x2160.jpg"
    readonly property bool booting: handoff.status === Loader.Ready && handoff.item.active
    // Stays set once the animation has played, so the frame it ends on (the
    // wallpaper framed as the desktop frames it, no scrim) is the one that
    // fades.
    property bool played: false
    // Plasma has drawn the desktop under this window, taskbar and Start's
    // unseen first draw included (Session.desktopReady in wildos-shell).
    // After a boot the W then opens onto it, so by the end of the flight
    // there is nothing left to fade in. Plasma's own "desktop" stage comes
    // before the taskbar has loaded, so it is not enough on its own.
    readonly property bool live: desktopReady.registered

    DBus.DBusServiceWatcher {
        id: desktopReady
        busType: DBus.BusType.Session
        watchedService: "org.wildos.shell.DesktopReady"
    }
    readonly property bool showingDesktop: live && (booting || played)
    // Same decode as the start-up animation's photo, so the two share it.
    readonly property real dpr: Screen.devicePixelRatio > 0 ? Screen.devicePixelRatio : 1
    readonly property int photoDecodeWidth: Math.ceil(1.18 * Math.max(width * dpr, height * dpr * 16 / 9))

    function reveal() {
        if (stage < readyStage || booting || fadeOut.running || content.opacity <= 0)
            return;
        if (played) {
            // The desktop is showing through: nothing left to uncover. Until
            // then the photo stays and the backdrop crossfades onto the
            // desktop when it is ready, unless that never came.
            if (backdrop.opacity === 0)
                content.opacity = 0;
            else if (!showingDesktop && doneSent)
                fadeOut.start();
            return;
        }
        fadeOut.start();
    }

    // Tells splash-hold the splash can close. After the animation that is
    // once the desktop is actually showing, so closing never cuts off the
    // crossfade onto it; if the desktop never says it is ready, a little
    // later anyway.
    property bool doneSent: false
    function sendDone() {
        if (doneSent)
            return;
        doneSent = true;
        DBus.SessionBus.asyncCall({
            service: "org.freedesktop.systemd1",
            path: "/org/freedesktop/systemd1",
            iface: "org.freedesktop.systemd1.Manager",
            member: "StartUnit",
            arguments: [new DBus.string("wildos-splash-done.target"), new DBus.string("replace")],
            signature: "(ss)"
        });
        reveal();
    }
    function maybeDone() {
        if (played && !booting && backdrop.opacity === 0)
            sendDone();
    }
    Timer {
        id: doneFallback
        interval: 3000
        onTriggered: root.sendDone()
    }

    onStageChanged: reveal()
    onLiveChanged: reveal()
    onBootingChanged: {
        if (booting) {
            played = true;
            return;
        }
        if (played) {
            maybeDone();
            doneFallback.start();
        }
        reveal();
    }

    Item {
        id: content
        anchors.fill: parent

        // Inside what fades, so the fade uncovers the desktop rather than a
        // dark screen. Gives way to the desktop as soon as it is drawn, in
        // case the flight ended before it was.
        Item {
            id: backdrop
            anchors.fill: parent
            opacity: root.showingDesktop ? 0 : 1
            visible: opacity > 0
            onOpacityChanged: root.maybeDone()
            // Long enough for the desktop to paint its wallpaper underneath:
            // it draws its first full frame only once something above it lets
            // it show, about a third of a second on software rendering.
            Behavior on opacity {
                NumberAnimation { duration: 400; easing.type: Easing.InOutQuad }
            }

            Rectangle {
                anchors.fill: parent
                color: "#000000"
            }

            Image {
                id: photo

                readonly property real coverScale: implicitWidth > 0
                    ? Math.max(root.width / implicitWidth, root.height / implicitHeight) : 1

                // Framed as the desktop frames its wallpaper after a boot, as
                // the sign-in screen frames it otherwise.
                width: implicitWidth * coverScale
                height: implicitHeight * coverScale
                x: (root.width - width) / 2
                y: (root.height - height) * (root.played ? 0.5 : 0.45)
                source: "file://" + root.wallpaper
                sourceSize.width: root.photoDecodeWidth
                asynchronous: false
                cache: true
                smooth: true
            }

            Rectangle {
                anchors.fill: parent
                visible: !root.played
                color: Qt.rgba(15 / 255, 17 / 255, 21 / 255, 0.22)
            }
        }

        // Installed with the sign-in screen by the same package.
        Loader {
            id: handoff
            anchors.fill: parent
            Component.onCompleted: setSource("file:///usr/share/sddm/themes/wildos/components/BootHandoff.qml", {
                marker: "file:///run/wildos/boot-handoff-session.png",
                photoSource: photo.source,
                photoItem: photo,
                photoFocus: 0.5,
                scrim: false,
                endOnLanding: true
            })
        }

        Binding {
            target: handoff.item
            property: "live"
            value: root.live
            when: handoff.status === Loader.Ready
        }
    }

    SequentialAnimation {
        id: fadeOut
        PauseAnimation {
            duration: 300
        }
        NumberAnimation {
            target: content
            property: "opacity"
            to: 0
            duration: 450
            easing.type: Easing.InOutQuad
        }
    }
}
