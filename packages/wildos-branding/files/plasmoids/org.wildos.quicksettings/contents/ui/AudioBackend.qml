import QtQuick
import org.kde.plasma.private.volume

// Output volume through plasma-pa, on whichever device Plasma treats as the
// preferred output, exactly as the stock volume applet does.
QtObject {
    id: audio

    readonly property var sink: PreferredDevice.sink
    // PipeWire's placeholder sink appears when there is no real output.
    readonly property bool available: sink !== null && sink.name !== "auto_null"
    readonly property bool muted: available && sink.muted
    readonly property int percent: available ? Math.round(sink.volume / PulseAudio.NormalVolume * 100) : 0

    function setPercent(value: int): void {
        if (!available) {
            return;
        }
        sink.volume = Math.round(value / 100 * PulseAudio.NormalVolume);
        if (sink.muted && value > 0) {
            sink.muted = false;
        }
    }

    function toggleMute(): void {
        if (available) {
            sink.muted = !sink.muted;
        }
    }

    // The output devices that are plugged in, for the flyout's Sound output
    // page and the tray menu's Output list.
    readonly property var outputModel: PulseObjectFilterModel {
        filterOutInactiveDevices: true
        filterVirtualDevices: true
        sourceModel: SinkModel {}
    }

    // The same devices as { name, current, device }; choosing one makes it
    // the default, as the stock volume applet does.
    readonly property Instantiator outputDevices: Instantiator {
        model: audio.outputModel
        delegate: QtObject {
            required property var model
            readonly property string name: model.Description
            readonly property bool current: model.Default
            readonly property var device: model.PulseObject
        }
    }

    function outputs(): var {
        const list = [];
        for (let i = 0; i < outputDevices.count; ++i) {
            const output = outputDevices.objectAt(i);
            if (output && output.device && output.device.name !== "auto_null") {
                list.push(output);
            }
        }
        return list;
    }
}
