# Design Goals

> Snapshot of the project's Docs version, 2026-10-05.

## Purpose

Unnamed Gpl3 Qt App is one file-browser interface for Xbox 360 storage: FATX drives and images, STFS packages, files on a console over XBDM, and plain host folders. It holds the functionality that cannot live in Genexis because it depends on GPLv3 code, and it borrows Genexis's Kirigami look so the two read as one product family.

The users are Xbox 360 modders who today switch between separate tools for drives, packages, executables and the debug monitor.

## Goals and non-goals

The goal is a single, safe place to read and change anything an Xbox 360 stores or exposes, with the same actions everywhere.

**Goals**

- Browse every storage target in one interface: FATX drives and images, STFS packages, a console over XBDM, host folders.
- Read, write, extract, inject, replace and delete files with the same actions on every target; each action is enabled only when that target supports it.
- Inspect and work on single files: XEX information, decrypt and encrypt, compress and decompress, patch, sign.
- Reach a live console: file transfer over XBDM and a memory viewer and editor.
- Run on Linux, Windows and macOS, and on Android wherever raw device access is not needed.

**Non-goals for now**

- Mounting a filesystem into the operating system (FUSE, WinFsp, macFUSE). Browsing a drive inside the app matters more.
- NAND, xboxupd and bootloader support, until the gxbuild3 code is relicensed.
- Raw USB drive access on Android.
- Retail XEX signing. No retail private key exists, so retail output carries a cleared signature.

## Platform targets

The target is full desktop support on Linux, Windows and macOS, and a reduced but useful Android build.

| Feature | Linux | Windows | macOS | Android |
| --- | --- | --- | --- | --- |
| Browse, XEX tools, STFS | yes | yes | yes | yes |
| FATX on image files | yes | yes | yes | yes, through file descriptors |
| FATX on a physical drive | yes | yes, as administrator | yes, as root | no |
| XBDM and memory editor | yes | yes | yes | yes, over the network |

Today only Linux is exercised end to end. Windows is covered by a MinGW cross-build of the core run under Wine. macOS and Android have not been built, and drive access exists only on Linux.

## Architecture principles

The design keeps format code, storage access and user interface separate, so each can be tested and replaced alone.

1. **The core knows nothing about Qt.** `unnamed_core` is plain C++20 and builds on its own (`-DUNNAMED_BUILD_APP=OFF`), so it can be tested on every operating system.
2. **One interface for all storage.** Every backend implements `FileSystem` (list, stat, describe, openRead, openWrite, makeDirectory, rename, remove) and declares capability flags. The interface enables an action from the flags, never from the backend's name.
3. **File contents move as streams.** `copyTree()` copies a file or folder between any two filesystems. Extract, inject and copy need no per-pair code, and the same path works with Android file handles.
4. **The UI thread never touches a filesystem.** Each open filesystem has one worker thread. Listings, details and operations run as cancellable background jobs listed under Transfers.
5. **The expanded view is data, not UI code.** A backend returns property groups for a file or for itself, and one QML component renders any format.
6. **Optional backends are build features.** FATX, XEX and STFS are CMake options backed by git submodules, and the app builds without them.
7. **Library work stays small and in the forks.** FATX gained a `fatx_core` library; XexTool will get one in the same way.

```text
GUI (Qt and Kirigami)
  QML pages and components  -->  FileBrowser controller (mounts, selection, jobs, models)
                                          |  queues jobs
                                          v
One worker thread per open filesystem (listing, details and operations run as cancellable jobs)
                                          |  calls
                                          v
Core (plain C++, no Qt)
  FileSystem interface (list, describe, read, write, rename, remove; capability flags)
        ^                          ^
        |  copyTree() uses it      |  implement
  Streams and copyTree()    Backends, each implementing FileSystem:
                              Local folder | Demo | FATX (fatx_core) | XBDM (planned) | STFS (planned)
                                          |  reads and writes
                                          v
Storage: image files, physical drives, a console over the network
```

The controller queues work on a per-filesystem worker, which calls the FileSystem interface that every backend implements.

## Safety principles

The app edits drives that hold a console's only copy of someone's data, so the defaults protect it and every deviation is visible.

- **Writes follow the target.** Images open read-only and need an explicit unlock. Drives open read-write, and fall back to read-only with the reason when the system refuses a writable open. A read-write or read-only badge marks every place.
- **The GUI never runs as root.** On Linux a drive is opened through udisks2 and polkit. An elevated helper for Windows and `authopen` for macOS are planned.
- **Nothing half-written.** A new file appears only when its transfer finishes, and a cancelled transfer leaves nothing behind.
- **Replace keeps the file in place.** On FATX the new data overwrites the old clusters, so the directory-entry index and start cluster stay the same. It fails, changing nothing, if the new file does not fit the old allocation.
- **Destructive actions confirm.** Delete, repair and format ask first. Format needs the place's name typed and is offered only on a writable place.
- **Hostile input gives errors, not crashes.** Damaged and truncated images are fuzzed, and parsers are sanitizer-clean.
- **Live memory starts read-only.** The memory editor will need an explicit unlock before it writes to a console.

