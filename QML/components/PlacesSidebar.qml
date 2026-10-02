import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.exposuremg.unnamed

// The open filesystems ("places"). Selecting one switches the browser to it.
ColumnLayout {
    id: root

    signal propertiesRequested
    signal placeSelected
    signal openFolderRequested

    spacing: 0

    Kirigami.Heading {
        Layout.fillWidth: true
        Layout.margins: Kirigami.Units.largeSpacing
        level: 5
        text: qsTr("Places")
        color: Kirigami.Theme.disabledTextColor
    }

    ListView {
        id: places
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        model: FileBrowser.mounts

        delegate: QQC2.ItemDelegate {
            id: place

            required property int index
            required property string name
            required property string kind
            required property string subtitle
            required property bool isCurrent

            width: ListView.view.width
            highlighted: isCurrent
            contentItem: Kirigami.IconTitleSubtitle {
                icon.name: place.kind === "Local" ? "folder" : "drive-harddisk"
                title: place.name
                subtitle: place.subtitle
                selected: place.highlighted
            }
            onClicked: {
                FileBrowser.selectMount(place.index);
                root.placeSelected();
            }

            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: placeMenu.openFor(place)
            }
            TapHandler {
                acceptedDevices: PointerDevice.TouchScreen
                onLongPressed: placeMenu.openFor(place)
            }
        }

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.largeSpacing * 2
            visible: places.count === 0
            text: qsTr("No places open")
        }
    }

    QQC2.Menu {
        id: placeMenu

        property int row: -1

        function openFor(item) {
            row = item.index;
            popup();
        }

        QQC2.MenuItem {
            text: qsTr("Properties")
            icon.name: "document-properties"
            onTriggered: {
                FileBrowser.selectMount(placeMenu.row);
                FileBrowser.inspectFileSystem = true;
                root.propertiesRequested();
            }
        }
        QQC2.MenuItem {
            text: qsTr("Close")
            icon.name: "dialog-close"
            onTriggered: FileBrowser.closeMount(placeMenu.row)
        }
    }

    Kirigami.Separator {
        Layout.fillWidth: true
    }

    QQC2.ToolButton {
        Layout.fillWidth: true
        text: qsTr("Open Folder…")
        icon.name: "folder-open"
        onClicked: root.openFolderRequested()
    }

    QQC2.ToolButton {
        Layout.fillWidth: true
        text: qsTr("Open Demo")
        icon.name: "applications-development"
        onClicked: FileBrowser.openDemo()
    }
}
