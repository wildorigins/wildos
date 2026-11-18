pragma Singleton

import QtQuick

// Numbers the way Windows Task Manager writes them: one decimal for CPU and
// rates, whole megabytes with thousands separators for memory, with the
// separators of the regional format chosen in Settings (LC_NUMERIC).
QtObject {
    readonly property var locale: Qt.locale()

    function cpu(value) {
        return Number(value || 0).toLocaleString(locale, "f", 1) + "%";
    }

    function percent(value) {
        return Math.round(value || 0) + "%";
    }

    function memory(bytes) {
        return qsTr("%1 MB").arg(Number(Math.round((bytes || 0) / 1048576)).toLocaleString(locale, "f", 0));
    }

    function disk(bytesPerSecond) {
        const mb = (bytesPerSecond || 0) / 1048576;
        return qsTr("%1 MB/s").arg(mb === 0 ? "0" : Number(Math.max(mb, 0.1)).toLocaleString(locale, "f", 1));
    }

    function network(bytesPerSecond) {
        const mbps = (bytesPerSecond || 0) * 8 / 1000000;
        return qsTr("%1 Mbps").arg(mbps === 0 ? "0" : Number(Math.max(mbps, 0.1)).toLocaleString(locale, "f", 1));
    }
}
