#include "gui/MountModel.hpp"

namespace unnamed::gui {

MountModel::MountModel(QObject* parent) : QAbstractListModel(parent) {}

int MountModel::indexOf(const QString& kind, const QString& hostPath) const {
    for (size_t i = 0; i < m_mounts.size(); ++i) {
        if (m_mounts[i].kind == kind && m_mounts[i].hostPath == hostPath)
            return static_cast<int>(i);
    }
    return -1;
}

void MountModel::setCurrent(int index) {
    if (m_current == index)
        return;
    const int old = m_current;
    m_current = index;
    if (old >= 0 && old < count())
        emit dataChanged(this->index(old), this->index(old), {IsCurrentRole});
    if (index >= 0 && index < count())
        emit dataChanged(this->index(index), this->index(index), {IsCurrentRole});
}

void MountModel::add(Mount mount) {
    beginInsertRows({}, count(), count());
    m_mounts.push_back(std::move(mount));
    endInsertRows();
}

void MountModel::remove(int index) {
    if (index < 0 || index >= count())
        return;
    beginRemoveRows({}, index, index);
    m_mounts.erase(m_mounts.begin() + index);
    endRemoveRows();
    if (m_current == index)
        m_current = -1;
    else if (m_current > index)
        --m_current;
}

int MountModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : count();
}

QVariant MountModel::data(const QModelIndex& index, int role) const {
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const Mount& m = at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole: return m.name;
    case KindRole: return m.kind;
    case SubtitleRole: return m.hostPath.isEmpty() ? m.kind : m.hostPath;
    case IsCurrentRole: return index.row() == m_current;
    }
    return {};
}

QHash<int, QByteArray> MountModel::roleNames() const {
    return {
        {NameRole, "name"},
        {KindRole, "kind"},
        {SubtitleRole, "subtitle"},
        {IsCurrentRole, "isCurrent"},
    };
}

} // namespace unnamed::gui
