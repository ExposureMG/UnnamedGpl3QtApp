pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.exposuremg.unnamed

// Connects to a console over XBDM: by address, from the consoles that
// answered a search on the network, or from the ones reached before.
Kirigami.Dialog {
    id: root
    objectName: "consoleDialog"

    title: qsTr("Connect to Console")
    padding: 0
    preferredWidth: Kirigami.Units.gridUnit * 30
    standardButtons: Kirigami.Dialog.Cancel
    customFooterActions: [
        Kirigami.Action {
            objectName: "connectAction"
            text: qsTr("Connect")
            icon.name: "network-connect"
            enabled: addressField.text.trim() !== "" && !FileBrowser.connecting
            onTriggered: root.connectTo(addressField.text, portField.value)
        }
    ]

    function choose(console) {
        addressField.text = console.address;
        portField.value = console.port;
    }

    function connectTo(address, port) {
        FileBrowser.cancelDiscovery();
        FileBrowser.connectConsole(address, port);
    }

    onOpened: {
        FileBrowser.discoverConsoles();
        addressField.forceActiveFocus();
    }
    onClosed: {
        FileBrowser.cancelDiscovery();
        FileBrowser.cancelConnect();
    }

    Connections {
        target: FileBrowser
        function onConsoleConnected() {
            root.close();
        }
    }

    ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            visible: true
            type: Kirigami.MessageType.Information
            text: qsTr("Devkits, consoles with an XBDM plugin and emulators answer on port 730. Type the address when a console is not found: networks often block the search.")
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            columns: 2
            columnSpacing: Kirigami.Units.largeSpacing

            QQC2.Label {
                text: qsTr("Address")
            }
            QQC2.TextField {
                id: addressField
                objectName: "addressField"
                Layout.fillWidth: true
                placeholderText: qsTr("IP address, such as 192.168.1.20")
                enabled: !FileBrowser.connecting
                Keys.onReturnPressed: if (text.trim() !== "")
                    root.connectTo(text, portField.value)
            }
            QQC2.Label {
                text: qsTr("Port")
            }
            QQC2.SpinBox {
                id: portField
                objectName: "portField"
                from: 1
                to: 65535
                value: 730
                editable: true
                enabled: !FileBrowser.connecting
                textFromValue: (value, locale) => String(value)
                valueFromText: (text, locale) => parseInt(text, 10)
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            visible: FileBrowser.connecting

            QQC2.BusyIndicator {
                Layout.preferredHeight: Kirigami.Units.iconSizes.smallMedium
                Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
                running: FileBrowser.connecting
            }
            QQC2.Label {
                Layout.fillWidth: true
                text: qsTr("Connecting to %1…").arg(addressField.text.trim())
                elide: Text.ElideRight
            }
        }

        Kirigami.InlineMessage {
            objectName: "connectError"
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            visible: text !== ""
            type: Kirigami.MessageType.Error
            text: FileBrowser.connectError
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.smallSpacing

            Kirigami.Heading {
                Layout.fillWidth: true
                level: 5
                text: qsTr("Found on the network")
            }
            QQC2.BusyIndicator {
                Layout.preferredHeight: Kirigami.Units.iconSizes.smallMedium
                Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
                running: FileBrowser.discovering
                visible: running
            }
            QQC2.ToolButton {
                objectName: "searchButton"
                icon.name: FileBrowser.discovering ? "process-stop" : "view-refresh"
                text: FileBrowser.discovering ? qsTr("Stop") : qsTr("Search Again")
                onClicked: FileBrowser.discovering ? FileBrowser.cancelDiscovery() : FileBrowser.discoverConsoles()
            }
        }

        ListView {
            id: foundList
            objectName: "foundList"
            Layout.fillWidth: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 5
            clip: true
            model: FileBrowser.discoveredConsoles

            delegate: QQC2.ItemDelegate {
                id: found

                required property var modelData

                width: ListView.view.width
                highlighted: addressField.text === found.modelData.address && portField.value === found.modelData.port
                onClicked: root.choose(found.modelData)
                onDoubleClicked: root.connectTo(found.modelData.address, found.modelData.port)

                contentItem: Kirigami.IconTitleSubtitle {
                    icon.name: "network-server"
                    title: found.modelData.name
                    subtitle: found.modelData.address + ":" + found.modelData.port
                    selected: found.highlighted
                }
            }

            Kirigami.PlaceholderMessage {
                anchors.centerIn: parent
                width: parent.width - Kirigami.Units.largeSpacing * 4
                visible: foundList.count === 0
                text: FileBrowser.discovering ? qsTr("Searching…") : FileBrowser.discoveryError !== "" ? FileBrowser.discoveryError : qsTr("No console answered")
            }
        }

        Kirigami.Heading {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            level: 5
            text: qsTr("Saved")
        }

        ListView {
            id: savedList
            objectName: "savedList"
            Layout.fillWidth: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 5
            Layout.bottomMargin: Kirigami.Units.smallSpacing
            clip: true
            model: FileBrowser.savedConsoles

            delegate: QQC2.ItemDelegate {
                id: saved

                required property var modelData

                width: ListView.view.width
                highlighted: addressField.text === saved.modelData.address && portField.value === saved.modelData.port
                onClicked: root.choose(saved.modelData)
                onDoubleClicked: root.connectTo(saved.modelData.address, saved.modelData.port)

                contentItem: RowLayout {
                    Kirigami.IconTitleSubtitle {
                        Layout.fillWidth: true
                        icon.name: "network-server"
                        title: saved.modelData.name
                        subtitle: saved.modelData.address + ":" + saved.modelData.port
                        selected: saved.highlighted
                    }
                    QQC2.ToolButton {
                        icon.name: "list-remove"
                        text: qsTr("Forget")
                        display: QQC2.AbstractButton.IconOnly
                        onClicked: FileBrowser.forgetConsole(saved.modelData.address, saved.modelData.port)
                        QQC2.ToolTip.text: text
                        QQC2.ToolTip.visible: hovered
                        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                    }
                }
            }

            Kirigami.PlaceholderMessage {
                anchors.centerIn: parent
                width: parent.width - Kirigami.Units.largeSpacing * 4
                visible: savedList.count === 0
                text: qsTr("Consoles you connect to are listed here")
            }
        }
    }
}
