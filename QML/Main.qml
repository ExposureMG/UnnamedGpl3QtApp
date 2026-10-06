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
        onOpenFatxRequested: root.openFatxDialog()
        onOpenDriveRequested: root.openDriveDialog()
    }

    globalDrawer: Kirigami.GlobalDrawer {
        id: navDrawer

        title: root.title
        modal: true
        width: Kirigami.Units.gridUnit * 14
        handleVisible: false
        // Kirigami slides a hidden handle only most of the way off the edge.
        Component.onCompleted: handle.visible = false

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

    FileDialog {
        id: fatxDialog
        title: qsTr("Open FATX Image")
        nameFilters: [qsTr("Disk and partition images (*.img *.bin *.fatx *.raw *.dd)"), qsTr("All files (*)")]
        onAccepted: FileBrowser.openFatxImage(selectedFile)
    }

    DrivesDialog {
        id: drivesDialog
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

    // Results of long operations, e.g. a filesystem health check.
    Kirigami.Dialog {
        id: reportDialog
        property alias text: reportText.text
        standardButtons: Kirigami.Dialog.Close
        padding: Kirigami.Units.largeSpacing
        preferredWidth: Kirigami.Units.gridUnit * 34
        maximumWidth: preferredWidth

        // Reports line up columns with spaces.
        QQC2.Label {
            id: reportText
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
            font: Kirigami.Theme.fixedWidthFont
        }
    }

    // Writing to a place needs an explicit unlock.
    Kirigami.PromptDialog {
        id: unlockDialog
        property int row: -1
        property string placeName: ""
        property string device: ""
        property bool isDrive: false
        title: qsTr("Enable Writing")
        preferredWidth: Kirigami.Units.gridUnit * 26
        subtitle: qsTr("Enable writing to “%1”? Changes are written to %2 immediately; keep a copy of the image if it matters.")
                  .arg(placeName).arg(device)
        dialogType: Kirigami.PromptDialog.Warning
        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
        onAccepted: FileBrowser.setMountWritable(row, true)
    }

    // Repair writes fsck's fixes to the filesystem.
    Kirigami.PromptDialog {
        id: repairDialog
        property int row: -1
        property string placeName: ""
        property string device: ""
        title: qsTr("Repair Filesystem")
        subtitle: qsTr("Repair “%1” on %2? The health check's fixes are written immediately; files in damaged areas may be cut short or removed.")
                  .arg(placeName).arg(device)
        dialogType: Kirigami.PromptDialog.Warning
        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
        onAccepted: FileBrowser.repairMount(row)
    }

    // Formatting erases a whole partition: type the place's name to confirm.
    Kirigami.Dialog {
        id: formatDialog
        objectName: "formatDialog"
        property int row: -1
        property string placeName: ""
        property string device: ""
        property bool isDrive: false
        title: qsTr("Format")
        padding: Kirigami.Units.largeSpacing
        preferredWidth: Kirigami.Units.gridUnit * 26
        standardButtons: Kirigami.Dialog.Cancel
        customFooterActions: [
            Kirigami.Action {
                text: qsTr("Format")
                icon.name: "edit-delete-shred"
                enabled: formatConfirm.text === formatDialog.placeName
                onTriggered: {
                    FileBrowser.formatMount(formatDialog.row, formatLabel.text);
                    formatDialog.close();
                }
            }
        ]
        onOpened: formatConfirm.forceActiveFocus()

        ColumnLayout {
            spacing: Kirigami.Units.largeSpacing

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: true
                type: Kirigami.MessageType.Error
                text: (formatDialog.isDrive
                       ? qsTr("Everything in “%1” on the drive %2 will be erased. This cannot be undone.")
                       : qsTr("Everything in “%1” (%2) will be erased. This cannot be undone."))
                      .arg(formatDialog.placeName).arg(formatDialog.device)
            }
            QQC2.Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Type the name of the place to confirm: %1").arg(formatDialog.placeName)
            }
            QQC2.TextField {
                id: formatConfirm
                objectName: "formatConfirm"
                Layout.fillWidth: true
                placeholderText: formatDialog.placeName
            }
            QQC2.Label {
                text: qsTr("New label")
            }
            QQC2.TextField {
                id: formatLabel
                Layout.fillWidth: true
                maximumLength: 42
                text: "XBOX"
            }
        }
    }

    Kirigami.PromptDialog {
        id: globalErrorDialog
        title: qsTr("Error")
        subtitle: ""
        // Long messages wrap instead of widening the dialog to the window.
        preferredWidth: Kirigami.Units.gridUnit * 26
        maximumWidth: preferredWidth
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
        function onReportReady(title, text) {
            reportDialog.title = title;
            reportDialog.text = text;
            reportDialog.open();
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

    function openFatxDialog() {
        fatxDialog.open();
    }

    function openDriveDialog() {
        drivesDialog.open();
    }

    function confirmUnlock(row, name, device, isDrive) {
        unlockDialog.row = row;
        unlockDialog.placeName = name;
        unlockDialog.device = device;
        unlockDialog.isDrive = isDrive;
        unlockDialog.open();
    }

    function confirmRepair(row, name, device) {
        repairDialog.row = row;
        repairDialog.placeName = name;
        repairDialog.device = device;
        repairDialog.open();
    }

    function confirmFormat(row, name, device, isDrive) {
        formatDialog.row = row;
        formatDialog.placeName = name;
        formatDialog.device = device;
        formatDialog.isDrive = isDrive;
        formatConfirm.text = "";
        formatLabel.text = "XBOX";
        formatDialog.open();
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
