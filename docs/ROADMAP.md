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
helper on Windows, `authopen` after unmounting on macOS. Drives open read-write
when possible (read-only with the reason otherwise), images read-only until
unlocked; format needs type-to-confirm.

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
- [x] Write support: inject (a new file goes to a temporary file renamed to
  its name in `finish()`; a dropped transfer deletes it), replace **in place**
  (see the decisions below), new folder, rename, delete, clear, repair (fsck
  with default answers), format (mkfs). FATX name rules (ASCII, 42
  characters, allowed characters, no names that differ only in case) are
  checked with clear messages. Images open read-only; *Enable Writing…*
  reopens one writable after a warning. *Format…* needs the place's name
  typed and is only offered on a writable place; *Repair Filesystem* and
  *Delete* ask first. Round trips are checked with the library's fsck and
  `fsck.fatx`; damaged images are fuzzed (fork: 300 rounds per test run,
  6000 more run once with other seeds, 150 rounds of in-place replacements;
  app: 60 rounds), ASan/UBSan clean
- [x] Drives on Linux: *Open Drive…* lists whole disks from sysfs (model,
  size, mounted/in use, read-only; loop devices on request) and flags Xbox
  360 drives by probing their FATX layout when they are readable without a
  prompt. A drive opens **read-write** (exclusive open); when that is refused
  (mounted or in use, read-only device, permission or polkit refusal) it
  opens read-only and a dialog gives the reason. Each FATX partition becomes
  a place with a read-write/read-only badge; *Make Read-only* / *Enable
  Writing* switch a drive place without a prompt. Privileges: `core::DriveAccess`
  methods tried in order: the user's own rights, then udisks2 `OpenDevice`
  over D-Bus (sd-bus, `-DUNNAMED_WITH_UDISKS2`, on when libsystemd is found):
  polkit decides and udisks passes a file descriptor, so the GUI never runs
  as root. Verified here with loop devices backed by images: listing,
  read/write as root, a non-root user refused by polkit, then allowed by a
  polkit rule; read-write by default, and the read-only fallback for a
  read-only loop device and for one held open exclusively by another
  process. **Not verified**: a real Xbox 360 drive, the interactive
  polkit password prompt (no authentication agent here), removable media
  hot-plug
- [ ] Drives on Windows (elevated helper, `\\.\PhysicalDriveN`, sector-aligned
  I/O) and macOS (`authopen` after unmounting): to implement behind
  `core::DriveAccess`/`BlockDevice`; `listDrives()` returns nothing there yet
- [x] Portability check: the core with FATX cross-builds with MinGW and its
  tests (library-only FATX tests included) pass under Wine. **Not
  verified**: MSVC (the fork uses POSIX `mode_t`/`S_IRUSR`), macOS, Android
  (an fd-backed `BlockDevice` exists, no SAF glue yet), real Windows
- [ ] Verified against a real drive on all desktops (M1 "done when")

#### Deferred (decided, not worked on now)

- FAT timestamp format: the fork stores seconds without halving them (FAT
  stores seconds/2); check against console-written dates and fix.
- Recovery (undelete, `unrm.fatx`) in the UI.
- Health-check polish (the *Check Filesystem* / *Repair* / *Format* UI stays
  as it is meanwhile).
- Drives on Windows and macOS (see above).

#### M1 decisions and notes

- **Fork changes** (branch `fatx-core`, 5 commits on v1.19): build fix,
  `fatx_core` + API, a set of bug fixes found by the new tests (reads at the
  last byte of an area overran the buffer; growing within a cluster failed;
  shrinking leaked clusters; growing from an unaligned size misplaced data;
  allocating part of a larger gap and freeing before the first gap corrupted
  the free-space map, which could cross-link files; moving to the root
  renamed in place; names with `{`/`}` threw), and MinGW support. Each is
  covered by a test in `tests/test_core.cpp`. **Not sent upstream**: the
  fork is maintained by re-pushing to GitHub from SourceForge.
- **Drives open read-write by default, images read-only** (images are often
  the backup of a drive; they keep the *Enable Writing…* unlock).
