# Progress and Handoff

> Snapshot of the project's Docs version, 2026-10-07.

## Status

Two of six milestones are done: the app browses, extracts, writes and replaces files on FATX images and, on Linux, on drives. The XEX tools (M2) are built and reviewed on Linux. For XBDM (M3) the core has a console backend and the GUI connects to consoles, both tested only against a mock console. Nothing exists for STFS, and none of it has been tried on a real console, a console drive or real titles.

| Milestone | Scope | Status |
| --- | --- | --- |
| M0 | Foundations: streams, generic copy, async jobs, folder and rename operations, feature flags | Done |
| M1 | FATX: browse, write, in-place replace, format, check and repair, drives on Linux | Done on Linux; not tried on a real drive |
| M2 | XEX: info, extract, decrypt and encrypt, compress and decompress, patch, sign | Built on Linux; matches the command line; not tried with real titles |
| M3 | XBDM: file transfer, memory viewer and editor | File backend on UpdClient and its GUI, tested against UpdClient's mock only; memory viewer not started; not tried on any console |
| M4 | STFS: create, inject, extract, delete | Blocked on relicensing or a clean-room version |
| M5 | Packaging and CI for Linux, Windows, macOS, Android | Not started |

## M3 status

M3 is on the app's `m3-xbdm` branch. The XBDM protocol client is UpdClient (`extern/UpdClient`, ExposureMG/UpdClient `main` at `ad83582`), a separate repository that is used here unchanged; you registered it as a submodule in `c089e0e`, together with `extern/curl` and `extern/mbedtls`, which are for future FTP, SMB and an updater and are not built yet.

- `UNNAMED_WITH_XBDM`, ON by default, builds UpdClient's library into the core; without the submodule the build goes on without XBDM.
- `XbdmFileSystem` shows a console's drives as folders and supports list, details, extract, inject, replace, new folder, rename, delete and clear, with the console's own details (name, type, id, execution state, title address, free space). Names, 4 GiB sizes and full drives are refused before anything is sent. An abandoned upload is deleted, or listed as left over; the only copy after a failed replace is kept and named. A replace cancelled or cut off once the old file may be gone asks the console where the upload is, and reports a failure that names the kept copy (or says it may be kept), never just "Cancelled".
- One command connection per place, a second one on demand while a transfer runs, `reconnect()`, a Connected or Disconnected state, `cancel()` from any thread, `connectXbdm()`, `discoverConsoles()`, the registry kind `XBDM`, and `openClient()` for a future memory viewer. `docs/ROADMAP.md` (M3 status) has the details.
- `xbdm_tests` runs it against UpdClient's mock console over an in-memory pipe and loopback TCP, including cancels, drops, reconnects, connection limits and hostile names; 4 GiB - 1 each way on request.
- A review of the XBDM integration by two agents reported 15 distinct problems; the fixes and what was left are under "M3 review round" below.
- Verified only against that mock. Every real target is unverified: devkits running XDKBuild or RGLoader, a retail console running Glitch2 with an XBDM plugin, Xenia and Xenon. The protocol checklist for them is `extern/UpdClient/docs/HARDWARE_TEST_PLAN.md`, which is missing from UpdClient `ad83582` and present in its history at `0f96753`; the app's is under "XBDM hardware checklist for the app" below.
- GUI: *Connect to Console…* with an address, a search of the network and saved consoles; a console is a place with a connected or disconnected badge whose drives are folders, and every file action works on it as on any place. Cancel in Transfers interrupts a blocked call at once; a lost console offers *Reconnect* on the next action and keeps its place; closing says `bye`. Driven headless through the real QML against the mock, killed and restarted mid-upload included (`docs/ROADMAP.md`, M3 status).
- Not built: *Run on Console* (it does not fit File Tools, which do not know where a file lives) and the memory viewer and editor.

### M3 review round

Two agents reviewed the XBDM integration and reported 17 findings, 15 distinct problems (two were reported by both). Each was checked again: 10 were reproduced by a test before the fix (the GUI ones with a throwaway harness that drives the real QML headless against the mock console in its own process), 4 were confirmed by reading the code, and 1 is a gap in UpdClient. Everything here ran against the mock only.

Fixed. The core fixes have regression tests in `xbdm_tests` and `core_tests`; the GUI fixes (the first, and the search and same-console ones) were checked with the throwaway harness, which is not in the repository, and the dialog's wording was not tested:

- A job cancelled while it waited behind another, or whose place was closed, still ran: a queued Delete deleted the folder on the console and ended as Succeeded. It now ends as Cancelled without running, and a closed place's queued listings do nothing.
- A replace cancelled after the old file was deleted showed only "Cancelled", and named a kept copy as fact even when the rename had already taken effect. The console is now asked: a rename that happened is success, otherwise the failure says where the new contents are.
- A new folder, delete or rename whose answer was lost after the console carried it out was sent again and its refusal reported as an error; a recursive delete stopped half way. The refusal now counts as success when the console shows the change done.
- The sizing pass of a copy ignored a cancel and walked the whole tree, reconnecting for each folder. `copyTree` now checks for a cancel at every folder, and lets each level's listing go before it walks the folders.
- A cancel did not reach a call still opening an extra connection, and a greeting timeout there marked the whole place Disconnected.
- Kept copies were never checked again; an upload whose name contained the library's "is kept as" text was taken for a kept copy.
- A path the console refused as too long (406) was reported as "No such file or folder".
- A search stopped and reopened at once showed the stopped search's empty result.
- The same console under another address (127.1, 127.0.0.1) opened a second place. It is now recognised by its console id and port.
- On Windows, console names such as `...`, `a.` or `NUL.txt` would have been merged, renamed or opened as devices when extracted; the host folder now refuses them with the name (built with MinGW only, not run on Windows).
- Found while checking: the Connect dialog offered "the console's name", but UpdClient accepts only IP addresses.

