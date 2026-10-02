import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.exposuremg.unnamed

// Clickable breadcrumb of the current folder.
Flickable {
    id: root

    implicitHeight: row.implicitHeight
    contentWidth: row.implicitWidth
    contentHeight: height
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    flickableDirection: Flickable.HorizontalFlick

    // Keep the current (last) segment visible.
    onContentWidthChanged: contentX = Math.max(0, contentWidth - width)

    Row {
        id: row
        height: root.height

        Repeater {
            model: FileBrowser.pathSegments

            delegate: Row {
                id: segment

                required property int index
                required property var modelData
                readonly property bool last: index === FileBrowser.pathSegments.length - 1

                height: row.height

                QQC2.ToolButton {
                    height: parent.height
                    text: segment.modelData.name
                    font.bold: segment.last
                    onClicked: FileBrowser.navigateTo(segment.modelData.path)
                }

                QQC2.Label {
                    height: parent.height
                    visible: !segment.last
                    text: "›"
                    verticalAlignment: Text.AlignVCenter
                    color: Kirigami.Theme.disabledTextColor
                }
            }
        }
    }
}
