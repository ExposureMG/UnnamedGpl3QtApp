#include "gui/FileBrowser.hpp"

#include "core/PathUtil.hpp"

#include <QCoreApplication>

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
      m_mounts(new MountModel(this)) {
    connect(m_model, &FileSystemModel::countChanged, this, &FileBrowser::stateChanged);
}

bool FileBrowser::has(core::Capability cap) const {
    const core::FileSystem* fs = m_model->fileSystem();
    return fs && core::hasCapability(fs->capabilities(), cap);
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
    mount.fs = std::move(fs);
    m_mounts->add(std::move(mount));
    setError({});
    selectMount(m_mounts->count() - 1);
}

void FileBrowser::selectMount(int index) {
    if (index < 0 || index >= m_mounts->count())
        return;
    m_mounts->setCurrent(index);
    m_model->setFileSystem(m_mounts->at(index).fs);
    m_selectedName.clear();
    navigate(m_mounts->at(index).currentPath);
}

void FileBrowser::closeMount(int index) {
    if (index < 0 || index >= m_mounts->count())
        return;
    const bool wasCurrent = index == currentMount();
    m_mounts->remove(index);
    if (!wasCurrent) {
        emit stateChanged();
        return;
    }
    m_selectedName.clear();
    if (m_mounts->count() > 0) {
        selectMount(qMin(index, m_mounts->count() - 1));
    } else {
        m_model->setFileSystem(nullptr);
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

void FileBrowser::navigate(const QString& path) {
    const core::Status status = m_model->setPath(path);
    if (status) {
        m_mounts->at(currentMount()).currentPath = path;
        m_selectedName.clear();
    }
    setError(status ? QString() : QString::fromStdString(status.message));
    refreshDetails();
    emit selectionChanged();
    emit stateChanged();
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
    m_itemDetails.clear();
    m_fileSystemDetails.clear();

    if (const core::FileSystem* fs = m_model->fileSystem()) {
        core::Details d;
        const std::string target = hasSelection() ? selectedPath().toStdString() : m_model->path().toStdString();
        if (fs->describe(target, d))
            m_itemDetails = toVariant(d);
        if (fs->describeFileSystem(d))
            m_fileSystemDetails = toVariant(d);
    }
    emit detailsChanged();
}

// --- operations -------------------------------------------------------------

void FileBrowser::report(const core::Status& status, const QString& successMessage) {
    if (status) {
        setError({});
        if (!successMessage.isEmpty())
            emit notice(successMessage);
    } else {
        setError(QString::fromStdString(status.message));
    }
}

void FileBrowser::extractSelected(const QUrl& destinationFolder) {
    if (!canExtract())
        return;
    const auto dest = toHostPath(destinationFolder.toLocalFile()) / core::pathFromUtf8(m_selectedName.toStdString());
    report(m_model->fileSystem()->extract(selectedPath().toStdString(), dest),
           tr("Extracted %1").arg(m_selectedName));
}

void FileBrowser::injectFile(const QUrl& file) {
    if (!canInject())
        return;
    const core::Status status = m_model->fileSystem()->inject(m_model->path().toStdString(), toHostPath(file.toLocalFile()));
    report(status, tr("Added %1").arg(file.fileName()));
    if (status)
        refresh();
}

void FileBrowser::replaceSelected(const QUrl& file) {
    if (!canReplace())
        return;
    const core::Status status = m_model->fileSystem()->replace(selectedPath().toStdString(), toHostPath(file.toLocalFile()));
    report(status, tr("Replaced %1").arg(m_selectedName));
    if (status)
        refresh();
}

void FileBrowser::removeSelected() {
    if (!canRemove())
        return;
    const QString name = m_selectedName;
    const core::Status status = m_model->fileSystem()->remove(selectedPath().toStdString());
    report(status, tr("Deleted %1").arg(name));
    if (status) {
        m_selectedName.clear();
        refresh();
    }
}

void FileBrowser::refresh() {
    if (!isOpen())
        return;
    const core::Status status = m_model->setPath(m_model->path());
    if (!status)
        setError(QString::fromStdString(status.message));
    refreshDetails();
    emit selectionChanged();
    emit stateChanged();
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
