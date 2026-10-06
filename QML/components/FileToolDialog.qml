pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
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
    // Names of the output files in effect that exist already.
    readonly property var replacedFiles: {
        const out = [];
        if (operation) {
            for (const p of operation.parameters) {
                const v = values[p.id];
                if (p.kind === "outputFile" && activeIds.indexOf(p.id) >= 0 && v && FileBrowser.pathExists(v))
                    out.push(FileBrowser.resolvePath(v).split("/").pop());
            }
        }
        return out;
    }

    function openFor(name, availableOperations, index) {
        fileName = name;
        operations = availableOperations;
        operationIndex = index || 0;
        resetValues();
        open();
    }

    // Results get their suggested name, in the folder FileBrowser.outputFolder() names.
    function resetValues() {
        const v = {};
        if (operation) {
            for (const p of operation.parameters) {
                const suggested = (p.kind === "outputFile" || p.kind === "outputFolder") && p.defaultValue === "";
                v[p.id] = suggested ? p.suggestedName : p.defaultValue;
            }
        }
        values = v;
    }

    function setValue(id, value) {
        const v = Object.assign({}, values);
        v[id] = value;
        values = v;
    }

    // Replacing a file, the source or an existing output, is confirmed first.
    function run() {
        if (!canRun)
            return;
        if (changesFile || replacedFiles.length > 0)
            confirmDialog.open();
        else
            start();
    }

    function start() {
        FileBrowser.runFileOperation(fileName, operation.handler, operation.id, values);
        close();
    }

    title: qsTr("File Tools")
    padding: Kirigami.Units.largeSpacing
    preferredWidth: Kirigami.Units.gridUnit * 28
    // The same width for every tool; long help wraps.
    maximumWidth: preferredWidth
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
            textFormat: Text.PlainText
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: root.changesFile && root.operation.available && FileBrowser.canReplace
            type: Kirigami.MessageType.Warning
            text: qsTr("“%1” is changed in place. Keep a copy if it matters.").arg(root.fileName)
        }
        Kirigami.InlineMessage {
            objectName: "toolReplaces"
            Layout.fillWidth: true
            visible: root.replacedFiles.length > 0 && !root.changesFile && root.canRun
            type: Kirigami.MessageType.Warning
            text: qsTr("“%1” exists and will be replaced.").arg(root.replacedFiles.join("”, “"))
        }
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: root.operation !== null && !root.operation.available
            type: Kirigami.MessageType.Error
            text: root.operation ? root.operation.unavailableReason : ""
        }
        // Why the tool cannot run yet, above the fields so a long form does
        // not hide it.
        Kirigami.InlineMessage {
            objectName: "toolProblem"
            Layout.fillWidth: true
            visible: root.problem !== "" && root.operation !== null && root.operation.available
            type: Kirigami.MessageType.Information
            text: root.problem
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
                readonly property bool isFile: param.kind === "inputFile" || param.kind === "outputFile"
                                               || param.kind === "outputFolder"
                readonly property string resolved: isFile && value ? FileBrowser.resolvePath(String(value)) : ""
                readonly property string resolvedFolder: resolved.substring(0, Math.max(1, resolved.lastIndexOf("/")))

                objectName: "field_" + param.id
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
                    objectName: "choice_" + field.param.id
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
                    objectName: "switch_" + field.param.id
                    Layout.fillWidth: true
                    visible: field.param.kind === "boolean"
                    text: field.param.label
                    checked: field.value === true
                    onToggled: root.setValue(field.param.id, checked)
                }

                QQC2.SpinBox {
                    objectName: "spin_" + field.param.id
                    Layout.minimumWidth: Kirigami.Units.gridUnit * 7
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
                        objectName: "text_" + field.param.id
                        Layout.fillWidth: true
                        text: field.value === undefined ? "" : String(field.value)
                        inputMethodHints: field.param.kind === "integer" ? Qt.ImhFormattedNumbersOnly : Qt.ImhNone
                        placeholderText: field.param.suggestedName
                        onTextEdited: root.setValue(field.param.id, text)
                    }
                    QQC2.Button {
                        objectName: "choose_" + field.param.id
                        visible: field.isFile
                        icon.name: field.param.kind === "outputFolder" ? "folder-open"
                                 : field.param.kind === "outputFile" ? "document-save-as" : "document-open"
                        text: qsTr("Choose…")
                        onClicked: root.choose(field.param, field.value)
                    }
                }

                // Where a name without a folder goes.
                QQC2.Label {
                    objectName: "where_" + field.param.id
                    Layout.fillWidth: true
                    visible: field.isFile && field.resolved !== "" && field.resolved !== String(field.value).trim()
                    text: qsTr("In %1").arg(field.resolvedFolder)
                    elide: Text.ElideMiddle
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.disabledTextColor
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

    }

    Kirigami.PromptDialog {
        id: confirmDialog
        objectName: "toolConfirm"
        parent: QQC2.Overlay.overlay
        title: root.operation ? root.operation.name : ""
        preferredWidth: Kirigami.Units.gridUnit * 24
        subtitle: root.changesFile
                  ? qsTr("Replace “%1” with the result? Keep a copy if it matters.").arg(root.fileName)
                  : qsTr("Replace “%1” with the result?").arg(root.replacedFiles.join("”, “"))
        dialogType: Kirigami.PromptDialog.Warning
        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
        onAccepted: root.start()
    }

    // --- pickers ---------------------------------------------------------------

    // Starts where the field points, or in FileBrowser.outputFolder().
    function choose(param, current) {
        const path = current ? FileBrowser.resolvePath(String(current)) : "";
        const folder = FileBrowser.outputFolder();
        const filters = param.nameFilters.length > 0 ? param.nameFilters : [qsTr("All files (*)")];
        if (param.kind === "outputFolder") {
            folderPicker.target = param.id;
            folderPicker.currentFolder = path !== "" && FileBrowser.pathExists(path) ? FileBrowser.fileUrl(path) : folder;
            folderPicker.open();
        } else if (param.kind === "outputFile") {
            const file = path !== "" ? path : FileBrowser.resolvePath(param.suggestedName);
            savePicker.target = param.id;
            savePicker.nameFilters = filters;
            savePicker.currentFolder = FileBrowser.fileUrl(file.substring(0, file.lastIndexOf("/")) || "/");
            savePicker.selectedFile = FileBrowser.fileUrl(file);
            savePicker.open();
        } else {
            openPicker.target = param.id;
            openPicker.nameFilters = filters;
            openPicker.currentFolder = folder;
            if (path !== "" && FileBrowser.pathExists(path))
                openPicker.selectedFile = FileBrowser.fileUrl(path);
            openPicker.open();
        }
    }

    // One dialog per mode: Qt's own file dialog keeps the last picked name in
    // its file name field, so a shared one offered an opened patch's name for
    // saving.
    FileDialog {
        id: openPicker
        objectName: "toolOpenPicker"
        property string target: ""
        title: qsTr("Choose File")
        fileMode: FileDialog.OpenFile
        onAccepted: root.setValue(target, FileBrowser.localPath(selectedFile))
    }

    FileDialog {
        id: savePicker
        objectName: "toolSavePicker"
        property string target: ""
        title: qsTr("Save As")
        fileMode: FileDialog.SaveFile
        onAccepted: root.setValue(target, FileBrowser.localPath(selectedFile))
    }

    FolderDialog {
        id: folderPicker
        objectName: "toolFolderPicker"
        property string target: ""
        title: qsTr("Choose Folder")
        onAccepted: root.setValue(target, FileBrowser.localPath(selectedFolder))
    }
}
