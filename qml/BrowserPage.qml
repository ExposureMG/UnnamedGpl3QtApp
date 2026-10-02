import QtQuick
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami
import org.exposuremg.unnamed

Kirigami.ScrollablePage {
    id: page

    required property FileBrowser browser

    signal openRequested()

    title: browser.isOpen ? browser.path : qsTr("No folder open")

    actions: [
        Kirigami.Action {
            text: qsTr("Up")
            icon.name: "go-up"
            shortcut: "Alt+Up"
            enabled: page.browser.canGoUp
            onTriggered: page.browser.goUp()
        },
        Kirigami.Action {
            id: openFolderAction
            text: qsTr("Open Folder…")
            icon.name: "folder-open"
            onTriggered: page.openRequested()
        }
    ]

    header: Kirigami.InlineMessage {
        type: Kirigami.MessageType.Error
        position: Kirigami.InlineMessage.Position.Header
        text: page.browser.errorMessage
        visible: text.length > 0
    }

    footer: Controls.ToolBar {
        contentItem: Controls.Label {
            text: page.browser.statusText
            leftPadding: Kirigami.Units.largeSpacing
        }
    }

    ListView {
        id: list
        model: page.browser.model
        clip: true

        delegate: Controls.ItemDelegate {
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
            onClicked: page.browser.activate(entry.index)
        }

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 4
            visible: list.count === 0
            text: page.browser.isOpen ? qsTr("This folder is empty") : qsTr("Open a folder to begin")
            helpfulAction: page.browser.isOpen ? null : openFolderAction
        }
    }
}
