import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

QQC2.ToolBar {
    id: root

    property string pageTitle: qsTr("Unnamed Gpl3 Qt App")

    property bool canGoBack: false

    signal menuRequested
    signal backRequested
    signal openFolderRequested
    signal openDemoRequested

    position: QQC2.ToolBar.Header

    contentItem: RowLayout {
        spacing: 0

        QQC2.ToolButton {
            icon.name: "application-menu"
            display: QQC2.AbstractButton.IconOnly
            onClicked: root.menuRequested()

            QQC2.ToolTip.text: qsTr("Navigation menu")
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
        }

        QQC2.ToolButton {
            visible: root.canGoBack
            icon.name: "go-previous"
            display: QQC2.AbstractButton.IconOnly
            onClicked: root.backRequested()

            QQC2.ToolTip.text: qsTr("Close details")
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
        }

        Kirigami.Separator {
            Layout.fillHeight: true
            Layout.topMargin: Kirigami.Units.smallSpacing
            Layout.bottomMargin: Kirigami.Units.smallSpacing
        }

        QQC2.Label {
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.fillWidth: true
            text: root.pageTitle
            font.bold: true
            elide: Text.ElideRight
        }

        QQC2.ToolButton {
            id: openButton
            text: qsTr("Open")
            icon.name: "folder-open"
            display: QQC2.AbstractButton.TextBesideIcon
            Layout.rightMargin: Kirigami.Units.largeSpacing
            onClicked: openMenu.popup(openButton, 0, openButton.height)

            QQC2.Menu {
                id: openMenu

                QQC2.MenuItem {
                    text: qsTr("Open Folder…")
                    icon.name: "folder-open"
                    onTriggered: root.openFolderRequested()
                }
                QQC2.MenuItem {
                    text: qsTr("Open Demo (sample data)")
                    icon.name: "applications-development"
                    onTriggered: root.openDemoRequested()
                }
            }
        }
    }
}