Not fixed:

- With overwrite off, a file another client creates under the target name while an upload runs is deleted and replaced. UpdClient's `FileWriter::finish()` has no "do not replace" mode; reported to its owner and documented in `XbdmFileSystem.hpp`.
- Selecting a file on a console costs several commands and reconnects, and with XEX built in the XEX details read the whole executable (up to 64 MiB) on every selection, on the place's one worker.
- Quitting during a connect can wait up to 10 s: the TCP connect cannot be interrupted, and `XbdmClient::open()` takes no cancel hook.
- `copyTree` has no budget for entries; a hostile console could make it hold about 100,000 folder names per level.

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
| ExposureMG/UnnamedGpl3QtApp | `m3-xbdm` | see `git log` | M3 (XBDM); the review-round commits after `3c72576` are local until you push them |
| ExposureMG/UpdClient | `main` | `ad83582` | The app's `extern/UpdClient` submodule, used unchanged |

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
2. **M3 XBDM.** The protocol client (UpdClient) and the file backend are built against the mock. The GUI is built against the mock, and its first review's problems are fixed except those listed as not fixed. Left: the hex memory viewer and editor, which opens read-only, launching a xex on the console, a cheaper details pass for consoles, UpdClient's "do not replace" mode once its owner adds it, and the hardware checklist.
3. **M4 STFS.** Starts once the STFS files are relicensed or a clean-room version is chosen.
4. **M5 packaging.** Windows and macOS drive access, then installers for each platform and CI that builds the app on all of them.

**Only you can do**

- [ ] Try the FATX features on a real console drive, ideally a spare one or a copy.
- [ ] Run UpdClient's XBDM hardware checklist (`docs/HARDWARE_TEST_PLAN.md`; missing from `ad83582`, in UpdClient's history at `0f96753`) on the targets you have, then the app's checklist below.
- [ ] Choose the gxbuild3 route and ask erorn about the STFS files.
- [ ] Confirm the XexTool licence, and that of XeCrypt and libLZX.
- [ ] Give push access to `ExposureMG/XexTool` so M2 can begin.
- [ ] Open a pull request, or push to main, once so the CI workflow runs for the first time. It only triggers on those.

## XBDM hardware checklist for the app

Nothing in M3 has met a console. On each target you have (a devkit with XDKBuild, a devkit with RGLoader, a retail console with Glitch2 and an XBDM plugin, Xenia, Xenon), with the app's *Connect to Console…*, ideally on a spare drive or folder:

- [ ] Connect by IP address; the place shows the console's name and type. Connect again with the same console's other address, if it has one: the existing place is selected (it is recognised by `getconsoleid` and the port; say whether the target answers `getconsoleid`, and whether two emulators answer the same id).
- [ ] The search finds the console, or note that it does not (networks often drop the broadcast).
- [ ] Browse each drive; the details of a file, a folder and a drive; the Filesystem tab's console details and free space. Note any size shown as unknown.
- [ ] Extract a file and a folder tree; add a file and a folder; replace a file; new folder; rename, and rename changing only the case; delete a file and a folder tree; clear a folder.
- [ ] Errors: a missing file, a name that exists, a protected or read-only location, a full drive if you have one, a path of over 400 characters (which code the console answers, and whether the app says "refused" rather than "not found").
- [ ] Browse while a large download runs (a second connection), and copy a file from the console to itself (two connections). Note how many connections the target accepts before it answers 401.
- [ ] Cancel a large download and a large upload in Transfers: they stop at once, and nothing is left under the final name or as a `.part` file. Cancel a replace of a large file: the file is either replaced, or the error names the kept `.part` copy, which is then on the console.
- [ ] Pull the network cable during an upload: the place shows *Not connected*; reconnect once it is back; the console's details list no leftover after a reconnect.
- [ ] An upload and a download near 4 GiB, if the drive allows.

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
- A place's last reference must not be dropped on its own worker thread: its `QThreadPool` would wait for itself forever. `closeMount()` hands the place to `retire()`, which lets it go on another thread once its worker is done.
- Run headless with `QT_FORCE_STDERR_LOGGING=1`, or Qt's warnings (QML errors included) go to the journal instead of the terminal.
- UpdClient connects to numeric IP addresses only; "localhost" or a console's host name fails to resolve.
- A queued job must look at its cancel flag (and its place's `closed`) before it starts: `JobModel` cancels a queued job like a running one, but only the running one is interrupted.
