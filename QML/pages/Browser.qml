import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.exposuremg.unnamed

Kirigami.Page {
    id: root
    title: qsTr("Browser")

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Kirigami.Units.largeSpacing
        spacing: Kirigami.Units.largeSpacing

        Kirigami.AbstractCard {
            Layout.fillWidth: true

            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing

                Kirigami.Icon {
                    implicitWidth: Kirigami.Units.iconSizes.medium
                    implicitHeight: Kirigami.Units.iconSizes.medium
                    source: FileBrowser.isOpen ? "folder" : "folder-symbolic"
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing / 2

                    QQC2.Label {
                        text: FileBrowser.isOpen ? FileBrowser.rootName : qsTr("No Folder Open")
                        font.bold: true
                        font.pointSize: Kirigami.Theme.defaultFont.pointSize + 1
                    }

                    QQC2.Label {
                        text: FileBrowser.isOpen ? FileBrowser.rootPath + (FileBrowser.path === "/" ? "" : FileBrowser.path) : qsTr("Click 'Open Folder' in statusbar or browse to load a folder")
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                        color: Kirigami.Theme.disabledTextColor
                    }
                }

                QQC2.Button {
                    icon.name: "go-up"
                    text: qsTr("Up")
                    enabled: FileBrowser.canGoUp
                    onClicked: FileBrowser.goUp()
                }

                QQC2.Button {
                    icon.name: "folder-open"
                    text: qsTr("Browse…")
                    onClicked: root.Window.window.openFolderDialog()
                }
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: FileBrowser.model

            QQC2.ScrollBar.vertical: QQC2.ScrollBar {}

            delegate: QQC2.ItemDelegate {
                id: entry

                required property int index
                required property string name
                required property bool isDirectory
                required property string sizeText

                width: ListView.view.width
                contentItem: Kirigami.IconTitleSubtitle {
                    icon.name: entry.isDirectory ? "folder" : "text-x-generic"
                    title: entry.name
                    subtitle: entry.isDirectory ? qsTr("Folder") : entry.sizeText
                }
                onClicked: FileBrowser.activate(entry.index)
            }

            Kirigami.PlaceholderMessage {
                anchors.centerIn: parent
                width: parent.width - Kirigami.Units.gridUnit * 4
                visible: list.count === 0 && FileBrowser.isOpen
                text: qsTr("This folder is empty")
            }
        }

        QQC2.Label {
            text: FileBrowser.statusText
            color: Kirigami.Theme.disabledTextColor
        }
    }
}
