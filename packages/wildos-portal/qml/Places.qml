pragma ComponentBehavior: Bound

import QtCore
import QtQuick
import QtQuick.Controls
import WildOS.UI as UI

// The places column of DialogSave.dc: 190 wide, 32px rows with a 16px line
// icon and the name in 13px, the folder you are in lit with weight 600.
// The same places as the Files sidebar, in the same order: the Home
// folders and the folders pinned there, This computer, then the USB sticks
// and other disks that are mounted.
Item {
    id: places

    required property var folder

    readonly property var entries: {
        const home = StandardPaths.writableLocation(StandardPaths.HomeLocation);
        const list = [{ name: qsTr("Home"), icon: "home", url: home }];
        const add = (name, icon, location) => {
            const url = StandardPaths.writableLocation(location);
            if (url.toString().length > 0 && url.toString() !== home.toString()) {
                list.push({ name: name, icon: icon, url: url });
            }
        };
        add(qsTr("Desktop"), "desktop", StandardPaths.DesktopLocation);
        add(qsTr("Documents"), "document", StandardPaths.DocumentsLocation);
        add(qsTr("Downloads"), "download", StandardPaths.DownloadLocation);
        add(qsTr("Pictures"), "pictures", StandardPaths.PicturesLocation);
        add(qsTr("Music"), "music", StandardPaths.MusicLocation);
        add(qsTr("Videos"), "videos", StandardPaths.MoviesLocation);
        for (const pin of places.folder.pinned()) {
            if (!list.some(p => places.same(p.url, pin.url))) {
                list.push(pin);
            }
        }
        list.push({ name: qsTr("This computer"), icon: "computer", url: "wildos-computer:/" });
        for (const drive of places.folder.drives) {
            list.push({ name: drive.name, icon: drive.icon, url: drive.url });
        }
        return list;
    }

    function same(a, b) {
        const strip = s => s.toString().replace(/\/+$/, "");
        return strip(a) === strip(b);
    }

    Flickable {
        id: flick
        anchors.fill: parent
        anchors.margins: 10
        contentHeight: column.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: UI.ScrollBar {}

        Column {
            id: column
            width: flick.width
            spacing: 2

            Repeater {
                model: places.entries

                delegate: Rectangle {
                    id: place

                    required property var modelData
                    readonly property bool current: places.same(places.folder.url, modelData.url)

                    width: column.width
                    height: 32
                    radius: UI.Style.radiusXs
                    // Fades in on hover and clears at once, so moving along a list
                    // never lights two items. It comes before the colour so it sees
                    // the change in hover first.
                    Behavior on color {
                        enabled: placeMouse.containsMouse
                        ColorAnimation { duration: UI.Style.hoverDuration }
                    }
                    color: placeMouse.pressed ? UI.Style.surfacePressed : current || placeMouse.containsMouse ? UI.Style.surfaceHover : UI.Style.surfaceHoverClear
                    activeFocusOnTab: true
                    Accessible.role: Accessible.Button
                    Accessible.name: modelData.name
                    Accessible.onPressAction: places.folder.openUrl(modelData.url)
                    Keys.onReturnPressed: places.folder.openUrl(modelData.url)
                    Keys.onSpacePressed: places.folder.openUrl(modelData.url)

                    UI.FocusRing {
                        visible: place.activeFocus
                        targetRadius: place.radius
                    }

                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        x: 10
                        spacing: 10

                        UI.Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: place.modelData.icon
                            size: 16
                        }

                        UI.Label {
                            anchors.verticalCenter: parent.verticalCenter
                            width: place.width - 46
                            text: place.modelData.name
                            font.weight: place.current ? UI.Style.weightSemiBold : UI.Style.weightRegular
                        }
                    }

                    MouseArea {
                        id: placeMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: places.folder.openUrl(place.modelData.url)
                    }
                }
            }
        }
    }
}
