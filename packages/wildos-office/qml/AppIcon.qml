import QtQuick
import WildOS.Office

// Write's or Tables' icon from the App icons board, drawn from the copy in
// the program so it shows before the icon theme has caught up with it.
Image {
    property real size: 26

    width: size
    height: size
    source: Office.tables ? "data/org.wildos.tables.svg" : Office.slides ? "data/org.wildos.slides.svg" : "data/org.wildos.write.svg"
    sourceSize.width: Math.ceil(size * 2)
    sourceSize.height: Math.ceil(size * 2)
    fillMode: Image.PreserveAspectFit
    smooth: true
}
