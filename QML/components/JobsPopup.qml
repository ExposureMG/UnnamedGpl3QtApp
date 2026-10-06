import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.exposuremg.unnamed

// Running and finished background operations, with progress and cancel.
QQC2.Popup {
    id: root

    // Kept inside a narrow window.
    width: Math.min(Kirigami.Units.gridUnit * 26, parent ? parent.Window.width - 2 * margins : Infinity)
    margins: Kirigami.Units.smallSpacing
    height: Math.min(contentItem.implicitHeight + topPadding + bottomPadding, Kirigami.Units.gridUnit * 22)
    padding: Kirigami.Units.largeSpacing
    closePolicy: QQC2.Popup.CloseOnEscape | QQC2.Popup.CloseOnPressOutsideParent

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: false

            Kirigami.Heading {
                Layout.fillWidth: true
                level: 4
                text: qsTr("Transfers")
            }
            QQC2.ToolButton {
                text: qsTr("Clear finished")
                icon.name: "edit-clear-history"
                enabled: FileBrowser.jobs.count > FileBrowser.jobs.activeCount
                onClicked: FileBrowser.jobs.clearFinished()
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: Math.min(contentHeight, Kirigami.Units.gridUnit * 16)
            clip: true
            spacing: Kirigami.Units.largeSpacing
            model: FileBrowser.jobs

            QQC2.ScrollBar.vertical: QQC2.ScrollBar {}

            delegate: ColumnLayout {
                id: job

                required property int index
                required property string title
                required property int state
                required property real progress
                required property string message
                required property bool active

                width: ListView.view.width
                spacing: Kirigami.Units.smallSpacing / 2

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: false

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: job.title
                        font.bold: true
                        elide: Text.ElideMiddle
                    }
                    QQC2.ToolButton {
                        objectName: "cancelJob_" + job.index
                        visible: job.active
                        icon.name: "process-stop"
                        display: QQC2.AbstractButton.IconOnly
                        text: qsTr("Cancel")
                        onClicked: FileBrowser.jobs.cancel(job.index)
                        QQC2.ToolTip.text: text
                        QQC2.ToolTip.visible: hovered
                        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                    }
                }

                QQC2.ProgressBar {
                    Layout.fillWidth: true
                    visible: job.active
                    from: 0
                    to: 1
                    value: Math.max(0, job.progress)
                    indeterminate: job.progress < 0
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    visible: text !== ""
                    text: job.state === 1 ? qsTr("Done") : job.message
                    elide: Text.ElideMiddle
                    color: job.state === 2 ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.disabledTextColor
                }
            }

            Kirigami.PlaceholderMessage {
                anchors.centerIn: parent
                width: parent.width - Kirigami.Units.largeSpacing * 2
                visible: list.count === 0
                text: qsTr("No transfers")
            }
        }
    }
}
