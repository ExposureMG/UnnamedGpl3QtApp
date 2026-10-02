import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import org.kde.kirigami as Kirigami
import org.exposuremg.unnamed
import "components"

Kirigami.ApplicationWindow {
    id: root

    title: qsTr("Unnamed Gpl3 Qt App")
    width: 800
    height: 720
    minimumWidth: 380
    minimumHeight: 480
    maximumWidth: 1280

    pageStack.globalToolBar.style: Kirigami.ApplicationHeaderStyle.None

    header: StatusBar {
        id: statusBar

        pageTitle: root.pageStack.currentItem ? root.pageStack.currentItem.title : root.title

        onMenuRequested: root.globalDrawer.drawerOpen = !root.globalDrawer.drawerOpen
        onOpenFolderRequested: root.openFolderDialog()
    }

    globalDrawer: Kirigami.GlobalDrawer {
        id: navDrawer

        title: root.title
        modal: true
        width: Kirigami.Units.gridUnit * 14
        handleVisible: false

        actions: [
            Kirigami.Action {
                text: qsTr("Browser")
                onTriggered: root.switchPage("pages/Browser.qml")
            }
        ]

        footer: ColumnLayout {
            spacing: 0
            width: parent ? parent.width : undefined

            Kirigami.Separator {
                Layout.fillWidth: true
            }

            QQC2.ItemDelegate {
                text: qsTr("About")
                Layout.fillWidth: true
                onClicked: root.switchPage("pages/About.qml")
            }
        }
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("Open Folder")
        onAccepted: FileBrowser.openFolder(selectedFolder)
    }

    Shortcut {
        sequences: [StandardKey.Open]
        onActivated: root.openFolderDialog()
    }

    Shortcut {
        sequences: [StandardKey.Quit]
        onActivated: Qt.quit()
    }

    Kirigami.PromptDialog {
        id: globalErrorDialog
        title: qsTr("Error")
        subtitle: ""
        standardButtons: Kirigami.Dialog.NoButton
        showCloseButton: true
    }

    Connections {
        target: FileBrowser

        function onErrorMessageChanged() {
            if (FileBrowser.errorMessage !== "")
                root.showError(qsTr("Error"), FileBrowser.errorMessage);
        }
    }

    function showError(title, message) {
        globalErrorDialog.title = title || qsTr("Error");
        globalErrorDialog.subtitle = message || "";
        globalErrorDialog.open();
    }

    function openFolderDialog() {
        folderDialog.open();
    }

    function switchPage(url) {
        pageStack.clear();
        pageStack.push(Qt.resolvedUrl(url));
        navDrawer.drawerOpen = false;
    }

    Component.onCompleted: {
        pageStack.push(Qt.resolvedUrl("pages/Browser.qml"));
    }
}
