#pragma once

#include "core/FileSystemRegistry.hpp"
#include "gui/FileSystemModel.hpp"
#include "gui/JobModel.hpp"
#include "gui/MountModel.hpp"

#include <QObject>
#include <QThreadPool>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <functional>
#include <memory>

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
    // Physical drives can be listed and opened on this platform (Linux for now).
    Q_PROPERTY(bool drivesAvailable READ drivesAvailable CONSTANT)
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
    bool drivesAvailable() const;
    QVariantList drives() const { return m_drives; }
    bool drivesLoading() const { return m_drivesLoading; }

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

    // Opening / switching filesystems.
    Q_INVOKABLE void openFolder(const QUrl& folder);
    Q_INVOKABLE void openDemo();
    // Opens a FATX image read-only; every FATX partition in it (an Xbox 360
    // disk has several) becomes a place.
    Q_INVOKABLE void openFatxImage(const QUrl& file);
    // Lists the drives in the background (drivesChanged). Loop devices (images
    // attached with losetup) only on request.
    Q_INVOKABLE void refreshDrives(bool includeLoop = false);
    // Opens a drive read-only (the system may ask for permission); each FATX
    // partition on it becomes a place.
    Q_INVOKABLE void openDrive(const QString& path);
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
    void addMount(const QString& kind, const QString& hostPath, const QString& name);
    void insertMount(Mount mount);
    // Opens every FATX partition on a device in the background and adds them as places.
    void openFatxDevice(const QString& hostPath, const QString& displayName, bool isDrive);
    int indexOfRuntime(const MountRuntime* runtime) const;
    void runtimeReplaced(int index);
    void navigate(const QString& path, const QString& selectAfter = {});
    void setLoading(bool loading);
    void setSelectedName(const QString& name);
    void refreshDetails();
    void runJob(const QString& title, const std::shared_ptr<MountRuntime>& rt, Work work, Done done);
    void finishOperation(const core::Status& status, const QString& successMessage,
                         const QString& selectAfter = {});
    void setError(const QString& message);

    core::FileSystemRegistry m_registry;
    FileSystemModel* m_model;
    MountModel* m_mounts;
    JobModel* m_jobs;
    QString m_selectedName;
    QVariantMap m_itemDetails;
    QVariantMap m_fileSystemDetails;
    bool m_inspectFileSystem = false;
    bool m_loading = false;
    quint64 m_navGeneration = 0;     // drops listing results that were superseded
    quint64 m_detailsGeneration = 0; // same for details
    QString m_errorMessage;
    QVariantList m_drives;
    bool m_drivesLoading = false;
    QThreadPool m_openPool; // opens images off the UI thread; waited for on destruction
};

} // namespace unnamed::gui
