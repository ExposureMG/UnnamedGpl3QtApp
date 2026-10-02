# Unnamed Gpl3 Qt App

Stuff I wanted to put in [Genexis](https://github.com/ExposureMG/Genexis) but I can't because it's GPL V3


# Building

Requires CMake >= 3.21, a C++20 compiler, Qt >= 6.5 (Widgets, Quick, Quick
Controls 2, Quick Dialogs) and KDE Frameworks 6 Kirigami. KF6 QQC2 Desktop Style
is optional: it is used when installed, otherwise Qt's default style is used.

```sh
git submodule update --init --recursive   # optional for now, see extern/
cmake -S . -B build
cmake --build build
ctest --test-dir build                    # core unit tests
./build/UnnamedGpl3QtApp
```

# Project layout

```
include/core, src/core   GUI-free library (unnamed_core, std C++ only)
  FileSystem               abstract filesystem + Capability flags (browse, extract, inject, ...)
  LocalFileSystem          host directory backend
  FileSystemRegistry       kind -> factory; new backends (FATX, STFS, NAND, ...) register here
include/gui, src/gui     C++ glue exposed to QML (talks to core::FileSystem only)
  FileBrowser              QML singleton: open filesystem, path, navigation, errors
  FileSystemModel          list model over one directory of a FileSystem
QML/                     Kirigami UI (QML module org.exposuremg.unnamed)
  Main.qml                 ApplicationWindow: status bar header, navigation drawer, dialogs
  components/StatusBar.qml top toolbar (menu button, page title, Open Folder)
  pages/Browser.qml        folder card + file list
  pages/About.qml          about page
src/Main.cpp             application entry point (loads the QML module)
tests/                   unit tests for core (run via ctest)
extern/                  git submodules (XexTool, gxbuild3, Genexis, FATX)
```

# Design language

The UI follows [Genexis](https://github.com/ExposureMG/Genexis): Kirigami on Qt
Quick Controls 2 with the KDE desktop style, no global toolbar but a custom
`StatusBar` header (menu button, separator, bold page title, actions), a modal
navigation drawer with a footer item, single-page navigation via
`switchPage()`, `Kirigami.AbstractCard` summary cards, Kirigami `Units` for all
spacing, and one global error dialog. Keep new pages and components in that
style (see `QML/`).

Adding a filesystem: implement `core::FileSystem` (only `list()` is required,
override the other operations and report them via `capabilities()`), then
register a factory in `FileSystemRegistry::withBuiltins()`.

# Heavy WIP


Planned:

- Qt GUI File Browser
- Xbox 360 formats supported

- Xbdm Client?
- XRPC / JRPC / JRPC2 Client?


FS Formats:

- Xbox 360 Neighbourhood
- FATX (Integrated FATX)
- STFS (Via libgxbuild)
- NAND (Via libgxbuild)
- xboxupd.bin (Via libgxbuild)

File Formats:

- XEX (Integrated XexTool)
- Bootloader? (Via libgxbuild)

Data Formats

- SMC Config
- DashLaunch ini
- RGLoader ini


FS Functions:

- Inject
- Extract
- Replace
- Delete
- Clear
- Health Check
- Attempt Repair


Health Check:

- STFS, XEX, Bootloader: Sha1 and RSA?
- NAND: ECD

Repair:

- STFS, XEX, Bootloader: N/A
- NAND: ECC


File Functions:

- Decrypt key [+ CPU key]
- Decompress
- Compress
- Encrypt: key [+ CPU key]
- Derive key: cpu and 1bl key
- Patch: offset and bin

File Info:

- Payload Sha1 (No Header)
- Crypt Status
- Compression Status

