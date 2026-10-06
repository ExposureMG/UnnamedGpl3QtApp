#include "gui/FileBrowser.hpp"

#ifdef UNNAMED_WITH_FATX
#include "core/FatxFileSystem.hpp"
#endif
#include "core/Drives.hpp"
#include "core/Format.hpp"
#include "core/LocalFileSystem.hpp"
#include "core/PathUtil.hpp"
#include "core/Transfer.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QRunnable>
#include <QStandardPaths>

#include <cmath>

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

const char* kindName(core::ParameterKind kind) {
    switch (kind) {
    case core::ParameterKind::Choice: return "choice";
    case core::ParameterKind::Boolean: return "boolean";
    case core::ParameterKind::Integer: return "integer";
    case core::ParameterKind::Text: return "text";
    case core::ParameterKind::InputFile: return "inputFile";
    case core::ParameterKind::OutputFile: return "outputFile";
    case core::ParameterKind::OutputFolder: return "outputFolder";
    }
    return "text";
}

QVariant toVariant(const core::ParameterValue& value) {
    if (const bool* b = std::get_if<bool>(&value))
        return *b;
    if (const std::int64_t* i = std::get_if<std::int64_t>(&value))
        return QVariant::fromValue<qlonglong>(*i);
    return QString::fromStdString(std::get<std::string>(value));
}

QVariantMap toVariant(const core::FormatHandler& handler, const core::OperationDescriptor& op,
                      const std::string& sourceName, bool canReplace) {
    QVariantList parameters;
    for (const core::ParameterDescriptor& p : op.parameters) {
        QVariantList options;
        for (const core::ChoiceOption& o : p.options)
            options.append(QVariantMap{{QStringLiteral("id"), QString::fromStdString(o.id)},
                                       {QStringLiteral("label"), QString::fromStdString(o.label)}});
        QStringList filters;
        for (const std::string& f : p.nameFilters)
            filters.append(QString::fromStdString(f));
        parameters.append(QVariantMap{
            {QStringLiteral("id"), QString::fromStdString(p.id)},
            {QStringLiteral("kind"), QString::fromLatin1(kindName(p.kind))},
            {QStringLiteral("label"), QString::fromStdString(p.label)},
            {QStringLiteral("help"), QString::fromStdString(p.help)},
            {QStringLiteral("defaultValue"), toVariant(p.defaultValue)},
            {QStringLiteral("options"), options},
            {QStringLiteral("minimum"), QVariant::fromValue<qlonglong>(p.minimum)},
            {QStringLiteral("maximum"), QVariant::fromValue<qlonglong>(p.maximum)},
            {QStringLiteral("required"), p.required},
            {QStringLiteral("nameFilters"), filters},
            {QStringLiteral("suggestedName"),
             QString::fromStdString(core::expandSuggestedName(p.suggestedName, sourceName))},
        });
    }
    // An operation that rewrites the source only on request stays available,
    // and validateOperation() refuses that choice on a read-only place.
    const bool available = !op.modifiesSource || !op.modifiesSourceWhen.parameter.empty() || canReplace;
    return {
        {QStringLiteral("handler"), QString::fromStdString(handler.id())},
        {QStringLiteral("handlerName"), QString::fromStdString(handler.name())},
        {QStringLiteral("id"), QString::fromStdString(op.id)},
        {QStringLiteral("name"), QString::fromStdString(op.name)},
        {QStringLiteral("description"), QString::fromStdString(op.description)},
        {QStringLiteral("modifiesSource"), op.modifiesSource},
        {QStringLiteral("available"), available},
        {QStringLiteral("unavailableReason"),
         available ? QString() : tr("This place is read-only, and this tool changes the file.")},
        {QStringLiteral("parameters"), parameters},
    };
}

// A file field's text (a path, relative to `base`, or a file URL) as an absolute path.
QString resolveHostPath(QString path, const QString& base) {
    path = path.trimmed();
    if (path.startsWith(QStringLiteral("file:")))
        path = QUrl(path).toLocalFile();
    if (path.isEmpty() || QDir::isAbsolutePath(path))
        return path;
    return QDir::cleanPath(QDir(base).filePath(path));
}

