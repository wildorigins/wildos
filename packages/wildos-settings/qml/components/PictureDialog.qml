import QtQuick
import QtQuick.Layouts
import WildOS.Settings
import WildOS.UI as UI

// Choosing the part of a photo that becomes the account picture: the photo
// fills a square, the circle shows what Start, the lock screen and the
// sign-in screen will show, drag to move it and zoom with the slider or the
// mouse wheel. The crop itself is made from the original file.
UI.Dialog {
    id: dialog

    property url file
    readonly property size natural: file.toString().length > 0 ? Users.pictureSize(file.toString()) : Qt.size(0, 0)
    readonly property real frameSize: 280
    readonly property real cover: natural.width > 0 && natural.height > 0 ? frameSize / Math.min(natural.width, natural.height) : 1
    property real zoom: 1
    readonly property real scaleNow: cover * zoom

    function clamp() {
        photo.x = Math.min(0, Math.max(frameSize - photo.width, photo.x));
        photo.y = Math.min(0, Math.max(frameSize - photo.height, photo.y));
    }

    function choose(url) {
        file = url;
        zoom = 1;
        zoomSlider.value = 1;
        photo.width = natural.width * cover;
        photo.height = natural.height * cover;
        photo.x = (frameSize - photo.width) / 2;
        photo.y = (frameSize - photo.height) / 2;
        open();
    }

    title: qsTr("Choose your picture")
    text: qsTr("Drag the photo to choose the part inside the circle.")
    onZoomChanged: {
        // Zoom about the middle of the circle.
        const cx = frameSize / 2;
        const before = photo.width;
        photo.width = natural.width * scaleNow;
        photo.height = natural.height * scaleNow;
        const ratio = before > 0 ? photo.width / before : 1;
        photo.x = cx - (cx - photo.x) * ratio;
        photo.y = cx - (cx - photo.y) * ratio;
        clamp();
    }

    Item {
        id: frame
        Layout.alignment: Qt.AlignHCenter
        Layout.preferredWidth: dialog.frameSize
        Layout.preferredHeight: dialog.frameSize
        clip: true

        Rectangle {
            anchors.fill: parent
            color: UI.Style.surfaceSunken
        }

        Image {
            id: photo
            source: dialog.file
            autoTransform: true
            asynchronous: true
            smooth: true
            mipmap: true
            fillMode: Image.Stretch
            // Big camera photos are shown from a smaller copy; the crop
            // still comes from the full picture.
            sourceSize.width: Math.min(dialog.natural.width, 1600)

            DragHandler {
                target: photo
                xAxis.minimum: dialog.frameSize - photo.width
                xAxis.maximum: 0
                yAxis.minimum: dialog.frameSize - photo.height
                yAxis.maximum: 0
                cursorShape: active ? Qt.ClosedHandCursor : Qt.OpenHandCursor
            }
        }

        WheelHandler {
            onWheel: event => {
                zoomSlider.value = Math.max(zoomSlider.from, Math.min(zoomSlider.to, zoomSlider.value + event.angleDelta.y / 600));
                dialog.zoom = zoomSlider.value;
            }
        }

        // The window colour, faded, outside the circle, so the circle reads
        // as the picture and the rest as what is cut away.
        Canvas {
            id: veil
            anchors.fill: parent
            readonly property color shade: Qt.rgba(UI.Style.windowOpaque.r, UI.Style.windowOpaque.g, UI.Style.windowOpaque.b, 0.62)
            readonly property color edge: UI.Style.borderControl
            onShadeChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d");
                ctx.reset();
                ctx.fillStyle = shade;
                ctx.fillRect(0, 0, width, height);
                ctx.globalCompositeOperation = "destination-out";
                ctx.beginPath();
                ctx.arc(width / 2, height / 2, width / 2 - 1, 0, 2 * Math.PI);
                ctx.fill();
                ctx.globalCompositeOperation = "source-over";
                ctx.strokeStyle = edge;
                ctx.lineWidth = 2;
                ctx.beginPath();
                ctx.arc(width / 2, height / 2, width / 2 - 1, 0, 2 * Math.PI);
                ctx.stroke();
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: 4
        spacing: 12

        UI.Icon {
            name: "minus"
            size: 14
            color: UI.Style.iconMuted
        }

        UI.Slider {
            id: zoomSlider
            Layout.fillWidth: true
            accessibleName: qsTr("Zoom")
            from: 1
            to: 4
            stepSize: 0
            value: dialog.zoom
            onMoved: dialog.zoom = value
        }

        UI.Icon {
            name: "plus"
            size: 14
            color: UI.Style.iconMuted
        }
    }

    buttons: [
        UI.Button {
            text: qsTr("Use this picture")
            kind: "primary"
            enabled: photo.status === Image.Ready
            onClicked: {
                const s = dialog.scaleNow;
                Users.setPicture(dialog.file.toString(), -photo.x / s, -photo.y / s, dialog.frameSize / s);
                dialog.close();
            }
        },
        UI.Button {
            text: qsTr("Cancel")
            onClicked: dialog.close()
        }
    ]
}
