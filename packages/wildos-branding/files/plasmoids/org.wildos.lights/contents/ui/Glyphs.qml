pragma Singleton

import QtQuick

// The board's line icons (TaskbarCamMic.dc), on a 24 px grid.
QtObject {
    readonly property string mic: "M12 2a3 3 0 0 0-3 3v7a3 3 0 0 0 6 0V5a3 3 0 0 0-3-3zM19 10v2a7 7 0 0 1-14 0v-2M12 19v3"
    readonly property string micOff: "M2 2l20 20M18.89 13.23A7.12 7.12 0 0 0 19 12v-2M5 10v2a7 7 0 0 0 12 5M15 9.34V5a3 3 0 0 0-5.68-1.33M9 9v3a3 3 0 0 0 5.12 2.12M12 19v3"
    readonly property string camera: "M16 13l5.223 3.482a0.5 0.5 0 0 0 0.777-0.416V7.87a0.5 0.5 0 0 0-0.752-0.432L16 10.5M4 6h10a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2z"
    readonly property string cameraOff: "M10.66 6H14a2 2 0 0 1 2 2v2.5l5.248-3.062A0.5 0.5 0 0 1 22 7.87v8.196M16 16a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h2M2 2l20 20"
}
