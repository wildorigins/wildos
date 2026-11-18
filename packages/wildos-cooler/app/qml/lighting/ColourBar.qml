import QtQuick

// A rounded bar running through up to six colours from left to right
// (10.11: the sample above each effect's name).
Rectangle {
    id: bar

    property var colours: ["black"]
    readonly property int last: Math.max(0, colours.length - 1)

    function at(i) {
        return colours[Math.min(i, last)];
    }

    function where(i) {
        return last === 0 ? i / 5 : Math.min(1, i / last);
    }

    radius: height / 2
    gradient: Gradient {
        orientation: Gradient.Horizontal
        GradientStop { position: bar.where(0); color: bar.at(0) }
        GradientStop { position: bar.where(1); color: bar.at(1) }
        GradientStop { position: bar.where(2); color: bar.at(2) }
        GradientStop { position: bar.where(3); color: bar.at(3) }
        GradientStop { position: bar.where(4); color: bar.at(4) }
        GradientStop { position: bar.where(5); color: bar.at(5) }
    }
}
