import org.kde.kirigami as Kirigami

// Icon for a file/folder/filesystem "kind" (see core::Entry::kind).
Kirigami.Icon {
    property string kind: "file"

    readonly property var names: ({
            "folder": "folder",
            "xex": "application-x-executable",
            "stfs": "package-x-generic",
            "ini": "configure",
            "smc": "cpu",
            "updatebin": "system-software-update",
            "filesystem": "drive-harddisk",
            "file": "text-x-generic"
        })

    source: names[kind] || "text-x-generic"
    fallback: "text-x-generic"
}
