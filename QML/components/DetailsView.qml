import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.exposuremg.unnamed

// Expanded view of a file/folder or of the filesystem object itself. Renders
// the {title, subtitle, kind, notice, groups} map provided by FileBrowser, so
// every format (XEX, STFS, ...) shows up here without UI changes.
ColumnLayout {
    id: root

    property var details: ({})
    property bool showActions: false
    property var actions: null

    readonly property bool hasDetails: details !== undefined && details.title !== undefined && details.title !== ""

    spacing: Kirigami.Units.largeSpacing

    Kirigami.PlaceholderMessage {
        Layout.fillWidth: true
        Layout.topMargin: Kirigami.Units.gridUnit * 3
        visible: !root.hasDetails
        text: FileBrowser.isOpen ? qsTr("No details available") : qsTr("Nothing open")
        explanation: FileBrowser.isOpen ? qsTr("This filesystem cannot describe this item yet.") : qsTr("Open a folder to see its details.")
    }

    // Header: icon, name, kind.
    RowLayout {
        Layout.fillWidth: true
        Layout.fillHeight: false
        visible: root.hasDetails
        spacing: Kirigami.Units.largeSpacing

        KindIcon {
            kind: root.details.kind || "file"
            implicitWidth: Kirigami.Units.iconSizes.huge
            implicitHeight: Kirigami.Units.iconSizes.huge
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

            Kirigami.Heading {
                Layout.fillWidth: true
                level: 2
                text: root.details.title || ""
                wrapMode: Text.WrapAnywhere
            }
            QQC2.Label {
                Layout.fillWidth: true
                text: root.details.subtitle || ""
                color: Kirigami.Theme.disabledTextColor
                elide: Text.ElideRight
            }
        }
    }

    Kirigami.InlineMessage {
        Layout.fillWidth: true
        visible: root.hasDetails && (root.details.notice || "") !== ""
        type: Kirigami.MessageType.Information
        text: root.details.notice || ""
    }

    // Item actions (only for a selected item).
    Flow {
        Layout.fillWidth: true
        visible: root.hasDetails && root.showActions && root.actions !== null
        spacing: Kirigami.Units.smallSpacing

        QQC2.Button {
            action: root.actions ? root.actions.open : null
            visible: FileBrowser.selectedIsDirectory
        }
        QQC2.Button {
            action: root.actions ? root.actions.extract : null
        }
        QQC2.Button {
            action: root.actions ? root.actions.replace : null
            visible: !FileBrowser.selectedIsDirectory
        }
        QQC2.Button {
            action: root.actions ? root.actions.rename : null
        }
        QQC2.Button {
            action: root.actions ? root.actions.remove : null
        }
    }

    // Property groups.
    Repeater {
        model: root.hasDetails ? root.details.groups : []

        delegate: Kirigami.AbstractCard {
            id: card

            required property var modelData

            Layout.fillWidth: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Kirigami.Heading {
                    Layout.fillWidth: true
                    level: 4
                    text: card.modelData.title
                }
                Kirigami.Separator {
                    Layout.fillWidth: true
                }
                Repeater {
                    model: card.modelData.items

                    delegate: RowLayout {
                        id: row

                        required property var modelData

                        Layout.fillWidth: true
                        Layout.fillHeight: false
                        spacing: Kirigami.Units.largeSpacing

                        QQC2.Label {
                            Layout.preferredWidth: Kirigami.Units.gridUnit * 8
                            Layout.alignment: Qt.AlignTop
                            text: row.modelData.label
                            color: Kirigami.Theme.disabledTextColor
                            wrapMode: Text.Wrap
                        }
                        QQC2.Label {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignTop
                            text: row.modelData.value
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                }
            }
        }
    }

    Item {
        Layout.fillHeight: true
    }
}
