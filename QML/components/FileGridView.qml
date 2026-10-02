import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.exposuremg.unnamed

// Icon grid. Cells stretch so each row is filled evenly.
GridView {
    id: root

    readonly property int minCell: Kirigami.Units.gridUnit * 7

    signal contextRequested(int row, Item item, point position)
    signal deletePressed

    clip: true
    focus: true
    keyNavigationEnabled: true
    model: FileBrowser.model
    currentIndex: -1
    cellWidth: Math.floor(width / Math.max(1, Math.floor(width / minCell)))
    cellHeight: Kirigami.Units.gridUnit * 7

    QQC2.ScrollBar.vertical: QQC2.ScrollBar {}

    Connections {
        target: FileBrowser
        function onSelectionChanged() {
            root.currentIndex = FileBrowser.model.rowForName(FileBrowser.selectedName);
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

        width: root.cellWidth
        height: root.cellHeight
        highlighted: FileBrowser.selectedName === name
        onClicked: {
            root.forceActiveFocus();
            FileBrowser.select(index);
        }
        onDoubleClicked: FileBrowser.activate(index)

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            KindIcon {
                Layout.alignment: Qt.AlignHCenter
                kind: entry.kind
                implicitWidth: Kirigami.Units.iconSizes.large
                implicitHeight: Kirigami.Units.iconSizes.large
                selected: entry.highlighted
            }
            QQC2.Label {
                Layout.fillWidth: true
                Layout.fillHeight: true
                text: entry.name
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignTop
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                elide: Text.ElideRight
                maximumLineCount: 2
                color: entry.highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
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
