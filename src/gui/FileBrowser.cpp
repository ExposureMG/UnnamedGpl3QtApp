#include "gui/FileBrowser.hpp"

#include <QFileInfo>

namespace unnamed::gui {

FileBrowser::FileBrowser(QObject* parent)
    : QObject(parent),
      m_registry(core::FileSystemRegistry::withBuiltins()),
      m_model(new FileSystemModel(this)) {}

QString FileBrowser::rootName() const {
    if (m_rootPath.isEmpty())
        return {};
    const QString name = QFileInfo(m_rootPath).fileName();
    return name.isEmpty() ? m_rootPath : name;
}

QString FileBrowser::statusText() const {
    return isOpen() ? tr("%n item(s)", nullptr, m_model->rowCount()) : tr("Open a folder to begin");
}

void FileBrowser::openFolder(const QUrl& folder) {
    std::string error;
    auto fs = m_registry.open("Local", folder.toLocalFile().toStdString(), error);
    if (!fs) {
        setError(QString::fromStdString(error));
        return;
    }
    m_model->setFileSystem(std::move(fs));
    m_rootPath = folder.toLocalFile();
    setError({});
    emit stateChanged();
}

void FileBrowser::activate(int row) {
    if (row < 0 || row >= m_model->rowCount())
        return;
    const core::Entry& entry = m_model->entryAt(row);
    if (entry.type == core::EntryType::Directory)
        navigate(QString::fromStdString(core::joinPath(m_model->path().toStdString(), entry.name)));
}

void FileBrowser::goUp() {
    if (canGoUp())
        navigate(QString::fromStdString(core::parentPath(m_model->path().toStdString())));
}

void FileBrowser::navigate(const QString& path) {
    const core::Status status = m_model->setPath(path);
    setError(status ? QString() : QString::fromStdString(status.message));
    emit stateChanged();
}

void FileBrowser::setError(const QString& message) {
    if (m_errorMessage == message)
        return;
    m_errorMessage = message;
    emit errorMessageChanged();
}

} // namespace unnamed::gui