// Dialog values to typed parameters, file paths resolved against `base`. A
// value that does not fit its kind is passed on as text, so validation names
// the problem.
core::Parameters toParameters(const core::OperationDescriptor& op, const QVariantMap& values, const QString& base) {
    core::Parameters out;
    for (auto it = values.begin(); it != values.end(); ++it) {
        const std::string id = it.key().toStdString();
        const QVariant& v = it.value();
        const core::ParameterDescriptor* p = core::findParameter(op, id);
        const core::ParameterKind kind = p ? p->kind : core::ParameterKind::Text;
        if (kind == core::ParameterKind::Boolean && v.typeId() == QMetaType::Bool) {
            out[id] = v.toBool();
        } else if (kind == core::ParameterKind::Integer) {
            bool ok = false;
            qlonglong n = 0;
            if (v.typeId() == QMetaType::Double) {
                const double d = v.toDouble();
                ok = std::isfinite(d) && d == std::floor(d) && std::fabs(d) < 9.0e18;
                n = ok ? static_cast<qlonglong>(d) : 0;
            } else if (v.typeId() == QMetaType::QString) {
                n = v.toString().trimmed().toLongLong(&ok);
            } else {
                n = v.toLongLong(&ok);
            }
            if (ok)
                out[id] = std::int64_t{n};
            else
                out[id] = v.toString().toStdString();
        } else if (kind == core::ParameterKind::InputFile || kind == core::ParameterKind::OutputFile ||
                   kind == core::ParameterKind::OutputFolder) {
            out[id] = resolveHostPath(v.toString(), base).toStdString();
        } else {
            out[id] = v.toString().toStdString();
        }
    }
    return out;
}

} // namespace

FileBrowser::FileBrowser(QObject* parent)
    : QObject(parent),
      m_registry(core::FileSystemRegistry::withBuiltins()),
      m_handlers(core::FormatHandlerRegistry::withBuiltins()),
      m_model(new FileSystemModel(this)),
      m_mounts(new MountModel(this)),
      m_jobs(new JobModel(this)) {
    connect(m_model, &FileSystemModel::countChanged, this, &FileBrowser::stateChanged);
}

