import QtQuick
import QtQuick.Dialogs
import org.kde.kirigami as Kirigami
import org.exposuremg.unnamed

// Actions on the selected item / current folder, shared by the toolbar, the
// context menu and the details page, plus the dialogs they need. Every action
// is enabled from the filesystem's capabilities.
Item {
    id: root
    visible: false

    property alias open: openAction
    property alias properties: propertiesAction
    property alias extract: extractAction
    property alias replace: replaceAction
    property alias remove: removeAction
    property alias inject: injectAction

    signal propertiesRequested

    Kirigami.Action {
        id: openAction
        text: qsTr("Open")
        icon.name: "document-open-folder"
        enabled: FileBrowser.selectedIsDirectory
        onTriggered: FileBrowser.activate(FileBrowser.model.rowForName(FileBrowser.selectedName))
    }

    Kirigami.Action {
        id: propertiesAction
        text: qsTr("Properties")
        icon.name: "document-properties"
        enabled: FileBrowser.isOpen
        onTriggered: root.propertiesRequested()
    }

    Kirigami.Action {
        id: extractAction
        text: qsTr("Extract…")
        icon.name: "document-save-as"
        enabled: FileBrowser.canExtract
        onTriggered: extractDialog.open()
    }

    Kirigami.Action {
        id: replaceAction
        text: qsTr("Replace…")
        icon.name: "document-replace"
        enabled: FileBrowser.canReplace
        onTriggered: replaceDialog.open()
    }

    Kirigami.Action {
        id: removeAction
        text: qsTr("Delete")
        icon.name: "edit-delete"
        enabled: FileBrowser.canRemove
        onTriggered: deleteDialog.open()
    }

    Kirigami.Action {
        id: injectAction
        text: qsTr("Add Files…")
        icon.name: "list-add"
        enabled: FileBrowser.canInject
        onTriggered: injectDialog.open()
    }

    FolderDialog {
        id: extractDialog
        title: qsTr("Extract To")
        onAccepted: FileBrowser.extractSelected(selectedFolder)
    }

    FileDialog {
        id: replaceDialog
        title: qsTr("Replace With")
        onAccepted: FileBrowser.replaceSelected(selectedFile)
    }

    FileDialog {
        id: injectDialog
        title: qsTr("Add Files")
        fileMode: FileDialog.OpenFiles
        onAccepted: {
            for (const file of selectedFiles)
                FileBrowser.injectFile(file);
        }
    }

    Kirigami.PromptDialog {
        id: deleteDialog
        title: qsTr("Delete")
        subtitle: qsTr("Delete “%1”? This cannot be undone.").arg(FileBrowser.selectedName)
        dialogType: Kirigami.PromptDialog.Warning
        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
        onAccepted: FileBrowser.removeSelected()
    }
}