Known gap: the final in-place write of a Replace is not safe against power loss.

## Licensing

This app is GPLv3, which rules out linking GPLv2-only code, and that shapes which libraries it can use.

| Component | Licence | Decision |
| --- | --- | --- |
| This app | GPLv3 | The reason the app exists apart from Genexis. |
| FATX fork | GPLv3 only | Compatible. Changes live on the fork's `fatx-core` branch and are not sent upstream, since the project is re-pushed to GitHub from SourceForge. |
| XexTool | README says GPLv3, but the repo has no licence file | Confirm it, and the licences of its XeCrypt and libLZX submodules, before release. |
| gxbuild3 | GPLv2 | Cannot be linked into a GPLv3 program. Relicense only the STFS files, which needs co-maintainer erorn's agreement and a check of where each file came from. NAND, xboxupd and bootloaders wait. A clean-room STFS is the fallback. |
| Genexis | GPLv2 | Design reference. The status bar and navigation drawer are modelled on its QML, which is fine if you are the sole author of that code and needs checking if not. |

## UI and design language

The interface follows Genexis: Kirigami on Qt Quick Controls 2, so one set of screens serves a wide desktop window and a phone-width one.

- **Shell.** A custom status bar replaces the global toolbar and holds the menu button, page title, Transfers and Open. A modal drawer lists pages, with About in its footer. Pages switch one at a time.
- **Browser page.** A Places sidebar (a drawer when narrow), a clickable breadcrumb, a filter box, a sort menu, a sortable list view and an icon view, a context menu, and drag-and-drop to add files.
- **Expanded view.** A second page in Kirigami's page row. It sits beside the browser on wide windows and stacks with a back button on narrow ones. The Item tab describes the selected file or folder. The Filesystem tab describes the filesystem itself.
- **Capability-driven actions.** Extract, Replace, Rename, Delete, New Folder and Add Files appear enabled only when the filesystem supports them.
- **Background work is visible.** The Transfers popup shows progress and a cancel button for every operation.
- **Icons and style.** Breeze icons are initialised through KF6 IconThemes so icons appear on every platform. The KDE desktop style is used when installed. The default window is 1000 by 720, with a 380 by 480 minimum.

## Feature scope

FATX is built; XEX is next, then XBDM and STFS, and the rest waits on licensing.

| Area | Target behaviour | State |
| --- | --- | --- |
| FATX | Browse, extract, inject, replace in place, delete, new folder, rename; drives read-write, images read-only; format; filesystem check and repair | Built and tested on Linux with images and loop devices; not tried on a real drive |
| XEX | Info, extract, decrypt and encrypt, compress and decompress, patch, sign (devkit key signs; retail output clears the signature) | Next, through XexTool as a library |
| XBDM | Connect, file transfer, new folder, delete, memory viewer and editor | Not started; a mock server will allow testing without a console |
| STFS | Create, inject, extract, delete | Not started; waits on the relicense or a clean-room version |
| NAND, xboxupd, bootloaders | Same operations | Deferred until gxbuild3 is relicensed |
| Mount into the OS | Expose a FATX volume to other programs | Deferred |

## Quality bar

A feature counts as working only when it has been run, and anything not run is listed as unverified.

- **Round trips.** Each backend is tested through its public interface: write, read back, then check with an independent tool (FATX images are checked with `fsck.fatx`).
- **Portable core.** The core builds without Qt, with warnings on, under the address and undefined-behaviour sanitizers, and is cross-built for Windows and run under Wine.
- **Hostile input.** Damaged and truncated images are fuzzed; the result must be an error, never a crash.
- **Real UI.** Interface changes are checked by driving the actual QML headless and reading screenshots.
- **Honest limits.** Real hardware, MSVC, macOS, Android and GitHub CI are recorded as unverified until someone has run them.

## Open questions

Only push access to the XexTool fork blocks the next milestone; the XexTool licence must be settled before release.

- [ ] gxbuild3: relicense the STFS files, or write STFS clean-room? Needs erorn's agreement either way for the first option.
- [ ] Is XexTool GPLv3? Its repo has no licence file.
- [ ] Replace stages the whole new file in the host temp folder (up to 4 GiB). Keep that, or hold small files in memory?
- [ ] After a shrinking Replace the file's room to grow shrinks too. Keep the full original allocation instead?
- [ ] Check the FAT timestamp seconds against a console-written image. Deferred.
- [ ] Recovery (undelete) and health checking. Deferred; Check Filesystem and Format are still in the UI.
- [ ] Android: image files and network only, or a USB host path later?
