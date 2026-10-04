import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.exposuremg.unnamed

QQC2.ToolBar {
    id: root

    property string pageTitle: qsTr("Unnamed Gpl3 Qt App")

    property bool canGoBack: false

    signal menuRequested
    signal backRequested
    signal openFolderRequested
    signal openDemoRequested
    signal openFatxRequested
    signal openDriveRequested

    position: QQC2.ToolBar.Header

    contentItem: RowLayout {
        spacing: 0

        QQC2.ToolButton {
            icon.name: "application-menu"
            display: QQC2.AbstractButton.IconOnly
            onClicked: root.menuRequested()

            QQC2.ToolTip.text: qsTr("Navigation menu")
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
        }

        QQC2.ToolButton {
            visible: root.canGoBack
            icon.name: "go-previous"
            display: QQC2.AbstractButton.IconOnly
            onClicked: root.backRequested()

            QQC2.ToolTip.text: qsTr("Close details")
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
        }

        Kirigami.Separator {
            Layout.fillHeight: true
            Layout.topMargin: Kirigami.Units.smallSpacing
            Layout.bottomMargin: Kirigami.Units.smallSpacing
        }

        QQC2.Label {
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.fillWidth: true
            text: root.pageTitle
            font.bold: true
            elide: Text.ElideRight
        }

        QQC2.ToolButton {
            id: transfersButton
            objectName: "transfersButton"
            visible: FileBrowser.jobs.count > 0
            icon.name: "folder-sync"
            text: FileBrowser.jobs.activeCount > 0 ? String(FileBrowser.jobs.activeCount) : ""
            display: text === "" ? QQC2.AbstractButton.IconOnly : QQC2.AbstractButton.TextBesideIcon
            onClicked: jobsPopup.opened ? jobsPopup.close() : jobsPopup.open()

            QQC2.ToolTip.text: qsTr("Transfers")
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

            JobsPopup {
                id: jobsPopup
                x: transfersButton.width - width
                y: transfersButton.height
            }
        }

        QQC2.ToolButton {
            id: openButton
            text: qsTr("Open")
            icon.name: "folder-open"
            display: QQC2.AbstractButton.TextBesideIcon
            Layout.rightMargin: Kirigami.Units.largeSpacing
            onClicked: openMenu.popup(openButton, 0, openButton.height)

            QQC2.Menu {
                id: openMenu

                QQC2.MenuItem {
                    text: qsTr("Open Folder…")
                    icon.name: "folder-open"
                    onTriggered: root.openFolderRequested()
                }
                QQC2.MenuItem {
                    text: qsTr("Open FATX Image…")
                    icon.name: "drive-harddisk"
                    visible: FileBrowser.fatxAvailable
                    height: visible ? implicitHeight : 0
                    onTriggered: root.openFatxRequested()
                }
                QQC2.MenuItem {
                    text: qsTr("Open Drive…")
                    icon.name: "drive-removable-media"
                    visible: FileBrowser.drivesAvailable
                    height: visible ? implicitHeight : 0
                    onTriggered: root.openDriveRequested()
                }
                QQC2.MenuItem {
                    text: qsTr("Open Demo (sample data)")
                    icon.name: "applications-development"
                    onTriggered: root.openDemoRequested()
                }
            }
        }
    }
}
