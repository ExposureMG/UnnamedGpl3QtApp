import QtQuick
import QtQuick.Dialogs
import org.kde.kirigami as Kirigami
import org.exposuremg.unnamed

Kirigami.ApplicationWindow {
    id: root

    width: 900
    height: 600
    title: qsTr("Unnamed Gpl3 Qt App")

    FileBrowser {
        id: fileBrowser
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("Open Folder")
        onAccepted: fileBrowser.openFolder(selectedFolder)
    }

    Kirigami.Action {
        id: openAction
        text: qsTr("Open Folder…")
        icon.name: "folder-open"
        shortcut: StandardKey.Open
        onTriggered: folderDialog.open()
    }

    Kirigami.Action {
        id: quitAction
        text: qsTr("Quit")
        icon.name: "application-exit"
        shortcut: StandardKey.Quit
        onTriggered: Qt.quit()
    }

    globalDrawer: Kirigami.GlobalDrawer {
        title: root.title
        isMenu: true
        actions: [openAction, quitAction]
    }

    pageStack.initialPage: BrowserPage {
        browser: fileBrowser
        onOpenRequested: folderDialog.open()
    }
}