- **Replace is in place**: replacing a FATX file keeps its directory entry
  (same index in its directory, name, attributes, creation date) and its
  start cluster and chain positions; the new data is written over the
  existing clusters. The new size must fit the clusters the file has,
  ceil(old size / cluster size); a bigger file is refused with the sizes,
  changing nothing (the details view shows "Replacement can be up to"). A
  smaller file keeps the start of its chain and frees the tail; an empty one
  frees all its clusters (an empty FATX file has none) but keeps its entry.
  Nothing on the device changes before `finish()`: the data is staged in a
  temporary host file (as big as the file), then written over the old file in
  one pass. That pass is not power-loss atomic. New files still go through a
  temporary FATX file renamed into place. Fork API: `volume::replace()`,
  `replace_capacity()`, `clusters()`, `entry_info::entry_offset`.
- **Format** is `FileSystem::format()` (capability `Format`): the backend
  closes its volume, runs mkfs on its partition and reopens it.
- **Clear** empties a folder (keeps the volume label file).
- FATX times are local time as stored, shown in UTC by the details view;
  mkfs.fatx writes serial 0. Seconds: see Deferred.
- The app builds `fatx_core` in C++20 (the fork's own build uses C++26 and
  GCC 14's warnings); FATX needs CMake >= 3.25 and the Boost headers.

### M2 status (XEX)

- [x] Format-handler layer (core, no Qt): a `FormatHandler` recognises a file
  from a probe (name, size, first 4 KiB), may add property groups to its
  expanded view and offers operations described as data
  (`OperationDescriptor.hpp`: choice, boolean, integer with bounds, text,
  input file, output file, output folder; defaults, help, picker filters,
  suggested output names, conditions on an earlier parameter, a check
  across parameters). Values are type-checked and validated in the core;
  `runOperation()` reads the source as a stream from any `FileSystem` and
  writes outputs through `OperationIo` (host paths today; files in an output
  folder must have plain names, which excludes the names Windows reads as
  devices, and are never overwritten; a missing output folder is created;
  an output file is never the source itself, through links or another path
  to it). Host files are staged in a uniquely named, exclusively created
  `.<name>.<random>.part` and moved into place on success, without
  replacing a file that appeared meanwhile when overwriting is off;
  replacing a file keeps its mode and writes through a symlink, and a
  read-only file or one with other hard links is refused. Choices need
  unique option ids, and conditions only values their parameter can hold.
  Operations that rewrite the source need `Capability::Replace`; one may
  do so only on request
  (`modifiesSourceWhen`, e.g. a *Write to: a new file / in place* choice),
  and then stays available on read-only places with in place refused.
  `FileSystemOperationIo` reads inputs and writes outputs inside any
  `FileSystem` (tested with a second local place; the GUI still offers host
  paths only). Handlers register in
  `FormatHandlerRegistry::withBuiltins()` behind their build feature
- [x] Built-in *Any file* handler: Checksum (CRC-32, SHA-1, optionally saved
  in BSD format) and Hex Dump (`hexdump -C` layout), so the whole path is
  used and tested without XexTool
- [x] GUI: *File Tools…* (context menu, details page) appears when a handler
  has tools for the selected file; one dialog renders any operation,
  validates through the core and runs it as a cancellable job in
  Transfers. Results are filled in with their suggested names; a name
  without a folder means the current folder when the place is a host
  folder, otherwise Documents, and the dialog says which. Replacing a
  file, in place or as an existing output, asks first; why a tool cannot
  run yet shows at the top of the form. The pickers start there. The
  listing and details are read again after a tool writes. *Open XEX…*
  (Open menu, Places) opens the file's folder as a place with the file
  selected and described.
  Checked headless with screenshots (checksum, hex dump, conditional
  fields, typing into the fields, cancel from Transfers, narrow window,
  the replace confirmations, an output named like the source), with Qt's
  own file and folder dialogs, which an offscreen run gets.
  **Not verified**: the platform's native pickers (KDE, Windows, macOS);
  Qt's own save dialog leaves the file name empty instead of showing the
  suggested one
- [x] XEX handler (`XexHandler`, `-DUNNAMED_WITH_XEX=ON`) on the XexTool
  fork's `xextool_core` (branch `xex-core`, pinned at `4d85a98`, only
  `XexApi.h` included). Recognises XEX2 (and XEX1, which XexTool cannot
  read: no tools) by magic; a delta patch (`.xexp`) gets Info and Export
  Info only.
  Expanded view: executable, execution ID, security (machine, encryption,
  compression, regions, media, keys, sections), ratings, libraries,
  resources. Tools (15): Info (the `-l` report or the summary, optionally
  saved), Extract Basefile (`-b`), IDC Script (`-i`), Resources (`-d`),
  Decrypt and Encrypt (`-e`), Decompress (`-c u`, Basic, the default; or
  `-c b`, Uncompressed, every byte stored) and Compress (`-c c`, LZX), which
  refuse a run that would not change how the file is stored (Compress on an
  LZX file; Decompress when the result would be the source byte for byte)
  and write nothing then,
  Sign (`-m`: Devkit with the devkit key; Retail writes the cleared, all-zero
  signature, since no retail key exists), Apply Patch (`-p`), Remove Limits
  (`-r`), Add Bounding Path (`-a`), Fix Updated Executable (`-u`), Export and
  Import Info XML (`-z g`, `-z s`). Every xex edit writes a new file by
  default or the source in place on request. Files are read whole into
  memory (refused above 512 MiB; the expanded view stops at 64 MiB of file
  or of claimed image); a file whose header claims an image over 64 MiB and
  more than 256 times its size is refused as damaged. XexTool calls cannot
  be interrupted, so cancelling acts between read, process and write, and
  quitting waits for a running call. Resource names that are unsafe as
  file names are refused; a bounding path must be printable ASCII of up to
  255 characters. `tests/xex_tests.cpp` runs each tool over the fork's
  golden samples and compares SHA-256 and report text with the CLI's golden
  record (`extern/XexTool/tests/golden/expected.txt`), and checks the
  refusals; passes in Debug and under ASan/UBSan. One known difference from
  XexTool before the fork's library split (e965cd1), in the fork's
  command line too: a patch whose headers drop restriction entries (such
  as the bounding device id) gives a xex without them, where e965cd1 kept
  the unpatched xex's (`XexApi.h`, covered by the fork's `api_test`).
  Driven headless through the real QML on the golden samples: details,
  Info, Decrypt, Encrypt, Decompress, Compress, Sign (devkit and retail),
  Apply Patch, Remove Limits, Add Bounding Path and Resources write files
  with the golden record's hashes; in place on a host folder and on a
  writable FATX image (the result matches the command line and
  `fsck.fatx` is clean); in place is refused on a read-only image and when
  FATX cannot fit a larger file; damaged files give named errors. Two
  independent reviews of M2 found 12 distinct problems; all were confirmed
  and fixed with regression tests (the patch-header difference by
  documenting and testing it), except that quitting still waits for a
  running XexTool call. After the fixes, 224 crafted files and 4,000
  fuzzed inputs through the handler under ASan/UBSan gave no report.
  First try with real files (copies of four homebrew executables, retail,
  LZX compressed and encrypted): the details show every group, Info's
  report reaches the dialog whole, and Decompress then Compress give the
  command line's bytes. Two reports from it: Compress / Decompress defaulted
  to LZX, so on an LZX file it silently wrote the same storage again; it is
  now two tools, as above. Info "showed only the first lines": not
  reproduced (offscreen, and on a virtual KWin Wayland session with the
  user's fonts and scale), the report scrolls in its dialog; the dialog now
  opens each report at its top (it kept the previous report's scroll
  position), and its text can be selected and copied.
  **Not exposed**: special patches (`-s`, a per-title bit mask),
  the `-x` XML facts, `pack` (its input is an ELF), several edits in one run
  (run them one after another), extracting a subset of resources.
  **Known issue**: in place on a Windows host folder fails ("Permission
  denied"): the source is still open when the result is renamed over it,
  which Windows refuses (found under Wine with the MinGW build; not fixed).
  **Not verified**: real console titles and consoles running the output;
  real Windows, where the no-replace move (`MoveFileExW`) and the
  device-name checks matter. The GUI writes new files to host paths only

## Risks

- **Raw-device safety.** FATX writes and format can destroy a console drive:
  images read-only by default with an explicit unlock, drives read-write by
  default (user decision) with a visible badge and one-click read-only,
  confirmations for delete/repair, type-to-confirm for format; still to do:
  offer to save an image backup before the first write.
- **Untrusted input.** STFS/XEX parsers read hostile files: bounds checks, sanitizer
  CI, fuzzing.
- **Live memory writes** can crash a console: the memory editor starts read-only.
- **Fork changes.** Library targets for FATX and XexTool need patches in the
  ExposureMG forks (FATX: a FUSE-free target and a replacement for its 189
  `console::write` calls; XexTool: everything except `main.cpp` as a library).
