#pragma once

#include "core/FileSystemRegistry.hpp"
#include "core/FormatHandlerRegistry.hpp"
#include "gui/FileSystemModel.hpp"
#include "gui/JobModel.hpp"
#include "gui/MountModel.hpp"

#include <QObject>
#include <QStringList>
#include <QThreadPool>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

namespace unnamed::gui {

// QML-facing singleton controller. Owns the open filesystems ("mounts"), the
// current folder, the selection and the details shown in the expanded view.
// The Kirigami UI binds to this and never touches core:: types directly.
class FileBrowser : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(unnamed::gui::FileSystemModel* model READ model CONSTANT)
    Q_PROPERTY(unnamed::gui::MountModel* mounts READ mounts CONSTANT)
    Q_PROPERTY(unnamed::gui::JobModel* jobs READ jobs CONSTANT)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(int currentMount READ currentMount NOTIFY stateChanged)
    Q_PROPERTY(bool isOpen READ isOpen NOTIFY stateChanged)
    Q_PROPERTY(bool canGoUp READ canGoUp NOTIFY stateChanged)
    Q_PROPERTY(QString path READ path NOTIFY stateChanged)
    Q_PROPERTY(QString rootName READ rootName NOTIFY stateChanged)
    Q_PROPERTY(QVariantList pathSegments READ pathSegments NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    // Built with the FATX backend (-DUNNAMED_WITH_FATX=ON).
    Q_PROPERTY(bool fatxAvailable READ fatxAvailable CONSTANT)
    // Built with the XEX tools (-DUNNAMED_WITH_XEX=ON).
    Q_PROPERTY(bool xexAvailable READ xexAvailable CONSTANT)
    // Physical drives can be listed and opened on this platform (Linux for now).
    Q_PROPERTY(bool drivesAvailable READ drivesAvailable CONSTANT)
    // Built with the console backend (UNNAMED_WITH_XBDM, extern/UpdClient).
    Q_PROPERTY(bool xbdmAvailable READ xbdmAvailable CONSTANT)
    // Consoles that answered the last search: [{name, address, port}].
    Q_PROPERTY(QVariantList discoveredConsoles READ discoveredConsoles NOTIFY consolesChanged)
    Q_PROPERTY(bool discovering READ discovering NOTIFY consolesChanged)
    // Why the last search failed; empty when it did not.
    Q_PROPERTY(QString discoveryError READ discoveryError NOTIFY consolesChanged)
    // Consoles reached before, most recent first: [{name, address, port}].
    Q_PROPERTY(QVariantList savedConsoles READ savedConsoles NOTIFY consolesChanged)
    // A connectConsole() in progress, and why the last one failed.
    Q_PROPERTY(bool connecting READ connecting NOTIFY connectingChanged)
    Q_PROPERTY(QString connectError READ connectError NOTIFY connectingChanged)
    // The current place is a console that lost its connection, and why.
    Q_PROPERTY(bool disconnected READ disconnected NOTIFY stateChanged)
    Q_PROPERTY(QString connectionError READ connectionError NOTIFY stateChanged)
    // [{path, model, size, removable, readOnly, inUse, readable, xbox, layout, note}]
    Q_PROPERTY(QVariantList drives READ drives NOTIFY drivesChanged)
    Q_PROPERTY(bool drivesLoading READ drivesLoading NOTIFY drivesChanged)

    // Selection (by name within the current folder) and capability gating.
    Q_PROPERTY(QString selectedName READ selectedName NOTIFY selectionChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    Q_PROPERTY(bool selectedIsDirectory READ selectedIsDirectory NOTIFY selectionChanged)
    Q_PROPERTY(bool canInspect READ canInspect NOTIFY stateChanged)
    Q_PROPERTY(bool canExtract READ canExtract NOTIFY selectionChanged)
    Q_PROPERTY(bool canReplace READ canReplace NOTIFY selectionChanged)
    Q_PROPERTY(bool canRemove READ canRemove NOTIFY selectionChanged)
    Q_PROPERTY(bool canInject READ canInject NOTIFY stateChanged)
    Q_PROPERTY(bool canMakeDirectory READ canMakeDirectory NOTIFY stateChanged)
    Q_PROPERTY(bool canRename READ canRename NOTIFY selectionChanged)

    // Expanded view: {title, subtitle, kind, notice, groups:[{title, items:[{label, value}]}]}
    Q_PROPERTY(QVariantMap itemDetails READ itemDetails NOTIFY detailsChanged)
    Q_PROPERTY(QVariantMap fileSystemDetails READ fileSystemDetails NOTIFY detailsChanged)
    Q_PROPERTY(bool inspectFileSystem READ inspectFileSystem WRITE setInspectFileSystem NOTIFY inspectFileSystemChanged)

    // File tools (format-handler operations) for the selected file:
    // [{handler, handlerName, id, name, description, modifiesSource (may rewrite the
    //   file; see rewritesSource() for the current values), available,
    //   unavailableReason, parameters:[{id, kind, label, help, defaultValue,
    //   options:[{id, label}], minimum, maximum, required, nameFilters, suggestedName}]}]
    // kind is "choice", "boolean", "integer", "text", "inputFile", "outputFile" or "outputFolder".
    Q_PROPERTY(QVariantList fileOperations READ fileOperations NOTIFY fileOperationsChanged)
public:
    explicit FileBrowser(QObject* parent = nullptr);

    FileSystemModel* model() const { return m_model; }
    ~FileBrowser() override;

    MountModel* mounts() const { return m_mounts; }
    JobModel* jobs() const { return m_jobs; }
    bool loading() const { return m_loading; }
    int currentMount() const { return m_mounts->current(); }
    bool isOpen() const { return m_mounts->current() >= 0; }
    bool canGoUp() const { return isOpen() && m_model->path() != QStringLiteral("/"); }
    QString path() const { return m_model->path(); }
    QString rootName() const;
    QVariantList pathSegments() const;
    QString statusText() const;
    QString errorMessage() const { return m_errorMessage; }
    bool fatxAvailable() const;
    bool xexAvailable() const;
    bool drivesAvailable() const;
    bool xbdmAvailable() const;
    QVariantList drives() const { return m_drives; }
    bool drivesLoading() const { return m_drivesLoading; }
    QVariantList discoveredConsoles() const { return m_discovered; }
    bool discovering() const { return m_discovering; }
    QString discoveryError() const { return m_discoveryError; }
    QVariantList savedConsoles() const { return m_savedConsoles; }
    bool connecting() const { return m_connecting; }
    QString connectError() const { return m_connectError; }
    bool disconnected() const;
    QString connectionError() const;

    QString selectedName() const { return m_selectedName; }
    bool hasSelection() const { return !m_selectedName.isEmpty(); }
    bool selectedIsDirectory() const;
    bool canInspect() const { return has(core::Capability::Inspect); }
    bool canExtract() const { return hasSelection() && has(core::Capability::Extract); }
    bool canReplace() const { return hasSelection() && !selectedIsDirectory() && has(core::Capability::Replace); }
    bool canRemove() const { return hasSelection() && has(core::Capability::Remove); }
    bool canInject() const { return has(core::Capability::Inject); }
    bool canMakeDirectory() const { return has(core::Capability::MakeDirectory); }
    bool canRename() const { return hasSelection() && has(core::Capability::Rename); }

    QVariantMap itemDetails() const { return m_itemDetails; }
    QVariantMap fileSystemDetails() const { return m_fileSystemDetails; }
    bool inspectFileSystem() const { return m_inspectFileSystem; }
    void setInspectFileSystem(bool value);
    QVariantList fileOperations() const { return m_fileOperations; }

    // Opening / switching filesystems.
    Q_INVOKABLE void openFolder(const QUrl& folder);
    Q_INVOKABLE void openDemo();
    // Opens the host folder holding `file` as a place and selects the file.
    Q_INVOKABLE void openFile(const QUrl& file);
    // Opens a FATX image read-only; every FATX partition in it (an Xbox 360
    // disk has several) becomes a place.
    Q_INVOKABLE void openFatxImage(const QUrl& file);
    // Lists the drives in the background (drivesChanged). Loop devices (images
    // attached with losetup) only on request.
    Q_INVOKABLE void refreshDrives(bool includeLoop = false);
    // Opens a drive read-only (the system may ask for permission); each FATX
    // partition on it becomes a place.
    Q_INVOKABLE void openDrive(const QString& path);
    // Searches the network for consoles in the background (consolesChanged).
    Q_INVOKABLE void discoverConsoles();
    Q_INVOKABLE void cancelDiscovery();
    // Connects to a console over XBDM in the background; it becomes a place
    // (consoleConnected) and is remembered, or connectError says why not.
    Q_INVOKABLE void connectConsole(const QString& host, int port);
    // Drops a connection attempt in progress.
    Q_INVOKABLE void cancelConnect();
    Q_INVOKABLE void forgetConsole(const QString& address, int port);
    // Opens a console place's connection again; the place keeps its folder.
    Q_INVOKABLE void reconnectMount(int index);
    Q_INVOKABLE void selectMount(int index);
    Q_INVOKABLE void closeMount(int index);

    // Navigation and selection.
    Q_INVOKABLE void navigateTo(const QString& path);
    Q_INVOKABLE void goUp();
    Q_INVOKABLE void select(int row);
    Q_INVOKABLE void clearSelection();
    // Folder: enter it. File: select it.
    Q_INVOKABLE void activate(int row);

    // Operations on the selection / current folder (gated by capabilities).
    Q_INVOKABLE void extractSelected(const QUrl& destinationFolder);
    Q_INVOKABLE void injectFile(const QUrl& file);
    Q_INVOKABLE void replaceSelected(const QUrl& file);
    Q_INVOKABLE void removeSelected();
    Q_INVOKABLE void makeDirectory(const QString& name);
    Q_INVOKABLE void renameSelected(const QString& newName);
    Q_INVOKABLE void refresh();
    // Filesystem health check of a place (read-only); the result arrives as reportReady().
    Q_INVOKABLE void checkMount(int index);
    // Places open read-only; writing needs this explicit unlock (the place is
    // opened again, writable or read-only).
    Q_INVOKABLE void setMountWritable(int index, bool writable);
    // Repairs a writable place (fsck with default answers); reportReady() gives the result.
    Q_INVOKABLE void repairMount(int index);
    // Erases a writable place and creates an empty filesystem labelled `label`.
    Q_INVOKABLE void formatMount(int index, const QString& label);

    // File tools. `values` maps parameter ids to values as the dialog holds
    // them (numbers, booleans, strings; file URLs or local paths).
    // Empty if the values are acceptable, otherwise the first problem.
    Q_INVOKABLE QString validateOperation(const QString& handler, const QString& operation,
                                          const QVariantMap& values) const;
    // Ids of the parameters in effect for these values (the others are hidden).
    Q_INVOKABLE QStringList activeParameters(const QString& handler, const QString& operation,
                                             const QVariantMap& values) const;
    // Whether these values make the operation rewrite the file in place.
    Q_INVOKABLE bool rewritesSource(const QString& handler, const QString& operation,
                                    const QVariantMap& values) const;
    // Runs an operation on the file `fileName` of the current folder as a
    // background job; a report, if any, arrives as reportReady().
    Q_INVOKABLE void runFileOperation(const QString& fileName, const QString& handler, const QString& operation,
                                      const QVariantMap& values);
    // Between the paths shown in text fields and the URLs of file pickers.
    Q_INVOKABLE QString localPath(const QUrl& url) const { return url.isLocalFile() ? url.toLocalFile() : url.toString(); }
    Q_INVOKABLE QUrl fileUrl(const QString& path) const { return QUrl::fromLocalFile(path); }
    // Where the tools' pickers start and relative paths in their file fields
    // point: the current folder when the place is a host folder, otherwise
    // the Documents folder.
    Q_INVOKABLE QUrl outputFolder() const { return QUrl::fromLocalFile(hostFolder()); }
    // A file field's value as the absolute host path it stands for.
    Q_INVOKABLE QString resolvePath(const QString& path) const;
    Q_INVOKABLE bool pathExists(const QString& path) const;

    // Where the console search sends its broadcast: 255.255.255.255, port
    // 730, by default. A directed broadcast (192.168.1.255) keeps it on one
    // network.
    void setDiscoveryTarget(const QString& broadcastAddress, quint16 port);

signals:
    void stateChanged();
    void selectionChanged();
    void detailsChanged();
    void inspectFileSystemChanged();
    void loadingChanged();
    void errorMessageChanged();
    void errorOccurred(const QString& message); // every failure, even a repeated one
    void notice(const QString& message); // transient success message
    void reportReady(const QString& title, const QString& text); // e.g. a health check result
    void drivesChanged();
    void fileOperationsChanged();
    void consolesChanged();
    void connectingChanged();
    void consoleConnected();
    // An action on a console place that is not connected: offer reconnectMount(index).
    void reconnectOffered(int index, const QString& name, const QString& reason);

private:
    // Handed to background work: cancellation flag and throttled progress.
    struct JobContext {
        std::shared_ptr<std::atomic_bool> cancel;
        std::function<void(double fraction, const QString& message)> progress;
        bool cancelled() const { return cancel->load(); }
    };
    using Work = std::function<core::Status(core::FileSystem&, const JobContext&)>;
    using Done = std::function<void(const core::Status&)>;

    std::shared_ptr<MountRuntime> runtime() const;
    bool has(core::Capability cap) const;
    QString selectedPath() const;
    QString hostFolder() const;
    void addMount(const QString& kind, const QString& hostPath, const QString& name);
    void insertMount(Mount mount);
    // Opens every FATX partition on a device in the background and adds them as places.
    void openFatxDevice(const QString& hostPath, const QString& displayName, bool isDrive);
    int indexOfRuntime(const MountRuntime* runtime) const;
    int indexOfFileSystem(const void* filesystem) const;
    // The place of the console with this id on this port, reached under any
    // address; -1 for none or an empty id.
    int indexOfConsole(const QString& consoleId, int port) const;
    void runtimeReplaced(int index);
    // Lets a closed place go once its worker is done, off the UI thread.
    void retire(std::shared_ptr<MountRuntime> rt);
    // A console place's connection state, read from its filesystem.
    void updateConnection(int index);
    // True (and reconnectOffered) when `rt` is a console that is not connected.
    bool offerReconnect(const std::shared_ptr<MountRuntime>& rt);
    void loadSavedConsoles();
    void saveConsole(const QString& name, const QString& address, int port);
    void setSavedConsoles(const QVariantList& consoles);
    void setConnectError(const QString& message);
    void navigate(const QString& path, const QString& selectAfter = {});
    void setLoading(bool loading);
    void setSelectedName(const QString& name);
    void refreshDetails();
    // Not run, and reconnecting offered instead, on a console that is not
    // connected unless `needsConnection` is false.
    void runJob(const QString& title, const std::shared_ptr<MountRuntime>& rt, Work work, Done done,
                bool needsConnection = true);
    void finishOperation(const core::Status& status, const QString& successMessage,
                         const QString& selectAfter = {});
    void setError(const QString& message);
    void setFileOperations(const QVariantList& operations);
    std::optional<core::OperationDescriptor> findOperation(const QString& handler, const QString& operation) const;

    core::FileSystemRegistry m_registry;
    core::FormatHandlerRegistry m_handlers;
    FileSystemModel* m_model;
    MountModel* m_mounts;
    JobModel* m_jobs;
    QString m_selectedName;
    QVariantMap m_itemDetails;
    QVariantMap m_fileSystemDetails;
    QVariantList m_fileOperations;
    bool m_inspectFileSystem = false;
    bool m_loading = false;
    quint64 m_navGeneration = 0;     // drops listing results that were superseded
    quint64 m_detailsGeneration = 0; // same for details
    QString m_errorMessage;
    QVariantList m_drives;
    bool m_drivesLoading = false;
    QVariantList m_discovered;
    bool m_discovering = false;
    QString m_discoveryError;
    std::shared_ptr<std::atomic_bool> m_discoveryCancel;
    QString m_discoveryAddress;
    quint16 m_discoveryPort = 730;
    QVariantList m_savedConsoles;
    bool m_connecting = false;
    QString m_connectError;
    std::shared_ptr<std::atomic_bool> m_connectCancel;
    QThreadPool m_openPool; // opens images off the UI thread; waited for on destruction
};

} // namespace unnamed::gui
