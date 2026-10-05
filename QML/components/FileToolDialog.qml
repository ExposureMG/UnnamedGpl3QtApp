pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import QtCore
import org.kde.kirigami as Kirigami
import org.exposuremg.unnamed

// Runs a file tool (an operation of a format handler) on one file. The form is
// built from the operation's parameter descriptors (FileBrowser.fileOperations),
// so a new tool needs no QML; FileBrowser validates the values.
Kirigami.Dialog {
    id: root
    objectName: "fileToolDialog"

    property string fileName: ""
    // A copy of FileBrowser.fileOperations taken when the dialog opens.
    property var operations: []
    property int operationIndex: 0
    readonly property var operation: operations.length > 0 ? operations[Math.min(operationIndex, operations.length - 1)] : null
    // Parameter id -> value, replaced as a whole on every edit so bindings update.
    property var values: ({})
    readonly property var activeIds: operation ? FileBrowser.activeParameters(operation.handler, operation.id, values) : []
    readonly property string problem: operation ? FileBrowser.validateOperation(operation.handler, operation.id, values) : ""
    readonly property bool canRun: operation !== null && operation.available && problem === ""
    // The values choose to rewrite the file in place.
    readonly property bool changesFile: operation !== null && operation.modifiesSource
                                        && FileBrowser.rewritesSource(operation.handler, operation.id, values)

    function openFor(name, availableOperations, index) {
        fileName = name;
        operations = availableOperations;
        operationIndex = index || 0;
        resetValues();
        open();
    }

    function resetValues() {
        const v = {};
        if (operation) {
            for (const p of operation.parameters)
                v[p.id] = p.defaultValue;
        }
        values = v;
    }

    function setValue(id, value) {
        const v = Object.assign({}, values);
        v[id] = value;
        values = v;
    }

    function run() {
        if (!canRun)
            return;
        FileBrowser.runFileOperation(fileName, operation.handler, operation.id, values);
        close();
    }

    title: qsTr("File Tools")
    padding: Kirigami.Units.largeSpacing
    preferredWidth: Kirigami.Units.gridUnit * 28
    standardButtons: Kirigami.Dialog.Cancel
    customFooterActions: [
        Kirigami.Action {
            text: root.operation ? root.operation.name : qsTr("Run")
            icon.name: root.changesFile ? "document-save" : "system-run"
            enabled: root.canRun
            onTriggered: root.run()
        }
    ]

    onOperationChanged: resetValues()

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: false
            spacing: Kirigami.Units.largeSpacing

            QQC2.Label {
                text: qsTr("Tool")
                visible: root.operations.length > 1
            }
            QQC2.ComboBox {
                objectName: "toolChooser"
                Layout.fillWidth: true
                visible: root.operations.length > 1
                model: root.operations
                textRole: "name"
                currentIndex: root.operationIndex
                onActivated: index => root.operationIndex = index
            }
        }

        QQC2.Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: root.operation ? root.operation.description : ""
            visible: text !== ""
        }
        QQC2.Label {
            Layout.fillWidth: true
            elide: Text.ElideMiddle
            color: Kirigami.Theme.disabledTextColor
            text: root.operation ? qsTr("%1 · %2").arg(root.fileName).arg(root.operation.handlerName) : root.fileName
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: root.changesFile && root.operation.available
            type: Kirigami.MessageType.Warning
            text: qsTr("“%1” is changed in place. Keep a copy if it matters.").arg(root.fileName)
        }
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: root.operation !== null && !root.operation.available
            type: Kirigami.MessageType.Error
            text: root.operation ? root.operation.unavailableReason : ""
        }

        Repeater {
            model: root.operation ? root.operation.parameters : []

            delegate: ColumnLayout {
                id: field

                required property var modelData
                readonly property var param: modelData
                readonly property var value: root.values[param.id]
                // QQC2.SpinBox holds 32-bit values; wider ranges get a text field.
                readonly property bool spinnable: param.minimum >= -2147483648 && param.maximum <= 2147483647

                Layout.fillWidth: true
                Layout.fillHeight: false
                visible: root.activeIds.indexOf(param.id) >= 0
                spacing: Kirigami.Units.smallSpacing

                QQC2.Label {
                    Layout.fillWidth: true
                    visible: field.param.kind !== "boolean"
                    text: field.param.label
                    wrapMode: Text.Wrap
                }

                QQC2.ComboBox {
                    Layout.fillWidth: true
                    visible: field.param.kind === "choice"
                    model: field.param.kind === "choice" ? field.param.options : []
                    textRole: "label"
                    valueRole: "id"
                    currentIndex: {
                        const options = field.param.options || [];
                        for (let i = 0; i < options.length; ++i) {
                            if (options[i].id === field.value)
                                return i;
                        }
                        return -1;
                    }
                    onActivated: root.setValue(field.param.id, currentValue)
                }

                QQC2.Switch {
                    Layout.fillWidth: true
                    visible: field.param.kind === "boolean"
                    text: field.param.label
                    checked: field.value === true
                    onToggled: root.setValue(field.param.id, checked)
                }

                QQC2.SpinBox {
                    visible: field.param.kind === "integer" && field.spinnable
                    editable: true
                    from: field.spinnable ? field.param.minimum : 0
                    to: field.spinnable ? field.param.maximum : 0
                    value: field.param.kind === "integer" && field.spinnable ? Number(field.value) : 0
                    onValueModified: root.setValue(field.param.id, value)
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: false
                    visible: field.param.kind === "text" || (field.param.kind === "integer" && !field.spinnable)
                             || field.param.kind === "inputFile" || field.param.kind === "outputFile"
                             || field.param.kind === "outputFolder"
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.TextField {
                        Layout.fillWidth: true
                        text: field.value === undefined ? "" : String(field.value)
                        inputMethodHints: field.param.kind === "integer" ? Qt.ImhFormattedNumbersOnly : Qt.ImhNone
                        placeholderText: field.param.kind === "outputFile" ? field.param.suggestedName : ""
                        onTextEdited: root.setValue(field.param.id, text)
                    }
                    QQC2.Button {
                        visible: field.param.kind === "inputFile" || field.param.kind === "outputFile"
                                 || field.param.kind === "outputFolder"
                        icon.name: field.param.kind === "outputFolder" ? "folder-open" : "document-open"
                        text: qsTr("Choose…")
                        onClicked: root.choose(field.param, field.value)
                    }
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    visible: field.param.help !== ""
                    text: field.param.help
                    wrapMode: Text.Wrap
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.disabledTextColor
                }
            }
        }

        QQC2.Label {
            objectName: "toolProblem"
            Layout.fillWidth: true
            visible: root.problem !== ""
            text: root.problem
            wrapMode: Text.Wrap
            color: Kirigami.Theme.disabledTextColor
        }
    }

    // --- pickers ---------------------------------------------------------------

    readonly property url documentsFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)

    function choose(param, current) {
        const chosen = current ? FileBrowser.fileUrl(current) : "";
        if (param.kind === "outputFolder") {
            folderPicker.target = param.id;
            if (current)
                folderPicker.currentFolder = chosen;
            folderPicker.open();
            return;
        }
        filePicker.target = param.id;
        filePicker.saving = param.kind === "outputFile";
        filePicker.nameFilters = param.nameFilters.length > 0 ? param.nameFilters : [qsTr("All files (*)")];
        if (filePicker.saving)
            filePicker.selectedFile = current ? chosen : documentsFolder + "/" + param.suggestedName;
        filePicker.open();
    }

    FileDialog {
        id: filePicker
        property string target: ""
        property bool saving: false
        title: saving ? qsTr("Save As") : qsTr("Choose File")
        fileMode: saving ? FileDialog.SaveFile : FileDialog.OpenFile
        onAccepted: root.setValue(target, FileBrowser.localPath(selectedFile))
    }

    FolderDialog {
        id: folderPicker
        property string target: ""
        title: qsTr("Choose Folder")
        onAccepted: root.setValue(target, FileBrowser.localPath(selectedFolder))
    }
}
