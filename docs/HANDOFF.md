# Progress and Handoff

> Snapshot of the project's Docs version, 2026-10-07.

## Status

Two of six milestones are done: the app browses, extracts, writes and replaces files on FATX images and, on Linux, on drives. The XEX tools (M2) are built and reviewed on Linux. For XBDM (M3) the core has a console backend, tested only against a mock console; the GUI does not offer it yet. Nothing exists for STFS, and none of it has been tried on a real console, a console drive or real titles.

| Milestone | Scope | Status |
| --- | --- | --- |
| M0 | Foundations: streams, generic copy, async jobs, folder and rename operations, feature flags | Done |
| M1 | FATX: browse, write, in-place replace, format, check and repair, drives on Linux | Done on Linux; not tried on a real drive |
| M2 | XEX: info, extract, decrypt and encrypt, compress and decompress, patch, sign | Built on Linux; matches the command line; not tried with real titles |
| M3 | XBDM: file transfer, memory viewer and editor | Core file backend built on UpdClient, tested against its mock only; GUI and memory viewer not started; not tried on any console |
| M4 | STFS: create, inject, extract, delete | Blocked on relicensing or a clean-room version |
| M5 | Packaging and CI for Linux, Windows, macOS, Android | Not started |

## M3 status

M3 is on the app's `m3-xbdm` branch. The XBDM protocol client is UpdClient (`extern/UpdClient`, ExposureMG/UpdClient `main` at `ad83582`), a separate repository that is used here unchanged.

- `UNNAMED_WITH_XBDM`, ON by default, builds UpdClient's library into the core; without the submodule the build goes on without XBDM.
- `XbdmFileSystem` shows a console's drives as folders and supports list, details, extract, inject, replace, new folder, rename, delete and clear, with the console's own details (name, type, id, execution state, title address, free space). Names, 4 GiB sizes and full drives are refused before anything is sent. An abandoned upload is deleted, or listed as left over; the only copy after a failed replace is kept and named.
- One command connection per place, a second one on demand while a transfer runs, `reconnect()`, a Connected or Disconnected state, `cancel()` from any thread, `connectXbdm()`, `discoverConsoles()`, the registry kind `XBDM`, and `openClient()` for a future memory viewer. `docs/ROADMAP.md` (M3 status) has the details.
- `xbdm_tests` runs it against UpdClient's mock console over an in-memory pipe and loopback TCP, including cancels, drops, reconnects, connection limits and hostile names; 4 GiB - 1 each way on request.
- Verified only against that mock. Every real target is unverified: devkits running XDKBuild or RGLoader, a retail console running Glitch2 with an XBDM plugin, Xenia and Xenon. The checklist for them is `extern/UpdClient/docs/HARDWARE_TEST_PLAN.md`, which is missing from UpdClient `ad83582` and present in its history at `0f96753`.
- Not built: the GUI for it (connect, discovery, connection badge, reconnect, cancelling a blocked transfer from Transfers) and the memory viewer and editor.

## M2 status

M2 is built on the app's `m2-xex` branch, on the XexTool fork's `xex-core` branch, and has been reviewed once.

- File Tools (15 for a xex) run every XexTool operation except special patches, `-x` and `pack`, on a file in any place, and write files that match the command line's golden record byte for byte.
- After a first try with real files, Compress / Decompress, which defaulted to LZX and so silently recompressed an LZX file, is now two tools: Decompress (Basic, or Uncompressed with every byte stored) and Compress. Each refuses, writing nothing, a run that would not change how the file is stored.
- A report that Info showed only its first lines was not reproduced: the whole report reaches the dialog and scrolls there, offscreen and on a virtual KWin Wayland session with the user's fonts. The dialog now opens each report at its top, where it used to keep the previous report's scroll position, and its text can be selected and copied.
- A review by two independent agents found 12 problems, among them an output that could be the source itself, user files named `<output>.part` being destroyed, an empty patch reading a same-named file from the working directory, and an old, unhardened fork pinned by the submodule. All are fixed with regression tests, except that quitting still waits for a running XexTool call.
- One known difference from XexTool before the library split: a patch whose headers drop restriction entries gives a xex without them. It is documented in `XexApi.h` and tested.
- Not verified: commercial titles, consoles running the output, and real Windows. Info, Decompress and Compress were tried on copies of four homebrew executables. Under Wine, in-place edits on a host folder fail because the source is still open; that is not fixed.

## What is built

The app is about 7,400 lines of C++ and QML across a Qt-free core, a GUI layer, and three test programs.

**M0 foundations**

- File contents move as byte streams. A new file is written to a temporary file and moved into place only on success, so a cancelled copy leaves nothing behind.
- `copyTree()` copies a file or folder between any two filesystems, with progress and cancel. Extract, inject and replace all use it.
- New Folder and Rename work in the core and the UI.
- Each open filesystem has its own worker thread. Listing, details and every operation run as cancellable background jobs, shown in a Transfers popup.
- The core builds without Qt, and optional backends are CMake features backed by submodules.

