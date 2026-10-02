#pragma once

#include "core/FileSystem.hpp"

#include <QAbstractListModel>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <vector>

namespace unnamed::gui {

// List model of one directory of a core::FileSystem, exposed to QML.
class FileSystemModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by FileBrowser")
public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        IsDirectoryRole,
        SizeTextRole,
    };

    explicit FileSystemModel(QObject* parent = nullptr);

    void setFileSystem(std::shared_ptr<core::FileSystem> fs);
    core::FileSystem* fileSystem() const { return m_fs.get(); }

    // Navigates to `path`; on failure the model keeps its previous contents.
    core::Status setPath(const QString& path);
    QString path() const { return m_path; }

    const core::Entry& entryAt(int row) const { return m_entries.at(static_cast<size_t>(row)); }

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    std::shared_ptr<core::FileSystem> m_fs;
    QString m_path = QStringLiteral("/");
    std::vector<core::Entry> m_entries;
};

} // namespace unnamed::gui
