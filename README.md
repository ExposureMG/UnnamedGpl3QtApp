# Unnamed Gpl3 Qt App

Stuff I wanted to put in [Genexis](https://github.com/ExposureMG/Genexis) but I can't because it's GPL V3


# Building

Requires CMake >= 3.21, a C++20 compiler, Qt >= 6.5 (Widgets, Quick, Quick
Controls 2, Quick Dialogs) and KDE Frameworks 6 Kirigami and IconThemes (the
latter initialises the Breeze icon theme so icons work on every platform, as in
Genexis). KF6 QQC2 Desktop Style is optional: it is used when installed,
otherwise Qt's default style is used.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build                    # core unit tests
./build/UnnamedGpl3QtApp
```

The GUI-free core and its tests need only a compiler and CMake:

```sh
cmake -S . -B build -DUNNAMED_BUILD_APP=OFF
```

Optional backends are CMake features backed by git submodules (all off by
default until their milestone lands, see [docs/ROADMAP.md](docs/ROADMAP.md)):
`-DUNNAMED_WITH_FATX=ON`, `_XEX`, `_STFS` (run `git submodule update --init
extern/<name>` first).

FATX (`-DUNNAMED_WITH_FATX=ON`) needs CMake >= 3.25 and the Boost headers
(e.g. `libboost-dev`); no FUSE. With Boost.Program_options installed
(`libboost-program-options-dev`) the FATX command-line tool is built too and
the FATX tests use its `mkfs.fatx`/`fsck.fatx` modes to make and check images;
without it those tests are skipped.

# Project layout

```
include/core, src/core   GUI-free library (unnamed_core, std C++ only)
  FileSystem               abstract filesystem: list, stat, describe (expanded view),
                           openRead/openWrite streams, mkdir, rename, remove, Capability flags
  Stream, Transfer         byte streams and copyTree(): extract/inject/copy between any two filesystems
  LocalFileSystem          host directory backend
  DemoFileSystem           read-only *sample data* filesystem (XEX, STFS, ... views)
  FileSystemRegistry       kind -> factory; new backends (FATX, STFS, NAND, ...) register here
  BlockDevice              random-access storage (image file now, drives later) under a backend
  FatxFileSystem           Xbox 360 FATX (optional, wraps fatx_core from extern/FATX)
  Drives                   drive listing and opening (Linux: sysfs, direct or udisks2)
include/gui, src/gui     C++ glue exposed to QML (talks to core::FileSystem only)
  FileBrowser              singleton: mounts, path, selection, details, operations
                           (all filesystem work runs in cancellable background jobs)
  JobModel                 background operations shown in the Transfers popup
  FileSystemModel          current folder as a list model (sort + filter)
  MountModel               open filesystems ("places")
QML/                     Kirigami UI (QML module org.exposuremg.unnamed)
  Main.qml                 window: status bar, nav drawer, shared actions/dialogs, details toggle
  pages/Browser.qml        places sidebar + breadcrumb + toolbar + list/icon view
  pages/Details.qml        expanded view page (Item / Filesystem tabs)
  pages/About.qml          about page
  components/              StatusBar, PlacesSidebar, PathBar, FileListView, FileGridView,
                           DetailsView, ItemActions, JobsPopup, KindIcon
src/Main.cpp             application entry point (loads the QML module)
tests/                   unit tests for core (run via ctest)
extern/                  git submodules (XexTool, gxbuild3, Genexis, FATX)
```

# Xbox 360 FATX

With `-DUNNAMED_WITH_FATX=ON`:

- *Open > Open FATX Image…* opens a partition image, a memory unit image or a
  whole Xbox 360 disk image; every FATX partition becomes a place.
- *Open > Open Drive…* (Linux) lists the drives, marks Xbox 360 ones, and
  opens one the same way. If you cannot read the drive (`/dev/sdX` is
  usually root-only), the app asks udisks2, and polkit asks for your
  password; the app itself never runs as root (`-DUNNAMED_WITH_UDISKS2`,
  needs libsystemd).
- Places open **read-only**. The place's menu has *Enable Writing…* (and
  *Make Read-only*), *Check Filesystem* (fsck dry run), *Repair Filesystem*
  and *Format…* (type the place's name to confirm; only on a writable place).
- Writing follows the FATX rules: ASCII names of at most 42 characters from
  letters, digits, spaces and ``! # $ % & ' ( ) - . @ [ ] ^ _ ` { } ~``, no two
  names that differ only in case, files up to 4 GiB.

# File browser UI

- **Places** sidebar lists the open filesystems (a drawer on narrow windows).
- **Breadcrumb**, filter field, sort menu, list (sortable columns) and icon view.
- **Expanded view** (Alt+Return or the info button): a second page in Kirigami's
  page row. Side by side with the browser on wide windows, stacked with a back
  button on narrow ones (phones/tablets). The *Item* tab describes the selected
  file or folder (XEX, STFS, INI, ... each get their own property groups); the
  *Filesystem* tab describes the filesystem object itself (type, capacity,
  health, supported operations).
- Extract / Replace / Rename / Delete / New Folder / Add Files are enabled from the
  filesystem's capabilities (context menu, details page, toolbar, drag and drop).
- Everything runs in the background; the status bar's *Transfers* button shows
  progress and lets you cancel.
- *Open > Open Demo* loads sample data so the expanded views can be explored
  before the real format backends exist.

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

