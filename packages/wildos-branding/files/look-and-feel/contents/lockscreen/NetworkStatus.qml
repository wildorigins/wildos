import QtQuick
import org.kde.plasma.networkmanagement as PlasmaNM
import WildOS.UI as UI

import "components"

// Whether the computer is on a network, in the corner beside the battery as
// Windows' lock screen shows it. It only shows: nothing here can be chosen
// or changed, and no network is named. The state is plasma-nm's own icon
// choice, as the taskbar reads it.
Item {
    id: net

    property Item backdrop: null

    readonly property string iconName: connectionIcon.connectionIcon
    readonly property string kind: {
        if (iconName.indexOf("flightmode") >= 0) {
            return "flight";
        }
        if (iconName.indexOf("wired") >= 0 && iconName.indexOf("disconnected") < 0 && iconName.indexOf("unavailable") < 0) {
            return "wired";
        }
        return /(wireless|mobile)-\d+/.test(iconName) ? "wifi" : "offline";
    }
    readonly property int strength: {
        const match = /-(\d+)/.exec(iconName);
        return match ? Number(match[1]) : 100;
    }

    width: 44
    height: 44
    Accessible.role: Accessible.StaticText
    Accessible.name: kind === "flight" ? qsTr("Flight mode is on")
        : kind === "offline" ? qsTr("Not connected to a network") : qsTr("Connected to a network")

    PlasmaNM.NetworkStatus {
        id: networkStatus
    }

    PlasmaNM.ConnectionIcon {
        id: connectionIcon
        connectivity: networkStatus.connectivity
    }

    Glass {
        anchors.fill: parent
        backdrop: net.backdrop
        tint: UI.Style.glassOnWallpaper
        blurRadius: 64
    }

    UI.Icon {
        anchors.centerIn: parent
        visible: net.kind === "wifi" || net.kind === "wired"
        name: net.kind === "wired" ? "ethernet" : net.strength >= 70 ? "wifi" : net.strength >= 45 ? "wifi-mid" : "wifi-low"
        size: 18
        strokeWidth: 2
        color: UI.Style.inkOnWallpaper
    }

    // Flight mode, or a globe struck through for no connection, as the
    // taskbar draws them.
    UI.Glyph {
        anchors.centerIn: parent
        visible: net.kind === "flight" || net.kind === "offline"
        width: 18
        height: 18
        strokeWidth: 1.8
        color: UI.Style.inkOnWallpaper
        path: net.kind === "flight" ? "M12 3v6M8.5 9h7M12 9l-9 6v2l9-3 9 3v-2zM12 14v5M9 21h6"
            : "M21 12a9 9 0 1 1-18 0a9 9 0 1 1 18 0zM3 12h18M12 3c2.4 2.5 3.6 5.5 3.6 9s-1.2 6.5-3.6 9c-2.4-2.5-3.6-5.5-3.6-9S9.6 5.5 12 3zM3.5 3.5l17 17"
    }
}
