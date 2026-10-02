#pragma once

#include "core/FileSystem.hpp"

#include <QAbstractListModel>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <vector>

namespace unnamed::gui {

// One open filesystem ("place") shown in the sidebar.
struct Mount {
    QString name;
    QString kind;     // registry kind: "Local", "Demo", later "FATX", ...
    QString hostPath; // folder/image on the host; empty for kinds without one
    QString currentPath = QStringLiteral("/");
    std::shared_ptr<core::FileSystem> fs;
};

class MountModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by FileBrowser")
public:
    enum Role { NameRole = Qt::UserRole + 1, KindRole, SubtitleRole, IsCurrentRole };

    explicit MountModel(QObject* parent = nullptr);

    int count() const { return static_cast<int>(m_mounts.size()); }
    Mount& at(int i) { return m_mounts.at(static_cast<size_t>(i)); }
    const Mount& at(int i) const { return m_mounts.at(static_cast<size_t>(i)); }
    int indexOf(const QString& kind, const QString& hostPath) const;

    int current() const { return m_current; }
    void setCurrent(int index);

    void add(Mount mount);
    void remove(int index);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    std::vector<Mount> m_mounts;
    int m_current = -1;
};

} // namespace unnamed::gui
