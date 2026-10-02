import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.exposuremg.unnamed

// Details-style list with sortable column headers. Compact widths hide the
// secondary columns.
ColumnLayout {
    id: root

    property alias view: list
    readonly property bool compact: width < Kirigami.Units.gridUnit * 30
    readonly property int kindWidth: Kirigami.Units.gridUnit * 11
    readonly property int sizeWidth: Kirigami.Units.gridUnit * 6
    readonly property int modifiedWidth: Kirigami.Units.gridUnit * 9
    readonly property int openWidth: Kirigami.Units.gridUnit * 2
    readonly property int iconWidth: Kirigami.Units.iconSizes.smallMedium

    signal contextRequested(int row, Item item, point position)
    signal deletePressed

    spacing: 0

    component SortHeader: QQC2.AbstractButton {
        id: header

        required property int key
        property alias align: label.horizontalAlignment
        readonly property bool active: FileBrowser.model.sortKey === key

        implicitHeight: Kirigami.Units.gridUnit * 1.8
        contentItem: RowLayout {
            QQC2.Label {
                id: label
                Layout.fillWidth: true
                text: header.text
                font.bold: header.active
                elide: Text.ElideRight
                color: header.active ? Kirigami.Theme.textColor : Kirigami.Theme.disabledTextColor
            }
            Kirigami.Icon {
                visible: header.active
                source: FileBrowser.model.sortAscending ? "arrow-up" : "arrow-down"
                implicitWidth: Kirigami.Units.iconSizes.small
                implicitHeight: Kirigami.Units.iconSizes.small
            }
        }
        onClicked: {
            if (active)
                FileBrowser.model.sortAscending = !FileBrowser.model.sortAscending;
            else {
                FileBrowser.model.sortKey = key;
                FileBrowser.model.sortAscending = true;
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.fillHeight: false
        Layout.leftMargin: Kirigami.Units.largeSpacing
        Layout.rightMargin: Kirigami.Units.largeSpacing
        spacing: Kirigami.Units.largeSpacing

        Item {
            implicitWidth: root.iconWidth
        }
        SortHeader {
            Layout.fillWidth: true
            text: qsTr("Name")
            key: FileSystemModel.SortByName
        }
        SortHeader {
            Layout.preferredWidth: root.kindWidth
            visible: !root.compact
            text: qsTr("Type")
            key: FileSystemModel.SortByKind
        }
        SortHeader {
            Layout.preferredWidth: root.sizeWidth
            text: qsTr("Size")
            key: FileSystemModel.SortBySize
        }
        SortHeader {
            Layout.preferredWidth: root.modifiedWidth
            visible: !root.compact
            text: qsTr("Modified")
            key: FileSystemModel.SortByModified
        }
        Item {
            implicitWidth: root.openWidth
        }
    }

    Kirigami.Separator {
        Layout.fillWidth: true
    }

    ListView {
        id: list
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        focus: true
        keyNavigationEnabled: true
        model: FileBrowser.model
        currentIndex: -1

        QQC2.ScrollBar.vertical: QQC2.ScrollBar {}

        // Selection <-> current index.
        Connections {
            target: FileBrowser
            function onSelectionChanged() {
                list.currentIndex = FileBrowser.model.rowForName(FileBrowser.selectedName);
            }
        }
        onCurrentIndexChanged: if (activeFocus && currentIndex >= 0)
            FileBrowser.select(currentIndex)

        Keys.onReturnPressed: FileBrowser.activate(currentIndex)
        Keys.onEnterPressed: FileBrowser.activate(currentIndex)
        Keys.onDeletePressed: root.deletePressed()
        Keys.onEscapePressed: FileBrowser.clearSelection()

        delegate: QQC2.ItemDelegate {
            id: entry

            required property int index
            required property string name
            required property bool isDirectory
            required property string kind
            required property string kindLabel
            required property string sizeText
            required property string modifiedText

            width: ListView.view.width
            leftPadding: Kirigami.Units.largeSpacing
            rightPadding: Kirigami.Units.largeSpacing
            highlighted: FileBrowser.selectedName === name
            onClicked: {
                list.forceActiveFocus();
                FileBrowser.select(index);
            }
            onDoubleClicked: FileBrowser.activate(index)

            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing

                KindIcon {
                    kind: entry.kind
                    implicitWidth: root.iconWidth
                    implicitHeight: root.iconWidth
                    selected: entry.highlighted
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    text: entry.name
                    elide: Text.ElideMiddle
                    color: entry.highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
                }
                QQC2.Label {
                    Layout.preferredWidth: root.kindWidth
                    visible: !root.compact
                    text: entry.kindLabel
                    elide: Text.ElideRight
                    color: entry.highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.disabledTextColor
                }
                QQC2.Label {
                    Layout.preferredWidth: root.sizeWidth
                    text: entry.sizeText
                    elide: Text.ElideRight
                    color: entry.highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.disabledTextColor
                }
                QQC2.Label {
                    Layout.preferredWidth: root.modifiedWidth
                    visible: !root.compact
                    text: entry.modifiedText
                    elide: Text.ElideRight
                    color: entry.highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.disabledTextColor
                }
                // Explicit "open" affordance for touch (double-tap is awkward).
                QQC2.ToolButton {
                    Layout.preferredWidth: root.openWidth
                    visible: entry.isDirectory
                    icon.name: "go-next"
                    display: QQC2.AbstractButton.IconOnly
                    text: qsTr("Open")
                    onClicked: FileBrowser.activate(entry.index)
                    QQC2.ToolTip.text: text
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                }
                Item {
                    visible: !entry.isDirectory
                    implicitWidth: root.openWidth
                }
            }

            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: (point, button) => {
                    FileBrowser.select(entry.index);
                    root.contextRequested(entry.index, entry, point.position);
                }
            }
            TapHandler {
                acceptedDevices: PointerDevice.TouchScreen
                onLongPressed: {
                    FileBrowser.select(entry.index);
                    root.contextRequested(entry.index, entry, point.position);
                }
            }
        }
    }
}
