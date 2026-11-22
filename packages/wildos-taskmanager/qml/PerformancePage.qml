import QtCore
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import WildOS.TaskManager
import WildOS.UI as UI

// Performance (TaskManagerPerf.dc): a 280px column of resources, each a
// 64px tile with its own 70 by 32 graph, and the chosen one's page: name
// and model, the 60 second graph, then its live stats, its specs and what
// is using it most. The processor adds an Overall / Each core switch and
// a row of small graphs, one per logical processor.
Item {
    id: page

    property string current: "cpu"
    property bool eachCore: false

    // Overall or Each core stays as last chosen, as in Windows' Task Manager.
    Settings {
        category: "Performance"
        property alias eachCore: page.eachCore
    }
    // Read again on every sample; the revision only changes when there is
    // a new one.
    readonly property var info: SystemStats.revision >= 0 ? SystemStats.details(current) : ({})
    readonly property color colour: Colours.resource(info.colour || "cpu")
    readonly property var cores: SystemStats.revision >= 0 && current === "cpu" ? SystemStats.coreHistories() : []

    signal message(string text)
    signal runTask()

    PageHeader {
        id: header
        width: parent.width
        title: qsTr("Performance")

        HeaderButton {
            text: qsTr("Run new task")
            iconName: "plus"
            onClicked: page.runTask()
        }
    }

    Flickable {
        id: side
        y: header.height
        width: 250
        height: parent.height - y
        contentHeight: tiles.implicitHeight + 20
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        T.ScrollBar.vertical: UI.ScrollBar {}

        Column {
            id: tiles
            x: 10
            y: 10
            width: 230
            spacing: 4

            Repeater {
                // Keyed on the list of resources, which changes only when a
                // disk or a connection comes or goes, so the tiles are not
                // rebuilt (and a click lost) on every sample.
                model: SystemStats.resourceIds

                ResourceTile {
                    required property string modelData
                    readonly property var resource: SystemStats.revision >= 0 ? SystemStats.resource(modelData) : ({})
                    width: tiles.width
                    title: resource.title || ""
                    subtitle: resource.subtitle || ""
                    values: resource.history || []
                    maximum: resource.max || 100
                    colour: Colours.resource(resource.colour || "cpu")
                    current: page.current === modelData
                    onClicked: page.current = modelData
                }
            }
        }
    }

    Rectangle {
        x: 250
        y: header.height
        width: 1
        height: parent.height - y
        color: UI.Style.divider
    }

    Flickable {
        id: detail
        x: 251
        y: header.height
        width: parent.width - x
        height: parent.height - y
        contentHeight: column.implicitHeight + 32
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        T.ScrollBar.vertical: UI.ScrollBar {}

        ColumnLayout {
            id: column
            x: 22
            y: 16
            width: detail.width - 44
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                spacing: 14

                UI.Label {
                    text: page.info.title || ""
                    size: 22
                    font.weight: UI.Style.weightSemiBold
                }

                UI.Label {
                    Layout.fillWidth: true
                    text: page.info.subtitle || ""
                    color: UI.Style.inkStatus
                }

                Segmented {
                    visible: page.current === "cpu"
                    options: [qsTr("Overall"), qsTr("Each core")]
                    currentIndex: page.eachCore ? 1 : 0
                    onActivated: index => page.eachCore = index === 1
                }
            }

            RowLayout {
                Layout.fillWidth: true

                UI.Label {
                    Layout.fillWidth: true
                    text: page.eachCore && page.current === "cpu" ? qsTr("% utilisation of each logical processor over 60 seconds") : (page.info.caption || "")
                    size: 11
                    color: UI.Style.inkAlpha(0.7, 0.64)
                }

                UI.Label {
                    text: page.info.scale || ""
                    size: 11
                    color: UI.Style.inkAlpha(0.7, 0.64)
                }
            }

            Graph {
                visible: !(page.eachCore && page.current === "cpu")
                Layout.fillWidth: true
                Layout.preferredHeight: 200
                values: page.info.history || []
                second: page.info.second || []
                maximum: page.info.max || 100
                colour: page.colour
                grid: true
            }

            // Each core: every logical processor's graph in a grid the
            // size of the large graph.
            Grid {
                id: coreGrid
                visible: page.eachCore && page.current === "cpu"
                Layout.fillWidth: true
                readonly property int n: Math.max(1, page.cores.length)
                // Cells about 1.6 times wider than tall, like the large graph,
                // so sixty seconds of samples are not crushed into a sliver.
                columns: {
                    // Of the two counts nearest the ideal, the one that
                    // leaves fewer empty cells in the last row (8 cores as
                    // 4 by 2 rather than 3, 3 and 2).
                    const ideal = Math.sqrt(n * column.width / (260 * 1.6));
                    const options = [Math.floor(ideal), Math.ceil(ideal)].map(c => Math.max(1, Math.min(n, c)));
                    const gap = c => Math.ceil(n / c) * c - n;
                    return gap(options[1]) < gap(options[0]) ? options[1] : options[0];
                }
                readonly property int rowsCount: Math.ceil(n / columns)
                spacing: 6
                readonly property real cellWidth: (column.width - (columns - 1) * spacing) / columns
                readonly property real cellHeight: Math.max(44, Math.min(cellWidth / 1.6, (260 - (rowsCount - 1) * spacing) / rowsCount))

                Repeater {
                    model: coreGrid.visible ? page.cores.length : 0

                    Graph {
                        required property int index
                        width: coreGrid.cellWidth
                        height: coreGrid.cellHeight
                        values: page.cores[index] || []
                        colour: page.colour
                        lineWidth: 1.2
                        edgeOpacity: 0.35
                        radius: 4
                    }
                }
            }

            // Overall: one small graph per logical processor under the
            // large one, eight to a row at the design's size.
            Grid {
                id: miniGrid
                visible: page.current === "cpu" && !page.eachCore
                Layout.fillWidth: true
                readonly property int n: page.cores.length
                // The design's eight to a row (about 70px each at the
                // window's default size): more processors wrap onto more
                // rows instead of squeezing, and a narrow window drops to
                // fewer columns before a graph goes under 64px.
                columns: Math.max(1, Math.min(8, n, Math.floor((column.width + columnSpacing) / (64 + columnSpacing))))
                columnSpacing: 8
                rowSpacing: 8
                // From the page's width, not the grid's own: the grid's width
                // comes from its children, so tying them to it never settles.
                readonly property real cellWidth: (column.width - (columns - 1) * columnSpacing) / columns

                Repeater {
                    model: miniGrid.visible ? page.cores.length : 0

                    Column {
                        required property int index
                        width: miniGrid.cellWidth
                        spacing: 3

                        Graph {
                            width: parent.width
                            height: 38
                            values: page.cores[index] || []
                            colour: page.colour
                            lineWidth: 1.2
                            edgeOpacity: 0.35
                            radius: 4
                        }

                        UI.Label {
                            width: parent.width
                            text: qsTr("Core %1").arg(index + 1)
                            size: 10
                            color: UI.Style.inkPlaceholder
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                spacing: 28

                GridLayout {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 11
                    Layout.alignment: Qt.AlignTop
                    columns: 3
                    rowSpacing: 12
                    columnSpacing: 18

                    Repeater {
                        model: (page.info.stats || []).length

                        Column {
                            required property int index
                            readonly property var modelData: page.info.stats[index] || ({})
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            spacing: 2

                            UI.Label {
                                width: parent.width
                                text: modelData.label
                                size: 11
                                color: UI.Style.inkAlpha(0.7, 0.64)
                            }

                            // Stays on one line: a value too long for its
                            // column at 18px is set a little smaller.
                            TextMetrics {
                                id: fit
                                font.family: UI.Style.fontFamily
                                font.letterSpacing: UI.Style.tracking(UI.Style.px(18))
                                font.pixelSize: UI.Style.px(18)
                                font.weight: UI.Style.weightMedium
                                text: modelData.value || ""
                            }

                            UI.Label {
                                width: parent.width
                                text: modelData.value
                                size: fit.advanceWidth > parent.width ? 15 : 18
                                font.weight: UI.Style.weightMedium
                                font.features: { "tnum": 1 }
                            }
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 12
                    Layout.alignment: Qt.AlignTop
                    implicitHeight: specs.implicitHeight

                    Rectangle {
                        width: 1
                        height: parent.height
                        color: UI.Style.tone(0.08)
                    }

                    Column {
                        id: specs
                        x: 22
                        width: parent.width - 22
                        spacing: 7

                        Repeater {
                            model: (page.info.specs || []).length

                            Item {
                                required property int index
                                readonly property var modelData: page.info.specs[index] || ({})
                                width: specs.width
                                height: Math.max(specLabel.implicitHeight, specValue.implicitHeight)

                                UI.Label {
                                    id: specLabel
                                    width: Math.min(implicitWidth, parent.width * 0.45)
                                    text: modelData.label
                                    size: 12
                                    color: UI.Style.inkAlpha(0.7, 0.64)
                                }

                                UI.Label {
                                    id: specValue
                                    anchors.right: parent.right
                                    width: Math.min(implicitWidth, parent.width - specLabel.width - 16)
                                    horizontalAlignment: Text.AlignRight
                                    text: modelData.value
                                    size: 12
                                }
                            }
                        }
                    }
                }
            }

            TopList {
                visible: (page.info.topTitle || "").length > 0
                Layout.fillWidth: true
                title: page.info.topTitle || ""
                metric: page.info.topMetric || ""
                total: page.info.topTotal || 1
                colour: page.colour
            }
        }
    }
}
