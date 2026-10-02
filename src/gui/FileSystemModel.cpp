#include "gui/FileSystemModel.hpp"

#include <QLocale>

#include <algorithm>

namespace unnamed::gui {

FileSystemModel::FileSystemModel(QObject* parent) : QAbstractListModel(parent) {}

void FileSystemModel::setFileSystem(std::shared_ptr<core::FileSystem> fs) {
    beginResetModel();
    m_fs = std::move(fs);
    m_path = QStringLiteral("/");
    m_entries.clear();
    endResetModel();
    if (m_fs)
        setPath(m_path);
}

core::Status FileSystemModel::setPath(const QString& path) {
    if (!m_fs)
        return core::Status::failure("No filesystem open");

    std::vector<core::Entry> entries;
    const core::Status status = m_fs->list(path.toStdString(), entries);
    if (!status)
        return status;

    // Directories first, then case-insensitive by name.
    std::sort(entries.begin(), entries.end(), [](const core::Entry& a, const core::Entry& b) {
        if (a.type != b.type)
            return a.type == core::EntryType::Directory;
        return QString::compare(QString::fromStdString(a.name),
                                QString::fromStdString(b.name), Qt::CaseInsensitive) < 0;
    });

    beginResetModel();
    m_path = path;
    m_entries = std::move(entries);
    endResetModel();
    return status;
}

int FileSystemModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
}

QVariant FileSystemModel::data(const QModelIndex& index, int role) const {
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};

    const core::Entry& entry = entryAt(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return QString::fromStdString(entry.name);
    case IsDirectoryRole:
        return entry.type == core::EntryType::Directory;
    case SizeTextRole:
        return QLocale().formattedDataSize(static_cast<qint64>(entry.size));
    }
    return {};
}

QHash<int, QByteArray> FileSystemModel::roleNames() const {
    return {
        {NameRole, "name"},
        {IsDirectoryRole, "isDirectory"},
        {SizeTextRole, "sizeText"},
    };
}

} // namespace unnamed::gui