**M1 FATX**

- The FATX fork has a `fatx_core` library with no FUSE and no Boost.Program\_options, a log hook, and pluggable I/O. Bugs found by the new tests are fixed in it, among them an overrun when reading a file's last byte, leaked clusters when shrinking, and free-space bugs that could cross-link files.
- The app browses, describes and extracts from FATX images and Xbox 360 disk images, where each partition becomes its own place.
- It writes: inject, delete, new folder, rename, clear. Replace overwrites the file in place, keeping its directory-entry index and start cluster, and fails without changes if the new file does not fit.
- Format, filesystem check and repair are in the UI with confirmations.
- On Linux an Open Drive dialog lists disks and flags likely Xbox 360 drives. A drive opens read-write and falls back to read-only with the reason. Images open read-only.

## Where the code lives

All work is committed and pushed. The app has 11 commits after your last one on `claude/wizardly-ritchie-ghhtsy`; `working` has those plus one commit that adds these two documents. The FATX fork has 5.

| Repository | Branch | Head | Note |
| --- | --- | --- | --- |
| ExposureMG/UnnamedGpl3QtApp | `claude/wizardly-ritchie-ghhtsy` | `a876990` | Main working branch |
| ExposureMG/UnnamedGpl3QtApp | `working` | `a876990` plus the documents commit | Same code, plus `docs/DESIGN_GOALS.md` and `docs/HANDOFF.md` |
| ExposureMG/FATX | `fatx-core` | `b6814d2` | `master` is untouched at upstream v1.19; the app's submodule points here |

Every commit is authored and committed as ExposureMG <exposuremg@protonmail.com>, with no co-author or session trailers.

**Build and run**

- Needs CMake 3.21 or newer, a C++20 compiler, Qt 6.5 or newer (Widgets, Quick, Quick Controls 2, Quick Dialogs) and KDE Frameworks 6 Kirigami and IconThemes.
- `cmake -S . -B build`, `cmake --build build`, `ctest --test-dir build`.
- Core and tests only, without Qt: add `-DUNNAMED_BUILD_APP=OFF`.
- FATX: `git submodule update --init extern/FATX`, then `-DUNNAMED_WITH_FATX=ON`.
- `docs/ROADMAP.md` in the repo holds the plan, decisions and per-milestone status.

## Verification

Everything below was run on Linux in a cloud container. Nothing was run on a real console drive.

| Area | How it was checked | Result |
| --- | --- | --- |
| App and tests | Clean build with FATX on and off, then `ctest` (core, drives and FATX test programs); app started headless | Pass; no QML errors |
| FATX fork | 624 tests, including the original FUSE shell tests, in Debug and under the address and undefined-behaviour sanitizers | Pass |
| In-place Replace | Directory-entry offset, start cluster and chain compared before and after; oversize replacement leaves the device byte-for-byte unchanged | Pass |
| Damaged images | Corrupted and truncated images fuzzed, 300 rounds per run plus 6,000 more once under the sanitizers | No crashes |
| Consistency | `fsck.fatx` run on images after writes, replaces and repairs | Clean |
| Drives | Loop devices backed by images: read-write open, read-only device, device held by another process | Correct fallback and reason shown |
| Windows | Core and FATX library cross-built with MinGW; tests run under Wine | Pass |
| UI | Real QML driven headless; screenshots read for browse, details, extract, transfers, drive picker, unlock and format dialogs | Looks and behaves as intended; the FATX and drive screens were checked by the agent that built them and not re-reviewed |

**Not verified**

- A real Xbox 360 drive, or any file or date written by a console.
- The interactive polkit password prompt, and the udisks2 path in the latest round (no system bus was running).
- MSVC, macOS, Android and real Windows.
- GitHub CI: the workflow is written but has never run.
- Power-loss behaviour of the final in-place write during Replace.
- Drive access on Windows and macOS, which is not implemented.

## Decisions so far

| Decision | Made by | Effect |
| --- | --- | --- |
| Drop NAND, xboxupd and bootloaders until gxbuild3 is relicensed; relicense only the needed STFS files in the meantime | You | M4 covers STFS only |
| Mounting into the OS is not a priority; browsing a drive inside the app is | You | No FUSE in the FATX path |
| XexTool signs with the devkit key, and retail output clears the signature | Established from the XexTool source | Sign offers Devkit or Retail / patched console |
| Do not send the FATX fork fixes upstream, because the project is re-pushed to GitHub from SourceForge | You | Fixes stay on the `fatx-core` branch |
| Drives open read-write; images open read-only | You | Drives fall back to read-only with the reason if the write is refused |
| Replace keeps the file's directory-entry index and position, and may require the new file to fit | You | Implemented as an in-place overwrite |
| Timestamp formatting, recovery and undelete, and health work can wait | You | Listed as deferred; Check Filesystem and Format stay in the UI |
| Android is image files and network only | Proposed by me, not yet confirmed | No raw USB drives on Android |
| Copy the app branch to a new `working` branch, then commit these documents there only | You | `working` is one commit ahead; `claude/wizardly-ritchie-ghhtsy` stays at `a876990`; which branch to use from now on is still open |

