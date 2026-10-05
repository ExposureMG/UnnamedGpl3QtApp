import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import QtCore
import org.kde.kirigami as Kirigami
import org.exposuremg.unnamed
import "../components"

Kirigami.Page {
    id: root

    title: qsTr("Browser")
    padding: 0

    Kirigami.ColumnView.fillWidth: true

    // The application window (Main.qml); provides shared actions and dialogs.
    required property var app
    readonly property var itemActions: app.itemActions
    // The sidebar is inline when there is room, otherwise it is a drawer.
    readonly property bool sidebarInline: width >= Kirigami.Units.gridUnit * 30
    readonly property bool gridMode: prefs.viewMode === "grid"

    Settings {
        id: prefs
        category: "Browser"
        property string viewMode: "list"
        property int sortKey: FileSystemModel.SortByName
        property bool sortAscending: true
    }

    Component.onCompleted: {
        FileBrowser.model.sortKey = prefs.sortKey;
        FileBrowser.model.sortAscending = prefs.sortAscending;
    }

    Connections {
        target: FileBrowser.model
        function onSortChanged() {
            prefs.sortKey = FileBrowser.model.sortKey;
            prefs.sortAscending = FileBrowser.model.sortAscending;
        }
    }

    onSidebarInlineChanged: if (sidebarInline)
        sidebarDrawer.close()

    Shortcut {
        sequence: "Alt+Up"
        onActivated: FileBrowser.goUp()
    }
    Shortcut {
        sequence: StandardKey.Find
        onActivated: searchField.forceActiveFocus()
    }
    Shortcut {
        sequence: "Alt+Return"
        onActivated: root.app.toggleDetails()
    }

    // --- layout ---------------------------------------------------------------

    RowLayout {
        anchors.fill: parent
        spacing: 0

        PlacesSidebar {
            visible: root.sidebarInline
            Layout.fillWidth: false
            Layout.fillHeight: true
            Layout.preferredWidth: Kirigami.Units.gridUnit * 13
            onPropertiesRequested: root.app.showDetails(true)
            onOpenFolderRequested: root.app.openFolderDialog()
            onOpenFatxRequested: root.app.openFatxDialog()
        }

        Kirigami.Separator {
            visible: root.sidebarInline
            Layout.fillHeight: true
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Row 1: sidebar toggle, up, breadcrumb.
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: false
                Layout.margins: Kirigami.Units.smallSpacing
                spacing: Kirigami.Units.smallSpacing

                QQC2.ToolButton {
                    visible: !root.sidebarInline
                    icon.name: "sidebar-expand-left"
                    text: qsTr("Places")
                    display: QQC2.AbstractButton.IconOnly
                    onClicked: sidebarDrawer.open()
                    QQC2.ToolTip.text: text
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                }
                QQC2.ToolButton {
                    icon.name: "go-up"
                    text: qsTr("Up")
                    display: QQC2.AbstractButton.IconOnly
                    enabled: FileBrowser.canGoUp
                    onClicked: FileBrowser.goUp()
                    QQC2.ToolTip.text: text + " (Alt+Up)"
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                }
                PathBar {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Kirigami.Units.gridUnit * 2
                }
            }

            // Row 2: search, sort, view mode, add, details.
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: false
                Layout.leftMargin: Kirigami.Units.smallSpacing
                Layout.rightMargin: Kirigami.Units.smallSpacing
                Layout.bottomMargin: Kirigami.Units.smallSpacing
                spacing: Kirigami.Units.smallSpacing

                Kirigami.SearchField {
                    id: searchField
                    Layout.fillWidth: true
                    Layout.maximumWidth: Kirigami.Units.gridUnit * 20
                    enabled: FileBrowser.isOpen
                    placeholderText: qsTr("Filter this folder…")
                    onTextChanged: FileBrowser.model.filterText = text
                    Keys.onEscapePressed: {
                        text = "";
                        root.forceActiveFocus();
                    }
                }

                Item {
                    Layout.fillWidth: true
                }

                QQC2.ToolButton {
                    id: sortButton
                    icon.name: "view-sort"
                    text: qsTr("Sort")
                    display: QQC2.AbstractButton.IconOnly
                    enabled: FileBrowser.isOpen
                    onClicked: sortMenu.popup(sortButton, 0, sortButton.height)
                    QQC2.ToolTip.text: text
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

                    QQC2.Menu {
                        id: sortMenu

                        component SortItem: QQC2.MenuItem {
                            required property int key
                            font.bold: FileBrowser.model.sortKey === key
                            onTriggered: FileBrowser.model.sortKey = key
                        }

                        SortItem {
                            text: qsTr("Name")
                            key: FileSystemModel.SortByName
                        }
                        SortItem {
                            text: qsTr("Type")
                            key: FileSystemModel.SortByKind
                        }
                        SortItem {
                            text: qsTr("Size")
                            key: FileSystemModel.SortBySize
                        }
                        SortItem {
                            text: qsTr("Modified")
                            key: FileSystemModel.SortByModified
                        }
                        QQC2.MenuSeparator {}
                        QQC2.MenuItem {
                            text: FileBrowser.model.sortAscending ? qsTr("Ascending → Descending") : qsTr("Descending → Ascending")
                            onTriggered: FileBrowser.model.sortAscending = !FileBrowser.model.sortAscending
                        }
                    }
                }

                QQC2.ToolButton {
                    icon.name: "view-list-details"
                    text: qsTr("List view")
                    display: QQC2.AbstractButton.IconOnly
                    checkable: true
                    checked: !root.gridMode
                    autoExclusive: true
                    onClicked: prefs.viewMode = "list"
                    QQC2.ToolTip.text: text
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                }
                QQC2.ToolButton {
                    icon.name: "view-list-icons"
                    text: qsTr("Icon view")
                    display: QQC2.AbstractButton.IconOnly
                    checkable: true
                    checked: root.gridMode
                    autoExclusive: true
                    onClicked: prefs.viewMode = "grid"
                    QQC2.ToolTip.text: text
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                }

                Kirigami.Separator {
                    Layout.fillHeight: true
                    Layout.topMargin: Kirigami.Units.smallSpacing
                    Layout.bottomMargin: Kirigami.Units.smallSpacing
                }

                QQC2.ToolButton {
                    visible: FileBrowser.canMakeDirectory
                    action: root.itemActions.newFolder
                    display: QQC2.AbstractButton.IconOnly
                    QQC2.ToolTip.text: text
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                }
                QQC2.ToolButton {
                    visible: FileBrowser.canInject
                    action: root.itemActions.inject
                    display: QQC2.AbstractButton.IconOnly
                    QQC2.ToolTip.text: text
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                }
                QQC2.ToolButton {
                    icon.name: "documentinfo"
                    text: qsTr("Details")
                    display: QQC2.AbstractButton.IconOnly
                    enabled: FileBrowser.isOpen
                    checkable: true
                    checked: root.app.detailsOpen
                    onClicked: root.app.toggleDetails()
                    QQC2.ToolTip.text: text + " (Alt+Return)"
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                }
            }

            Kirigami.Separator {
                Layout.fillWidth: true
            }

            QQC2.ProgressBar {
                Layout.fillWidth: true
                Layout.fillHeight: false
                Layout.preferredHeight: visible ? implicitHeight : 0
                visible: FileBrowser.loading
                indeterminate: true
            }

            // Content: list / grid / empty states.
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                FileListView {
                    id: listView
                    anchors.fill: parent
                    visible: FileBrowser.isOpen && !root.gridMode
                    onContextRequested: (row, item, position) => itemMenu.openFor(item, position)
                    onDeletePressed: root.itemActions.remove.trigger()
                }

                FileGridView {
                    id: gridView
                    anchors.fill: parent
                    visible: FileBrowser.isOpen && root.gridMode
                    onContextRequested: (row, item, position) => itemMenu.openFor(item, position)
                    onDeletePressed: root.itemActions.remove.trigger()
                }

                Kirigami.PlaceholderMessage {
                    anchors.centerIn: parent
                    width: parent.width - Kirigami.Units.gridUnit * 4
                    visible: !FileBrowser.isOpen
                    icon.name: "folder-open"
                    text: qsTr("Nothing open")
                    explanation: FileBrowser.fatxAvailable ? qsTr("Open a folder or an Xbox 360 FATX image, or try the demo filesystem to explore the interface.") : qsTr("Open a folder, or try the demo filesystem to explore the interface.")
                    helpfulAction: Kirigami.Action {
                        text: qsTr("Open Folder…")
                        icon.name: "folder-open"
                        onTriggered: root.app.openFolderDialog()
                    }
                }

                Kirigami.PlaceholderMessage {
                    anchors.centerIn: parent
                    width: parent.width - Kirigami.Units.gridUnit * 4
                    visible: FileBrowser.isOpen && !FileBrowser.loading && FileBrowser.model.count === 0
                    text: FileBrowser.model.totalCount === 0 ? qsTr("This folder is empty") : qsTr("No matches")
                    explanation: FileBrowser.model.totalCount === 0 ? "" : qsTr("Nothing here matches “%1”.").arg(FileBrowser.model.filterText)
                }

                // Drop host files here to add them (when the filesystem allows it).
                DropArea {
                    id: dropArea
                    anchors.fill: parent
                    enabled: FileBrowser.canInject
                    onDropped: drop => {
                        for (const url of drop.urls)
                            FileBrowser.injectFile(url);
                    }

                    Rectangle {
                        anchors.fill: parent
                        visible: dropArea.containsDrag
                        color: Qt.alpha(Kirigami.Theme.highlightColor, 0.15)
                        border.color: Kirigami.Theme.highlightColor
                        border.width: 2
                    }
                }
            }

            Kirigami.Separator {
                Layout.fillWidth: true
            }

            QQC2.Label {
                Layout.fillWidth: true
                Layout.fillHeight: false
                Layout.margins: Kirigami.Units.smallSpacing
                Layout.leftMargin: Kirigami.Units.largeSpacing
                text: FileBrowser.statusText
                color: Kirigami.Theme.disabledTextColor
                elide: Text.ElideRight
            }
        }
    }

    // Places drawer for narrow windows.
    Kirigami.OverlayDrawer {
        id: sidebarDrawer
        objectName: "sidebarDrawer"
        edge: Qt.LeftEdge
        modal: true
        handleVisible: false
        width: Kirigami.Units.gridUnit * 14

        contentItem: PlacesSidebar {
            onPlaceSelected: sidebarDrawer.close()
            onPropertiesRequested: {
                sidebarDrawer.close();
                root.app.showDetails(true);
            }
            onOpenFolderRequested: {
                sidebarDrawer.close();
                root.app.openFolderDialog();
            }
            onOpenFatxRequested: {
                sidebarDrawer.close();
                root.app.openFatxDialog();
            }
        }
    }

    // Context menu for the selected item.
    QQC2.Menu {
        id: itemMenu

        function openFor(item, position) {
            parent = item;
            popup(position.x, position.y);
        }

        QQC2.MenuItem {
            action: root.itemActions.open
            visible: FileBrowser.selectedIsDirectory
            height: visible ? implicitHeight : 0
        }
        QQC2.MenuItem {
            action: root.itemActions.properties
            onTriggered: root.app.showDetails(false)
        }
        QQC2.MenuSeparator {}
        QQC2.MenuItem {
            action: root.itemActions.extract
        }
        QQC2.MenuItem {
            action: root.itemActions.rename
        }
        QQC2.MenuItem {
            action: root.itemActions.replace
        }
        QQC2.MenuItem {
            action: root.itemActions.fileTools
            visible: root.itemActions.fileTools.enabled
            height: visible ? implicitHeight : 0
        }
        QQC2.MenuItem {
            action: root.itemActions.remove
        }
    }
}
