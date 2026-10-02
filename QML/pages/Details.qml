import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.exposuremg.unnamed
import "../components"

// Expanded view. Kirigami's page row shows it next to the browser when the
// window is wide and as a separate, stacked page (with back) when it is narrow.
Kirigami.ScrollablePage {
    id: root

    title: qsTr("Details")

    // The application window (Main.qml); provides the shared actions.
    required property var app

    Kirigami.ColumnView.fillWidth: false
    implicitWidth: Kirigami.Units.gridUnit * 22

    header: QQC2.TabBar {
        currentIndex: FileBrowser.inspectFileSystem ? 1 : 0
        onCurrentIndexChanged: FileBrowser.inspectFileSystem = currentIndex === 1

        QQC2.TabButton {
            text: qsTr("Item")
        }
        QQC2.TabButton {
            text: qsTr("Filesystem")
        }
    }

    DetailsView {
        details: FileBrowser.inspectFileSystem ? FileBrowser.fileSystemDetails : FileBrowser.itemDetails
        showActions: !FileBrowser.inspectFileSystem && FileBrowser.hasSelection
        actions: root.app.itemActions
    }
}
