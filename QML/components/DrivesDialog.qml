import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.exposuremg.unnamed

// Picks a physical drive to open. Drives open read-write when possible and
// read-only otherwise (FileBrowser reports why); places can switch either way.
Kirigami.Dialog {
    id: root
    objectName: "drivesDialog"

    property string selectedPath: ""

    title: qsTr("Open Drive")
    padding: 0
    preferredWidth: Kirigami.Units.gridUnit * 30
    preferredHeight: Kirigami.Units.gridUnit * 22
    standardButtons: Kirigami.Dialog.Cancel
    customFooterActions: [
        Kirigami.Action {
            text: qsTr("Open")
            icon.name: "document-open"
            enabled: root.selectedPath !== ""
            onTriggered: {
                FileBrowser.openDrive(root.selectedPath);
                root.close();
            }
        }
    ]

    onOpened: {
        selectedPath = "";
        FileBrowser.refreshDrives(loopBox.checked);
    }

    ColumnLayout {
        spacing: 0

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            visible: true
            type: Kirigami.MessageType.Information
            text: qsTr("Drives open for reading and writing when possible; a drive that is mounted, read-only or not allowed opens read-only and says why. Use Make Read-only on a place to protect it. Drives you cannot open yet ask for permission.")
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: false
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.smallSpacing

            QQC2.CheckBox {
                id: loopBox
                text: qsTr("Show loop devices")
                onToggled: FileBrowser.refreshDrives(checked)
            }
            Item {
                Layout.fillWidth: true
            }
            QQC2.BusyIndicator {
                Layout.fillWidth: false
                Layout.preferredHeight: Kirigami.Units.iconSizes.smallMedium
                Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
                running: FileBrowser.drivesLoading
                visible: running
            }
            QQC2.ToolButton {
                icon.name: "view-refresh"
                text: qsTr("Refresh")
                enabled: !FileBrowser.drivesLoading
                onClicked: FileBrowser.refreshDrives(loopBox.checked)
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        ListView {
            id: list
            objectName: "drivesList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: Kirigami.Units.gridUnit * 12
            clip: true
            model: FileBrowser.drives

            delegate: QQC2.ItemDelegate {
                id: drive

                required property var modelData

                width: ListView.view.width
                highlighted: root.selectedPath === modelData.path
                onClicked: root.selectedPath = modelData.path
                onDoubleClicked: {
                    FileBrowser.openDrive(modelData.path);
                    root.close();
                }

                contentItem: RowLayout {
                    spacing: Kirigami.Units.largeSpacing

                    Kirigami.IconTitleSubtitle {
                        Layout.fillWidth: true
                        icon.name: drive.modelData.removable ? "drive-removable-media" : "drive-harddisk"
                        title: drive.modelData.model !== "" ? drive.modelData.model : drive.modelData.path
                        subtitle: [drive.modelData.model !== "" ? drive.modelData.path : "", drive.modelData.size, drive.modelData.note]
                                  .filter(s => s !== "").join(" · ")
                        selected: drive.highlighted
                    }
                    QQC2.Label {
                        Layout.fillWidth: false
                        Layout.rightMargin: Kirigami.Units.smallSpacing
                        visible: drive.modelData.xbox
                        text: qsTr("Xbox 360")
                        font.bold: true
                        color: Kirigami.Theme.positiveTextColor
                    }
                    Kirigami.Icon {
                        Layout.fillWidth: false
                        Layout.rightMargin: Kirigami.Units.smallSpacing
                        Layout.preferredWidth: Kirigami.Units.iconSizes.small
                        Layout.preferredHeight: Kirigami.Units.iconSizes.small
                        visible: drive.modelData.inUse || drive.modelData.readOnly
                        source: drive.modelData.inUse ? "emblem-warning" : "object-locked"
                    }
                }
            }

            Kirigami.PlaceholderMessage {
                anchors.centerIn: parent
                width: parent.width - Kirigami.Units.largeSpacing * 4
                visible: list.count === 0 && !FileBrowser.drivesLoading
                icon.name: "drive-harddisk"
                text: qsTr("No drives found")
                explanation: qsTr("Connect the Xbox 360 drive (for example with a SATA to USB adapter), then refresh.")
            }
        }
    }
}
