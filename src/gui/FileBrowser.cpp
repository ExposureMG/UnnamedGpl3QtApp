#include "gui/FileBrowser.hpp"

#include "core/LocalFileSystem.hpp"
#include "core/PathUtil.hpp"
#include "core/Transfer.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QRunnable>

namespace unnamed::gui {

namespace {

std::filesystem::path toHostPath(const QString& path) {
    return std::filesystem::path(path.toStdU16String());
}

QVariantMap toVariant(const core::Details& d) {
    QVariantList groups;
    for (const core::PropertyGroup& g : d.groups) {
        QVariantList items;
        for (const core::Property& p : g.items) {
            items.append(QVariantMap{{QStringLiteral("label"), QString::fromStdString(p.label)},
                                     {QStringLiteral("value"), QString::fromStdString(p.value)}});
        }
        groups.append(QVariantMap{{QStringLiteral("title"), QString::fromStdString(g.title)},
                                  {QStringLiteral("items"), items}});
    }
    return {
        {QStringLiteral("title"), QString::fromStdString(d.title)},
        {QStringLiteral("subtitle"), QString::fromStdString(d.subtitle)},
        {QStringLiteral("kind"), QString::fromStdString(d.kind)},
        {QStringLiteral("notice"), QString::fromStdString(d.notice)},
        {QStringLiteral("groups"), groups},
    };
}

QString tr(const char* text) { return QCoreApplication::translate("FileBrowser", text); }

} // namespace

FileBrowser::FileBrowser(QObject* parent)
    : QObject(parent),
      m_registry(core::FileSystemRegistry::withBuiltins()),
      m_model(new FileSystemModel(this)),
      m_mounts(new MountModel(this)),
      m_jobs(new JobModel(this)) {
    connect(m_model, &FileSystemModel::countChanged, this, &FileBrowser::stateChanged);
}

FileBrowser::~FileBrowser() {
    // Stop worker threads before the models they report to go away.
    for (int i = 0; i < m_mounts->count(); ++i) {
        const auto& rt = m_mounts->at(i).runtime;
        m_jobs->cancelAllFor(rt.get());
        rt->pool.clear();
        rt->pool.waitForDone();
    }
}

std::shared_ptr<MountRuntime> FileBrowser::runtime() const {
    return isOpen() ? m_mounts->at(currentMount()).runtime : nullptr;
}

bool FileBrowser::has(core::Capability cap) const {
    const auto rt = runtime();
    return rt && core::hasCapability(rt->capabilities, cap);
}

QString FileBrowser::rootName() const {
    return isOpen() ? m_mounts->at(currentMount()).name : QString();
}

QVariantList FileBrowser::pathSegments() const {
    QVariantList segments;
    if (!isOpen())
        return segments;
    segments.append(QVariantMap{{QStringLiteral("name"), rootName()}, {QStringLiteral("path"), QStringLiteral("/")}});
    QString accumulated;
    for (const QString& part : m_model->path().split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        accumulated += QLatin1Char('/') + part;
        segments.append(QVariantMap{{QStringLiteral("name"), part}, {QStringLiteral("path"), accumulated}});
    }
    return segments;
}

QString FileBrowser::statusText() const {
    if (!isOpen())
        return tr("Open a folder to begin");
    if (m_loading)
        return tr("Loading…");
    if (m_model->filterText().isEmpty())
        return QCoreApplication::translate("FileBrowser", "%n item(s)", nullptr, m_model->count());
    return tr("%1 of %2 items").arg(m_model->count()).arg(m_model->totalCount());
}

bool FileBrowser::selectedIsDirectory() const {
    const int row = m_model->rowForName(m_selectedName);
    return row >= 0 && m_model->entryAt(row).type == core::EntryType::Directory;
}

QString FileBrowser::selectedPath() const {
    return QString::fromStdString(core::joinPath(m_model->path().toStdString(), m_selectedName.toStdString()));
}

void FileBrowser::setInspectFileSystem(bool value) {
    if (m_inspectFileSystem == value)
        return;
    m_inspectFileSystem = value;
    emit inspectFileSystemChanged();
}

void FileBrowser::setLoading(bool loading) {
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
    emit stateChanged();
}

// --- background work --------------------------------------------------------

void FileBrowser::runJob(const QString& title, const std::shared_ptr<MountRuntime>& rt, Work work, Done done) {
    auto cancel = std::make_shared<std::atomic_bool>(false);
    const int id = m_jobs->add(title, rt.get(), cancel);

    JobContext ctx;
    ctx.cancel = cancel;
    auto timer = std::make_shared<QElapsedTimer>();
    timer->start();
    ctx.progress = [this, id, timer](double fraction, const QString& message) {
        // Called on the worker thread; update the UI at most ~12x per second.
        if (timer->elapsed() < 80)
            return;
        timer->restart();
        QMetaObject::invokeMethod(this, [this, id, fraction, message] { m_jobs->setProgress(id, fraction, message); },
                                  Qt::QueuedConnection);
    };

    rt->pool.start(QRunnable::create([this, rt, id, work = std::move(work), done = std::move(done), ctx] {
        const core::Status status = work(*rt->fs, ctx);
        QMetaObject::invokeMethod(
            this,
            [this, id, status, done] {
                m_jobs->finish(id,
                               status.ok ? JobModel::Succeeded
                                         : (status.cancelled ? JobModel::Cancelled : JobModel::Failed),
                               QString::fromStdString(status.message));
                done(status);
            },
            Qt::QueuedConnection);
    }));
}

void FileBrowser::finishOperation(const core::Status& status, const QString& successMessage,
                                  const QString& selectAfter) {
    if (status)
        emit notice(successMessage);
    else if (status.cancelled)
        emit notice(tr("Cancelled"));
    else
        setError(QString::fromStdString(status.message));
    navigate(m_model->path(), selectAfter);
}

// --- opening / switching ----------------------------------------------------

void FileBrowser::openFolder(const QUrl& folder) {
    const QString hostPath = folder.toLocalFile();
    const QString name = hostPath.section(QLatin1Char('/'), -1, -1, QString::SectionSkipEmpty);
    addMount(QStringLiteral("Local"), hostPath, name.isEmpty() ? hostPath : name);
}

void FileBrowser::openDemo() {
    addMount(QStringLiteral("Demo"), {}, tr("Demo (sample data)"));
}

void FileBrowser::addMount(const QString& kind, const QString& hostPath, const QString& name) {
    const int existing = m_mounts->indexOf(kind, hostPath);
    if (existing >= 0) {
        selectMount(existing);
        return;
    }

    std::string error;
    auto fs = m_registry.open(kind.toStdString(), toHostPath(hostPath), error);
    if (!fs) {
        setError(QString::fromStdString(error));
        return;
    }
    Mount mount;
    mount.name = name;
    mount.kind = kind;
    mount.hostPath = hostPath;
    mount.runtime = std::make_shared<MountRuntime>(std::move(fs));
    m_mounts->add(std::move(mount));
    setError({});
    selectMount(m_mounts->count() - 1);
}

void FileBrowser::selectMount(int index) {
    if (index < 0 || index >= m_mounts->count())
        return;
    m_mounts->setCurrent(index);
    m_model->clear();
    m_selectedName.clear();
    emit selectionChanged();
    navigate(m_mounts->at(index).currentPath);
}

void FileBrowser::closeMount(int index) {
    if (index < 0 || index >= m_mounts->count())
        return;
    const auto rt = m_mounts->at(index).runtime;
    const bool wasCurrent = index == currentMount();
    m_jobs->cancelAllFor(rt.get());
    m_mounts->remove(index);
    if (!wasCurrent) {
        emit stateChanged();
        return;
    }
    ++m_navGeneration; // drop any listing still in flight for the closed mount
    m_selectedName.clear();
    if (m_mounts->count() > 0) {
        selectMount(qMin(index, m_mounts->count() - 1));
    } else {
        m_model->clear();
        setLoading(false);
        refreshDetails();
        emit selectionChanged();
        emit stateChanged();
    }
}

// --- navigation / selection -------------------------------------------------

void FileBrowser::navigateTo(const QString& path) {
    if (isOpen())
        navigate(path);
}

void FileBrowser::goUp() {
    if (canGoUp())
        navigate(QString::fromStdString(core::parentPath(m_model->path().toStdString())));
}

void FileBrowser::refresh() {
    if (isOpen())
        navigate(m_model->path(), m_selectedName);
}

void FileBrowser::navigate(const QString& path, const QString& selectAfter) {
    const auto rt = runtime();
    if (!rt)
        return;
    const quint64 generation = ++m_navGeneration;
    setLoading(true);

    rt->pool.start(QRunnable::create([this, rt, path, selectAfter, generation] {
        std::vector<core::Entry> entries;
        const core::Status status = rt->fs->list(path.toStdString(), entries);
        QMetaObject::invokeMethod(
            this,
            [this, rt, path, selectAfter, generation, status, entries = std::move(entries)]() mutable {
                if (generation != m_navGeneration)
                    return; // superseded by a newer navigation, or the mount was closed
                setLoading(false);
                if (status) {
                    m_model->setEntries(path, std::move(entries));
                    m_mounts->at(currentMount()).currentPath = path;
                    m_selectedName = (!selectAfter.isEmpty() && m_model->rowForName(selectAfter) >= 0)
                                         ? selectAfter
                                         : QString();
                    setError({});
                } else {
                    setError(QString::fromStdString(status.message));
                    if (path != QStringLiteral("/") && m_model->path() != path)
                        navigate(QStringLiteral("/")); // e.g. a remembered folder is gone
                }
                refreshDetails();
                emit selectionChanged();
                emit stateChanged();
            },
            Qt::QueuedConnection);
    }));
}

void FileBrowser::setSelectedName(const QString& name) {
    if (m_selectedName == name)
        return;
    m_selectedName = name;
    refreshDetails();
    emit selectionChanged();
}

void FileBrowser::select(int row) {
    if (row < 0 || row >= m_model->count())
        return;
    setSelectedName(QString::fromStdString(m_model->entryAt(row).name));
}

void FileBrowser::clearSelection() {
    if (hasSelection())
        setSelectedName({});
}

void FileBrowser::activate(int row) {
    if (row < 0 || row >= m_model->count())
        return;
    const core::Entry& entry = m_model->entryAt(row);
    if (entry.type == core::EntryType::Directory)
        navigate(QString::fromStdString(core::joinPath(m_model->path().toStdString(), entry.name)));
    else
        select(row);
}

void FileBrowser::refreshDetails() {
    const auto rt = runtime();
    const quint64 generation = ++m_detailsGeneration;
    if (!rt || !core::hasCapability(rt->capabilities, core::Capability::Inspect)) {
        m_itemDetails.clear();
        m_fileSystemDetails.clear();
        emit detailsChanged();
        return;
    }

    const std::string target = hasSelection() ? selectedPath().toStdString() : m_model->path().toStdString();
    rt->pool.start(QRunnable::create([this, rt, target, generation] {
        core::Details item, filesystem;
        const bool haveItem = bool(rt->fs->describe(target, item));
        const bool haveFs = bool(rt->fs->describeFileSystem(filesystem));
        QMetaObject::invokeMethod(
            this,
            [this, generation, haveItem, haveFs, item = std::move(item), filesystem = std::move(filesystem)] {
                if (generation != m_detailsGeneration)
                    return;
                m_itemDetails = haveItem ? toVariant(item) : QVariantMap();
                m_fileSystemDetails = haveFs ? toVariant(filesystem) : QVariantMap();
                emit detailsChanged();
            },
            Qt::QueuedConnection);
    }));
}

// --- operations (each one is a cancellable background job) -------------------

void FileBrowser::extractSelected(const QUrl& destinationFolder) {
    const auto rt = runtime();
    if (!rt || !canExtract())
        return;
    const QString name = m_selectedName;
    const std::string source = selectedPath().toStdString();
    const QString destination = destinationFolder.toLocalFile();

    runJob(tr("Extract %1").arg(name), rt,
           [=](core::FileSystem& fs, const JobContext& ctx) {
               core::LocalFileSystem host(toHostPath(destination));
               core::TransferOptions options;
               options.progress = [&](const core::TransferProgress& p) {
                   ctx.progress(p.bytesTotal ? double(p.bytesDone) / double(p.bytesTotal) : -1.0,
                                QString::fromStdString(p.current));
                   return !ctx.cancelled();
               };
               return core::copyTree(fs, source, host, "/" + name.toStdString(), options);
           },
           [this, name](const core::Status& st) { finishOperation(st, tr("Extracted %1").arg(name)); });
}

void FileBrowser::injectFile(const QUrl& file) {
    const auto rt = runtime();
    if (!rt || !canInject())
        return;
    const QFileInfo info(file.toLocalFile());
    const QString name = info.fileName();
    const QString hostDir = info.absolutePath();
    const std::string destination = core::joinPath(m_model->path().toStdString(), name.toStdString());

    runJob(tr("Add %1").arg(name), rt,
           [=](core::FileSystem& fs, const JobContext& ctx) {
               core::LocalFileSystem host(toHostPath(hostDir));
               core::TransferOptions options;
               options.progress = [&](const core::TransferProgress& p) {
                   ctx.progress(p.bytesTotal ? double(p.bytesDone) / double(p.bytesTotal) : -1.0,
                                QString::fromStdString(p.current));
                   return !ctx.cancelled();
               };
               return core::copyTree(host, "/" + name.toStdString(), fs, destination, options);
           },
           [this, name](const core::Status& st) { finishOperation(st, tr("Added %1").arg(name), name); });
}

void FileBrowser::replaceSelected(const QUrl& file) {
    const auto rt = runtime();
    if (!rt || !canReplace())
        return;
    const QFileInfo info(file.toLocalFile());
    const QString hostDir = info.absolutePath();
    const QString hostName = info.fileName();
    const QString name = m_selectedName;
    const std::string destination = selectedPath().toStdString();

    runJob(tr("Replace %1").arg(name), rt,
           [=](core::FileSystem& fs, const JobContext& ctx) {
               core::LocalFileSystem host(toHostPath(hostDir));
               core::TransferOptions options;
               options.overwrite = true;
               options.progress = [&](const core::TransferProgress& p) {
                   ctx.progress(p.bytesTotal ? double(p.bytesDone) / double(p.bytesTotal) : -1.0,
                                QString::fromStdString(p.current));
                   return !ctx.cancelled();
               };
               return core::copyTree(host, "/" + hostName.toStdString(), fs, destination, options);
           },
           [this, name](const core::Status& st) { finishOperation(st, tr("Replaced %1").arg(name), name); });
}

void FileBrowser::removeSelected() {
    const auto rt = runtime();
    if (!rt || !canRemove())
        return;
    const QString name = m_selectedName;
    const std::string path = selectedPath().toStdString();
    runJob(tr("Delete %1").arg(name), rt,
           [path](core::FileSystem& fs, const JobContext&) { return fs.remove(path); },
           [this, name](const core::Status& st) { finishOperation(st, tr("Deleted %1").arg(name)); });
}

void FileBrowser::makeDirectory(const QString& name) {
    const auto rt = runtime();
    if (!rt || !canMakeDirectory() || name.trimmed().isEmpty())
        return;
    const QString folder = name.trimmed();
    const std::string path = core::joinPath(m_model->path().toStdString(), folder.toStdString());
    runJob(tr("New folder %1").arg(folder), rt,
           [path](core::FileSystem& fs, const JobContext&) { return fs.makeDirectory(path); },
           [this, folder](const core::Status& st) { finishOperation(st, tr("Created %1").arg(folder), folder); });
}

void FileBrowser::renameSelected(const QString& newName) {
    const auto rt = runtime();
    if (!rt || !canRename() || newName.trimmed().isEmpty() || newName == m_selectedName)
        return;
    const QString oldName = m_selectedName;
    const QString target = newName.trimmed();
    const std::string path = selectedPath().toStdString();
    runJob(tr("Rename %1").arg(oldName), rt,
           [path, target](core::FileSystem& fs, const JobContext&) { return fs.rename(path, target.toStdString()); },
           [this, oldName, target](const core::Status& st) {
               finishOperation(st, tr("Renamed %1 to %2").arg(oldName, target), st ? target : oldName);
           });
}

void FileBrowser::setError(const QString& message) {
    if (m_errorMessage != message) {
        m_errorMessage = message;
        emit errorMessageChanged();
    }
    if (!message.isEmpty())
        emit errorOccurred(message);
}

} // namespace unnamed::gui
