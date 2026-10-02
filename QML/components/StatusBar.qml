import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

QQC2.ToolBar {
    id: root

    property string pageTitle: qsTr("Unnamed Gpl3 Qt App")

    signal menuRequested
    signal openFolderRequested

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
            text: qsTr("Open Folder")
            icon.name: "folder-open"
            display: QQC2.AbstractButton.TextBesideIcon
            Layout.rightMargin: Kirigami.Units.largeSpacing
            onClicked: root.openFolderRequested()
        }
    }
}
