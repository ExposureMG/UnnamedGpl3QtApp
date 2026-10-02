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
    width: 1000
    height: 720
    minimumWidth: 380
    minimumHeight: 480
    maximumWidth: 1280

    pageStack.globalToolBar.style: Kirigami.ApplicationHeaderStyle.None

    header: StatusBar {
        id: statusBar

        pageTitle: root.pageStack.currentItem ? root.pageStack.currentItem.title : root.title

        onMenuRequested: root.globalDrawer.drawerOpen = !root.globalDrawer.drawerOpen
        canGoBack: root.pageStack.depth > 1
        onBackRequested: root.hideDetails()
        onOpenFolderRequested: root.openFolderDialog()
        onOpenDemoRequested: FileBrowser.openDemo()
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
                onTriggered: root.switchPage("pages/Browser.qml", {"app": root})
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

    // Shared by the browser toolbar, context menu and details page.
    ItemActions {
        id: itemActions
        onPropertiesRequested: root.showDetails(false)
    }
    property alias itemActions: itemActions

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

        function onErrorOccurred(message) {
            root.showError(qsTr("Error"), message);
        }
        function onNotice(message) {
            root.showPassiveNotification(message);
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

    // The expanded view is a second page in the page row (side by side when
    // the window is wide, stacked with a back button when narrow).
    readonly property bool detailsOpen: pageStack.depth > 1

    function showDetails(fileSystemTab) {
        if (fileSystemTab !== undefined)
            FileBrowser.inspectFileSystem = fileSystemTab;
        if (!detailsOpen)
            pageStack.push(Qt.resolvedUrl("pages/Details.qml"), {"app": root});
        else
            pageStack.currentIndex = 1;
    }

    function hideDetails() {
        if (detailsOpen)
            pageStack.pop();
    }

    function toggleDetails() {
        if (detailsOpen)
            hideDetails();
        else
            showDetails();
    }

    function switchPage(url, properties) {
        pageStack.clear();
        pageStack.push(Qt.resolvedUrl(url), properties || {});
        navDrawer.drawerOpen = false;
    }

    Component.onCompleted: {
        pageStack.push(Qt.resolvedUrl("pages/Browser.qml"), {"app": root});
    }
}