FileBrowser::~FileBrowser() {
    // Stop worker threads before the models they report to go away.
    m_openPool.waitForDone();
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

bool FileBrowser::fatxAvailable() const {
#ifdef UNNAMED_WITH_FATX
    return true;
#else
    return false;
#endif
}

bool FileBrowser::xexAvailable() const {
#ifdef UNNAMED_WITH_XEX
    return true;
#else
    return false;
#endif
}

bool FileBrowser::drivesAvailable() const { return fatxAvailable() && core::drivesSupported(); }

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

QString FileBrowser::hostFolder() const {
    if (isOpen()) {
        const Mount& mount = m_mounts->at(currentMount());
        if (mount.kind == QStringLiteral("Local") && !mount.hostPath.isEmpty())
            return QDir::cleanPath(mount.hostPath + m_model->path());
    }
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

QString FileBrowser::resolvePath(const QString& path) const { return resolveHostPath(path, hostFolder()); }

bool FileBrowser::pathExists(const QString& path) const {
    const QString resolved = resolvePath(path);
    return !resolved.isEmpty() && QFileInfo::exists(resolved);
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

void FileBrowser::openFile(const QUrl& file) {
    const QFileInfo info(file.toLocalFile());
    const QString folder = info.absolutePath();
    openFolder(QUrl::fromLocalFile(folder));
    if (isOpen() && m_mounts->indexOf(QStringLiteral("Local"), folder) == currentMount())
        navigate(QStringLiteral("/"), info.fileName());
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
    insertMount(std::move(mount));
}

void FileBrowser::insertMount(Mount mount) {
    m_mounts->add(std::move(mount));
    setError({});
    selectMount(m_mounts->count() - 1);
}

void FileBrowser::openFatxImage(const QUrl& file) {
    const QString hostPath = file.toLocalFile();
    openFatxDevice(hostPath, QFileInfo(hostPath).fileName(), false);
}

void FileBrowser::openDrive(const QString& path) {
    QString name = path;
    for (const QVariant& d : m_drives) {
        const QVariantMap drive = d.toMap();
        if (drive.value(QStringLiteral("path")).toString() == path && !drive.value(QStringLiteral("model")).toString().isEmpty())
            name = QStringLiteral("%1 (%2)").arg(drive.value(QStringLiteral("model")).toString(), path);
    }
    openFatxDevice(path, name, true);
}

void FileBrowser::openFatxDevice(const QString& hostPath, const QString& displayName, bool isDrive) {
#ifdef UNNAMED_WITH_FATX
    for (int i = 0; i < m_mounts->count(); ++i) {
        if (m_mounts->at(i).kind == QStringLiteral("FATX") && m_mounts->at(i).hostPath == hostPath) {
            selectMount(i); // already open
            return;
        }
    }

    // A drive or image file, read-only or writable.
    using Opener = std::function<core::Result<std::shared_ptr<core::BlockDevice>>(bool)>;
    const Opener openDevice = [hostPath, isDrive](bool writable) {
        return isDrive ? core::openDrive(hostPath.toStdString(), writable)
                       : core::openImageFile(toHostPath(hostPath), writable);
    };

    // Opening reads the whole directory tree: do it in the background.
    struct Opened {
        std::vector<Mount> mounts;
        QString error;
        QString readOnlyReason; // a drive that could only be opened read-only
    };
    auto result = std::make_shared<Opened>();
    auto cancel = std::make_shared<std::atomic_bool>(false);
    const int id = m_jobs->add(tr("Open %1").arg(displayName), this, cancel);
    m_openPool.start([this, id, hostPath, displayName, isDrive, openDevice, result] {
        // Drives open read-write when they can (falling back to read-only with
        // the reason); images open read-only until "Enable Writing".
        bool writable = false;
        core::Result<std::shared_ptr<core::BlockDevice>> device = core::Status::failure("");
        if (isDrive) {
            auto opened = core::openDriveForUse(hostPath.toStdString());
            if (opened) {
                device = opened.value().device;
                writable = opened.value().writable;
                result->readOnlyReason = QString::fromStdString(opened.value().readOnlyReason);
            } else {
                device = opened.status();
            }
        } else {
            device = openDevice(false);
        }
        if (!device) {
            result->error = QString::fromStdString(device.status().message);
        } else {
            const auto partitions = core::probeFatx(device.value());
            if (partitions.empty())
                result->error = tr("No FATX filesystem found on %1").arg(displayName);
            for (const core::FatxPartition& p : partitions) {
                const QString partName = QString::fromStdString(p.name);
                const QString name =
                    partitions.size() == 1 ? displayName : QStringLiteral("%1 – %2").arg(displayName, partName);
                auto fs = core::openFatx(device.value(), p, writable, name.toStdString());
                if (!fs) {
                    result->error = QString::fromStdString(fs.status().message);
                    continue;
                }
                Mount mount;
                mount.name = name;
                mount.kind = QStringLiteral("FATX");
                mount.hostPath = hostPath;
                mount.detail = QString::fromStdString(p.table + "/" + p.partition);
                mount.subtitle = isDrive ? tr("FATX drive · %1").arg(partName)
                                         : tr("FATX · %1").arg(partitions.size() == 1 ? displayName : partName);
                mount.isDrive = isDrive;
                mount.writable = writable;
                mount.runtime = std::make_shared<MountRuntime>(std::move(fs.value()));
                mount.reopen = [openDevice, p, name](bool writable) -> core::Result<std::unique_ptr<core::FileSystem>> {
                    auto dev = openDevice(writable);
                    if (!dev)
                        return dev.status();
                    return core::openFatx(dev.value(), p, writable, name.toStdString());
                };
                result->mounts.push_back(std::move(mount));
            }
        }
        QMetaObject::invokeMethod(
            this,
            [this, id, result, displayName] {
                m_jobs->finish(id, result->mounts.empty() ? JobModel::Failed : JobModel::Succeeded, result->error);
                for (Mount& m : result->mounts)
                    insertMount(std::move(m));
                if (!result->error.isEmpty())
                    setError(result->error);
                else if (!result->readOnlyReason.isEmpty())
                    emit reportReady(tr("Opened read-only"),
                                     tr("%1 could not be opened for writing, so it is open read-only.\n\n%2")
                                         .arg(displayName, result->readOnlyReason));
            },
            Qt::QueuedConnection);
    });
#else
    Q_UNUSED(isDrive);
    setError(tr("This build has no FATX support (%1)").arg(displayName.isEmpty() ? hostPath : displayName));
#endif
}

void FileBrowser::refreshDrives(bool includeLoop) {
    if (m_drivesLoading)
        return;
    m_drivesLoading = true;
    emit drivesChanged();
    auto list = std::make_shared<QVariantList>();
    m_openPool.start([this, includeLoop, list] {
        for (const core::DriveInfo& d : core::listDrives(includeLoop)) {
            QString layout;
            QString note;
            bool xbox = false;
#ifdef UNNAMED_WITH_FATX
            // Look inside only when no permission is needed (no password prompt
            // just for listing); read-only, a few sectors.
            if (d.readable) {
                if (auto dev = core::openDeviceNode(d.path, false)) {
                    const auto parts = core::probeFatx(dev.value());
                    if (!parts.empty()) {
                        layout = QString::fromStdString(parts.front().tableName);
                        xbox = parts.front().table == "hd" || parts.front().table == "kit";
                        note = tr("%n FATX partition(s)", nullptr, int(parts.size()));
                    }
                }
            } else {
                note = tr("Opening asks for permission");
            }
#endif
            if (d.inUse)
                note = note.isEmpty() ? tr("In use (mounted)") : note + QStringLiteral(" · ") + tr("in use (mounted)");
            QString model = QString::fromStdString(d.vendor.empty() ? d.model : d.vendor + " " + d.model).trimmed();
            list->append(QVariantMap{
                {QStringLiteral("path"), QString::fromStdString(d.path)},
                {QStringLiteral("model"), model},
                {QStringLiteral("size"), QString::fromStdString(core::humanSize(d.size)).section(QLatin1Char(' '), 0, 1)},
                {QStringLiteral("removable"), d.removable},
                {QStringLiteral("readOnly"), d.readOnly},
                {QStringLiteral("inUse"), d.inUse},
                {QStringLiteral("readable"), d.readable},
                {QStringLiteral("xbox"), xbox},
                {QStringLiteral("layout"), layout},
                {QStringLiteral("note"), note},
            });
        }
        QMetaObject::invokeMethod(
            this,
            [this, list] {
                m_drives = *list;
                m_drivesLoading = false;
                emit drivesChanged();
            },
            Qt::QueuedConnection);
    });
}

int FileBrowser::indexOfRuntime(const MountRuntime* runtime) const {
    for (int i = 0; i < m_mounts->count(); ++i) {
        if (m_mounts->at(i).runtime.get() == runtime)
            return i;
    }
    return -1;
}

// After a place's filesystem was replaced or changed as a whole.
void FileBrowser::runtimeReplaced(int index) {
    m_mounts->changed(index);
    if (index == currentMount()) {
        navigate(m_mounts->at(index).currentPath, m_selectedName);
        emit selectionChanged();
        emit stateChanged();
    }
}

void FileBrowser::setMountWritable(int index, bool writable) {
    if (index < 0 || index >= m_mounts->count())
        return;
    Mount& mount = m_mounts->at(index);
    if (!mount.reopen || mount.busy || mount.writable == writable)
        return;
    // Nothing may run on the old filesystem while the new one opens.
    const auto old = mount.runtime;
    m_jobs->cancelAllFor(old.get());
    old->pool.clear();
    old->pool.waitForDone();
    mount.busy = true;
    m_mounts->changed(index);

    const QString name = mount.name;
    auto reopen = mount.reopen;
    auto cancel = std::make_shared<std::atomic_bool>(false);
    const int id = m_jobs->add(writable ? tr("Enable writing to %1").arg(name) : tr("Make %1 read-only").arg(name),
                               this, cancel);
    m_openPool.start([this, id, old, reopen, writable] {
        auto opened = std::make_shared<core::Result<std::unique_ptr<core::FileSystem>>>(reopen(writable));
        QMetaObject::invokeMethod(
            this,
            [this, id, old, opened, writable] {
                const int i = indexOfRuntime(old.get());
                if (i < 0) { // closed meanwhile
                    m_jobs->finish(id, JobModel::Cancelled, {});
                    return;
                }
                Mount& m = m_mounts->at(i);
                m.busy = false;
                if (!*opened) {
                    m_jobs->finish(id, JobModel::Failed, QString::fromStdString(opened->status().message));
                    setError(QString::fromStdString(opened->status().message));
                    m_mounts->changed(i);
                    return;
                }
                m.runtime = std::make_shared<MountRuntime>(std::move(opened->value()));
                m.writable = writable;
                m_jobs->finish(id, JobModel::Succeeded, {});
                emit notice(writable ? tr("%1 is read-write").arg(m.name) : tr("%1 is read-only").arg(m.name));
                runtimeReplaced(i);
            },
            Qt::QueuedConnection);
    });
}

void FileBrowser::repairMount(int index) {
    if (index < 0 || index >= m_mounts->count())
        return;
    const auto rt = m_mounts->at(index).runtime;
    if (!core::hasCapability(rt->capabilities, core::Capability::Repair))
        return;
    const QString name = m_mounts->at(index).name;
    auto report = std::make_shared<std::string>();
    runJob(tr("Repair %1").arg(name), rt,
           [report](core::FileSystem& fs, const JobContext&) { return fs.repair(*report); },
           [this, rt, name, report](const core::Status& st) {
               if (st)
                   emit reportReady(tr("Repair: %1").arg(name), QString::fromStdString(*report));
               else
                   setError(QString::fromStdString(st.message));
               if (const int i = indexOfRuntime(rt.get()); i >= 0)
                   runtimeReplaced(i);
           });
}

void FileBrowser::formatMount(int index, const QString& label) {
    if (index < 0 || index >= m_mounts->count())
        return;
    const auto rt = m_mounts->at(index).runtime;
    if (!core::hasCapability(rt->capabilities, core::Capability::Format) || !m_mounts->at(index).writable)
        return;
    const QString name = m_mounts->at(index).name;
    const std::string newLabel = label.trimmed().toStdString();
    runJob(tr("Format %1").arg(name), rt,
           [newLabel](core::FileSystem& fs, const JobContext&) { return fs.format(newLabel); },
           [this, rt, name](const core::Status& st) {
               if (st)
                   emit notice(tr("Formatted %1").arg(name));
               else
                   setError(QString::fromStdString(st.message));
               if (const int i = indexOfRuntime(rt.get()); i >= 0) {
                   m_mounts->at(i).currentPath = QStringLiteral("/");
                   runtimeReplaced(i);
               }
           });
}

void FileBrowser::checkMount(int index) {
    if (index < 0 || index >= m_mounts->count())
        return;
    const auto rt = m_mounts->at(index).runtime;
    if (!core::hasCapability(rt->capabilities, core::Capability::HealthCheck))
        return;
    const QString name = m_mounts->at(index).name;
    auto report = std::make_shared<std::string>();
    runJob(tr("Check %1").arg(name), rt,
           [report](core::FileSystem& fs, const JobContext&) { return fs.healthCheck(*report); },
           [this, name, report](const core::Status& st) {
               if (st)
                   emit reportReady(tr("Health check: %1").arg(name), QString::fromStdString(*report));
               else
                   setError(QString::fromStdString(st.message));
           });
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
    setFileOperations({});
    const bool inspect = rt && core::hasCapability(rt->capabilities, core::Capability::Inspect);
    // File tools and format details read the selected file.
    const bool tools = rt && hasSelection() && !selectedIsDirectory() &&
                       core::hasCapability(rt->capabilities, core::Capability::Extract);
    if (!inspect && !tools) {
        m_itemDetails.clear();
        m_fileSystemDetails.clear();
        emit detailsChanged();
        return;
    }

    const std::string target = hasSelection() ? selectedPath().toStdString() : m_model->path().toStdString();
    const bool canReplace = core::hasCapability(rt->capabilities, core::Capability::Replace);
    const core::FormatHandlerRegistry handlers = m_handlers; // shared, stateless handlers
    rt->pool.start(QRunnable::create([this, rt, target, generation, inspect, tools, canReplace, handlers] {
        core::Details item, filesystem;
        const bool haveItem = inspect && bool(rt->fs->describe(target, item));
        const bool haveFs = inspect && bool(rt->fs->describeFileSystem(filesystem));
        QVariantList operations;
        if (tools) {
            if (auto probe = core::probeFile(*rt->fs, target)) {
                for (const auto& match : handlers.match(probe.value())) {
                    for (const core::OperationDescriptor& op : core::applicableOperations(*match.handler, probe.value()))
                        operations.append(toVariant(*match.handler, op, probe->name, canReplace));
                    if (!haveItem)
                        continue;
                    auto in = rt->fs->openRead(target);
                    std::vector<core::PropertyGroup> groups;
                    if (in && match.handler->describe(*in.value(), probe.value(), groups))
                        item.groups.insert(item.groups.end(), groups.begin(), groups.end());
                }
            }
        }
        QMetaObject::invokeMethod(
            this,
            [this, generation, haveItem, haveFs, item = std::move(item), filesystem = std::move(filesystem),
             operations = std::move(operations)] {
                if (generation != m_detailsGeneration)
                    return;
                m_itemDetails = haveItem ? toVariant(item) : QVariantMap();
                m_fileSystemDetails = haveFs ? toVariant(filesystem) : QVariantMap();
                emit detailsChanged();
                setFileOperations(operations);
            },
            Qt::QueuedConnection);
    }));
}

void FileBrowser::setFileOperations(const QVariantList& operations) {
    if (m_fileOperations == operations)
        return;
    m_fileOperations = operations;
    emit fileOperationsChanged();
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

// --- file tools ---------------------------------------------------------------

std::optional<core::OperationDescriptor> FileBrowser::findOperation(const QString& handler,
                                                                    const QString& operation) const {
    const auto h = m_handlers.find(handler.toStdString());
    if (!h)
        return std::nullopt;
    for (core::OperationDescriptor& op : h->operations()) {
        if (op.id == operation.toStdString())
            return std::move(op);
    }
    return std::nullopt;
}

QString FileBrowser::validateOperation(const QString& handler, const QString& operation,
                                       const QVariantMap& values) const {
    const auto op = findOperation(handler, operation);
    if (!op)
        return tr("Unknown file tool: %1").arg(operation);
    core::Parameters parameters = toParameters(*op, values, hostFolder());
    const core::Status st = core::validateParameters(*op, parameters);
    if (!st)
        return QString::fromStdString(st.message);
    if (core::rewritesSource(*op, parameters) && !has(core::Capability::Replace))
        return tr("This place is read-only: write to a new file instead.");
    return QString();
}

bool FileBrowser::rewritesSource(const QString& handler, const QString& operation, const QVariantMap& values) const {
    const auto op = findOperation(handler, operation);
    return op && core::rewritesSource(*op, toParameters(*op, values, hostFolder()));
}

QStringList FileBrowser::activeParameters(const QString& handler, const QString& operation,
                                          const QVariantMap& values) const {
    QStringList active;
    const auto op = findOperation(handler, operation);
    if (!op)
        return active;
    const core::Parameters parameters = toParameters(*op, values, hostFolder());
    for (const core::ParameterDescriptor& p : op->parameters) {
        if (core::isParameterActive(*op, parameters, p.id))
            active.append(QString::fromStdString(p.id));
    }
    return active;
}

void FileBrowser::runFileOperation(const QString& fileName, const QString& handlerId, const QString& operationId,
                                   const QVariantMap& values) {
    const auto rt = runtime();
    const int row = m_model->rowForName(fileName);
    if (!rt || row < 0 || m_model->entryAt(row).type != core::EntryType::File || !has(core::Capability::Extract))
        return;
    const auto handler = m_handlers.find(handlerId.toStdString());
    const auto op = findOperation(handlerId, operationId);
    if (!handler || !op) {
        setError(tr("Unknown file tool: %1").arg(operationId));
        return;
    }
    core::Parameters parameters = toParameters(*op, values, hostFolder());
    if (const core::Status st = core::validateParameters(*op, parameters); !st) {
        setError(QString::fromStdString(st.message));
        return;
    }
    const bool modifiesSource = core::rewritesSource(*op, parameters);
    // New files may land in the folder on show.
    const bool refresh = modifiesSource || core::writesOutputs(*op);
    if (modifiesSource && !has(core::Capability::Replace)) {
        setError(tr("%1 changes the file, but this place is read-only").arg(QString::fromStdString(op->name)));
        return;
    }

    const std::string path = core::joinPath(m_model->path().toStdString(), fileName.toStdString());
    const std::string id = operationId.toStdString();
    const QString title = tr("%1 %2").arg(QString::fromStdString(op->name), fileName);
    auto report = std::make_shared<std::string>();
    runJob(title, rt,
           [handler, id, path, parameters, report](core::FileSystem& fs, const JobContext& ctx) {
               core::HostOperationIo io(&fs, path);
               const core::ProgressFn progress = [&](const core::TransferProgress& p) {
                   ctx.progress(p.bytesTotal ? double(p.bytesDone) / double(p.bytesTotal) : -1.0,
                                QString::fromStdString(p.current));
                   return !ctx.cancelled();
               };
               auto result = core::runOperation(*handler, id, fs, path, parameters, io, progress);
               if (result)
                   *report = result.value();
               return result.status();
           },
           [this, title, name = op->name, fileName, refresh, report](core::Status st) {
               if (st && !report->empty())
                   emit reportReady(title, QString::fromStdString(*report));
               if (!st && !st.cancelled)
                   st.message = tr("%1 failed: %2").arg(QString::fromStdString(name), QString::fromStdString(st.message))
                                    .toStdString();
               if (refresh)
                   finishOperation(st, tr("%1: done").arg(title), fileName);
               else if (st && report->empty())
                   emit notice(tr("%1: done").arg(title));
               else if (!st && st.cancelled)
                   emit notice(tr("Cancelled"));
               else if (!st)
                   setError(QString::fromStdString(st.message));
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
