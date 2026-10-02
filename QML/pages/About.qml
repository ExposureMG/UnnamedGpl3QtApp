import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

Kirigami.Page {
    id: root
    title: qsTr("About")

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Kirigami.Units.largeSpacing
        spacing: Kirigami.Units.largeSpacing

        Kirigami.AbstractCard {
            Layout.fillWidth: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing / 2

                QQC2.Label {
                    text: qsTr("Unnamed Gpl3 Qt App")
                    font.bold: true
                    font.pointSize: Kirigami.Theme.defaultFont.pointSize + 1
                }

                QQC2.Label {
                    text: qsTr("Work in progress. Xbox 360 file and filesystem browser.")
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                    color: Kirigami.Theme.disabledTextColor
                }

                QQC2.Label {
                    text: qsTr("Licensed under the GNU GPL v3.")
                    color: Kirigami.Theme.disabledTextColor
                }
            }
        }

        Item {
            Layout.fillHeight: true
        }
    }
}
