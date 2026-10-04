# Roadmap

Goals: a cross-platform (Linux, Windows, macOS, possibly Android) Qt/Kirigami
tool for Xbox 360 filesystems, files and consoles. This file records the plan,
the decisions behind it and what is done. It is updated as milestones land.

## Requirements

| Area | Requirements |
|---|---|
| **FATX** | Read, write, extract, inject, delete, **browse a physical drive**, format. Mount is *not* a priority. |
| **XBDM** | Connect, inject/extract, create folder, delete, memory viewer/editor. |
| **Containers** | STFS: create, inject, extract, delete. NAND, xboxupd: later (see licensing). |
| **XEX** | Extract, decrypt/encrypt, compress/decompress, patch, sign. |
| **Bootloaders** | Same operations; later (lives in gxbuild3's `nand/`). |

## Decisions

- **Licensing.** This app is GPLv3. FATX is GPLv3-only and fits. gxbuild3 is GPLv2
  and cannot be linked into a GPLv3 program as it stands. The plan: relicense only
  the gxbuild3 files we need (STFS) once their authors agree (the STFS code is by
  co-maintainer *erorn*, and some files such as `SHA1.hpp` may be third-party, so
  check each file's origin), and **drop NAND, xboxupd and bootloader support until
  gxbuild3 is relicensed**. A clean-room STFS implementation is the fallback.
  XexTool's repo has no license file (the README says GPLv3); confirm that, and
  the licenses of its `XeCrypt` and `libLZX` submodules, before shipping.
- **Mount is deprioritised.** Browsing a drive inside the app matters more than
  exposing it to the OS, so the FATX work needs no FUSE.
- **Signing.** XexTool signs with the devkit key (`-m d`). It has no retail or
  freeboot private key: for retail output it writes an all-zero signature, which
  is expected to work only on consoles whose hypervisor skips the check (to be
  verified on hardware). The UI offers *Devkit (signed)* or *Retail / patched
  console (signature cleared)*. Note XexTool's retail sign routine returns
  `false` by design; wrappers must not treat that as an error.
- **Android.** Image files and network (XBDM) only. No raw drives (Android has no
  raw block access; USB OTG would be a separate, large project).
- **Android file access** goes through the `FileSystem` interface (a future SAF
  backend), never through host paths.

## Architecture

```
GUI (QML/Kirigami)  ->  FileBrowser (async jobs, one worker thread per mount)
                              |
                         core::FileSystem  <- Local, Demo, FATX, XBDM, STFS, ...
                              |
              streams (ByteSource/ByteSink) + copyTree()   <- extract = fs -> host,
                                                              inject  = host -> fs
```

- Backends only implement `list`, `stat`, `describe*`, `openRead`, `openWrite`,
  `makeDirectory`, `rename`, `remove` (+ clear/health check/repair) and advertise
  them via capability flags; the UI enables actions from those flags.
- Copying between *any* two filesystems is one function (`copyTree`), so
  FATX ↔ console ↔ host needs no per-pair code and works with streams on Android.
- Core calls are serialised per filesystem and run off the UI thread; listings,
  details and operations are cancellable background jobs shown in *Transfers*.
- Optional backends are CMake features (`UNNAMED_WITH_FATX`, `_XEX`, `_STFS`,
  `_XBDM`) backed by git submodules, so the GUI builds without them.

## Platform support

| Feature | Linux | Windows | macOS | Android |
|---|---|---|---|---|
| Browse, XEX tools, STFS | yes | yes | yes | yes |
| FATX on image files | yes | yes | yes | yes (file descriptors) |
| FATX on a physical drive | yes (udisks2) | planned (admin helper) | planned (authopen) | no |
| XBDM and memory editor | yes | yes | yes | yes (network) |

Drive access never runs the GUI as root: udisks2/pkexec on Linux, an elevated
helper on Windows, `authopen` after unmounting on macOS. Drives open read-only;
writing needs an explicit unlock and format needs type-to-confirm.

## Milestones

| # | Scope | Done when |
|---|---|---|
| **M0** | Foundations (below) | Core builds and tests pass on 3 desktops; async UI works |
| **M1** | **FATX**: FUSE-free `fatx_core` + log hook; device picker (each partition becomes a place); read-only, then write/inject/delete/mkdir; format, fsck, unrm | Round trips against `mkfs.fatx` images, then a real drive on all desktops |
| **M2** | **XEX**: real info view; extract, encrypt/decrypt, compress/decompress, patch, sign | Output matches the XexTool CLI byte for byte |
| **M3** | **XBDM**: protocol client with a mock server; file backend; hex memory viewer/editor | Test suite passes without a console; then verified on hardware |
| **M4** | **STFS** (after relicense or clean-room) | Create, inject, extract, delete round trip |
| **M5** | Packaging for Linux, Windows, macOS, Android; app CI on all platforms | Signed release builds |
| Later | NAND, xboxupd, bootloaders (need gxbuild3 relicensed); mount; Android USB drives | |

### M0 status

- [x] Streams instead of host paths (`ByteSource`/`ByteSink`, atomic file sink)
- [x] `Result<T>`, cancellable `Status`
- [x] `copyTree` with progress and cancel; extract/inject/replace use it
- [x] `makeDirectory` and `rename` in core and UI (New Folder, Rename)
- [x] Async: per-mount worker, async listing/details, cancellable job queue, Transfers popup
- [x] Core builds without Qt (`-DUNNAMED_BUILD_APP=OFF`); ASan/UBSan clean on Linux; cross-built with MinGW and tests pass under Wine
- [x] Feature flags (`cmake/Features.cmake`)
- [x] CI workflow written: core on Linux/Windows/macOS, sanitizers, app on Linux (`.github/workflows/ci.yml`). **Not yet run on GitHub**: the MSVC and Apple builds and the Arch app job are unverified
- [ ] App CI on Windows/macOS/Android (needs a KDE Frameworks toolchain; part of M5)
- [ ] Android build spike

### M1 status (FATX)

- [x] FATX fork: `fatx_core` library target without FUSE or Boost.Program_options,
  log sink instead of stdout, per-thread contexts (several volumes at once),
  pluggable I/O (`fatx::io_backend`) and an embedding API (`volume.hpp`).
  Fork branch `fatx-core` of ExposureMG/FATX, pinned by `extern/FATX`
- [x] Read-only FATX backend (`FatxFileSystem`, registry kind `FATX`): browse,
  stat, details (entry attributes, clusters, label, serial, geometry,
  capacity), extract, health check (fsck dry run). Partition images and Xbox 360
  disk images (each FATX partition becomes a place). GUI: *Open FATX Image…*
  and *Check Filesystem*. Tested against images made by `mkfs.fatx`
- [x] Write support: inject/replace (through a temporary file renamed over
  the target in `finish()`; a dropped transfer deletes it), new folder,
  rename, delete, clear, repair (fsck with default answers), format (mkfs).
  FATX name rules (ASCII, 42 characters, allowed characters, no names that
  differ only in case) are checked with clear messages. Places open
  read-only; *Enable Writing…* reopens one writable after a warning; *Format…*
  needs the place's name typed and is only offered on a writable place.
  Round trips are checked with the library's fsck and `fsck.fatx`; damaged
  images are fuzzed (fork: 300 rounds per test run, 6000 more run once with
  other seeds; app: 60 rounds), ASan/UBSan clean.
  Not atomic on power loss: replacing deletes the old file, then renames the
  new one (two directory writes)
- [x] Drives on Linux: *Open Drive…* lists whole disks from sysfs (model,
  size, mounted/in use, read-only; loop devices on request) and flags Xbox
  360 drives by probing their FATX layout when they are readable without a
  prompt. A drive opens read-only; each FATX partition becomes a place;
  *Enable Writing…* (stronger warning for drives) reopens it writable with
  an exclusive open (refused while mounted). Privileges: `core::DriveAccess`
  methods tried in order: the user's own rights, then udisks2 `OpenDevice`
  over D-Bus (sd-bus, `-DUNNAMED_WITH_UDISKS2`, on when libsystemd is found):
  polkit decides and udisks passes a file descriptor, so the GUI never runs
  as root. Verified here with a loop device backed by an image: listing,
  read/write as root, and a non-root user refused by polkit, then allowed by
  a polkit rule. **Not verified**: a real Xbox 360 drive, the interactive
  polkit password prompt (no authentication agent here), removable media
  hot-plug
- [ ] Drives on Windows (elevated helper, `\\.\PhysicalDriveN`, sector-aligned
  I/O) and macOS (`authopen` after unmounting): to implement behind
  `core::DriveAccess`/`BlockDevice`; `listDrives()` returns nothing there yet
- [x] Portability check: the core with FATX cross-builds with MinGW and its
  tests (library-only FATX tests included) pass under Wine. **Not
  verified**: MSVC (the fork uses POSIX `mode_t`/`S_IRUSR`), macOS, Android
  (an fd-backed `BlockDevice` exists, no SAF glue yet), real Windows
- [ ] unrm (undelete) in the UI: the library keeps `unrm.fatx`; not exposed yet
- [ ] Verified against a real drive on all desktops (M1 "done when")

#### M1 decisions and notes

- **Fork changes** (branch `fatx-core`, 4 commits on v1.19): build fix,
  `fatx_core` + API, a set of bug fixes found by the new tests (reads at the
  last byte of an area overran the buffer; growing within a cluster failed;
  shrinking leaked clusters; growing from an unaligned size misplaced data;
  allocating part of a larger gap and freeing before the first gap corrupted
  the free-space map, which could cross-link files; moving to the root
  renamed in place; names with `{`/`}` threw), and MinGW support. Each is
  covered by a test in `tests/test_core.cpp`. They should go upstream.
- **Images open read-only too**, with the same *Enable Writing…* unlock as
  drives, because images are often the backup of a drive.
- **Format** is `FileSystem::format()` (capability `Format`): the backend
  closes its volume, runs mkfs on its partition and reopens it.
- **Clear** empties a folder (keeps the volume label file).
- FATX times are local time as stored, shown in UTC by the details view;
  mkfs.fatx writes serial 0. The fork stores seconds without halving them
  (FAT stores seconds/2); not checked against console-written dates.
- The app builds `fatx_core` in C++20 (the fork's own build uses C++26 and
  GCC 14's warnings); FATX needs CMake >= 3.25 and the Boost headers.

## Risks

- **Raw-device safety.** FATX writes and format can destroy a console drive:
  read-only by default, explicit write unlock, type-to-confirm (all done in
  M1); still to do: offer to save an image backup before the first write.
- **Untrusted input.** STFS/XEX parsers read hostile files: bounds checks, sanitizer
  CI, fuzzing.
- **Live memory writes** can crash a console: the memory editor starts read-only.
- **Fork changes.** Library targets for FATX and XexTool need patches in the
  ExposureMG forks (FATX: a FUSE-free target and a replacement for its 189
  `console::write` calls; XexTool: everything except `main.cpp` as a library).