## Known issues and risks

The biggest risk is that the FATX write path has never touched a real console drive, so treat any real drive as untested.

- **Real hardware untested.** All FATX work ran on images and loop devices. Dates written by a console may show odd seconds, because the fork stores FAT time seconds without halving them.
- **Replace is not power-loss safe.** The data is staged first, but the final in-place overwrite is a single unprotected pass. Staging needs host temp space equal to the new file, up to 4 GiB.
- **Shrinking a file via Replace is one-way.** The freed tail clusters are gone, so a later replacement can only be as large as the shrunken file. Emptying a file frees all its clusters, including its start cluster.
- **XBDM has only met a mock.** The client, the mock and the backend follow a contract written from third-party clients. Error codes, sizes without `sizehi`, the connection limit and uploads near 4 GiB may differ on a console.
- **Licensing is unsettled.** gxbuild3 is GPLv2 and cannot be linked in. XexTool's repo has no licence file. The status bar and drawer are modelled on Genexis's QML, which is GPLv2.
- **Drive access exists only on Linux.** Windows needs an elevated helper and macOS needs `authopen`; neither is written. The fork also uses POSIX types, so it likely will not build with MSVC.
- **CI has never run.** The workflow is written, but the MSVC, Apple and Arch jobs are unverified.
- **Container-built toolchain.** Qt 6.8, Kirigami and the icon libraries were built from source in the session container, which is discarded when the session ends.

## Next steps

M2, the XEX tools, is built and reviewed; M3 has its core file backend.

1. **M2 XEX.** Built (see M2 status). Left: try real titles, fix in-place edits on Windows, and let quitting not wait for a running XexTool call.
2. **M3 XBDM.** The protocol client (UpdClient) and the file backend are built against the mock. Left: the GUI (connect, discovery, connection state, reconnect, cancel wired to the backend), the hex memory viewer and editor, which opens read-only, and the hardware checklist.
3. **M4 STFS.** Starts once the STFS files are relicensed or a clean-room version is chosen.
4. **M5 packaging.** Windows and macOS drive access, then installers for each platform and CI that builds the app on all of them.

**Only you can do**

- [ ] Try the FATX features on a real console drive, ideally a spare one or a copy.
- [ ] Run UpdClient's XBDM hardware checklist (`docs/HARDWARE_TEST_PLAN.md`; missing from `ad83582`, in UpdClient's history at `0f96753`) on the targets you have, then the app's XBDM backend once the GUI offers it.
- [ ] Choose the gxbuild3 route and ask erorn about the STFS files.
- [ ] Confirm the XexTool licence, and that of XeCrypt and libLZX.
- [ ] Give push access to `ExposureMG/XexTool` so M2 can begin.
- [ ] Open a pull request, or push to main, once so the CI workflow runs for the first time. It only triggers on those.

## Notes for the next person or session

The code is safe in git, but the working environment is not, so expect to rebuild it.

**Environment**

- Only Qt 6.4 and the old KF5 Kirigami were available from the package manager, so Qt 6.8, Kirigami 6.9 and the KF6 icon libraries were built from source into `/opt/kde` in the session container. That took over an hour on 4 cores, and it is lost when the container is reclaimed. A distribution that ships KF6 avoids it.
- Qt's own download hosts are blocked from the container; GitHub is reachable.
- To run the app headless, set `QT_QPA_PLATFORM=offscreen`, `QT_QUICK_BACKEND=software`, `QML_IMPORT_PATH`, `QT_PLUGIN_PATH`, `XDG_DATA_DIRS` and `LD_LIBRARY_PATH` to point at `/opt/kde`.
- UI changes were verified with a throwaway harness: temporarily replace `src/Main.cpp` with a version that drives the `FileBrowser` singleton and saves `grabWindow()` screenshots, then restore it. It is not in the repo, and `src/Main.cpp` must be clean before every commit.

**Working agreements**

- Commit as `ExposureMG <exposuremg@protonmail.com>`, with no `Co-Authored-By` or session trailers. History was rewritten once to remove them.
- App work goes to `claude/wizardly-ritchie-ghhtsy`; FATX fork work goes to `fatx-core`. Never touch the fork's `master`, never force-push, and do not open pull requests unless asked.
- Never test against a real block device. Use image files and loop devices.
- Report only what was run. M1 was built by a delegated agent; its commits, build and tests were spot-checked, but its code was not reviewed line by line.

**Pitfalls already hit**

- Every QML file that uses `FileBrowser` needs `import org.exposuremg.unnamed`.
- A layout nested in a layout fills both directions by default. Set `Layout.fillWidth` and `Layout.fillHeight` explicitly.
- In Kirigami's page row, a page's width comes from its `implicitWidth`, not an attached property.
- Parallel test runs must not share one scratch folder.
