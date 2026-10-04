#include "gui/JobModel.hpp"

namespace unnamed::gui {

JobModel::JobModel(QObject* parent) : QAbstractListModel(parent) {}

int JobModel::rowOf(int id) const {
    for (size_t i = 0; i < m_jobs.size(); ++i) {
        if (m_jobs[i].id == id)
            return static_cast<int>(i);
    }
    return -1;
}

int JobModel::activeCount() const {
    int n = 0;
    for (const Job& j : m_jobs)
        n += j.state == Running;
    return n;
}

int JobModel::add(const QString& title, const void* owner, std::shared_ptr<std::atomic_bool> cancelFlag) {
    Job job;
    job.id = m_nextId++;
    job.title = title;
    job.owner = owner;
    job.cancelFlag = std::move(cancelFlag);
    // Newest first.
    beginInsertRows({}, 0, 0);
    m_jobs.insert(m_jobs.begin(), std::move(job));
    endInsertRows();
    emit activeCountChanged();
    emit countChanged();
    return m_jobs.front().id;
}

void JobModel::setProgress(int id, double fraction, const QString& message) {
    const int row = rowOf(id);
    if (row < 0 || m_jobs[row].state != Running)
        return;
    m_jobs[row].progress = fraction;
    m_jobs[row].message = message;
    emit dataChanged(index(row), index(row), {ProgressRole, MessageRole});
}

void JobModel::finish(int id, State state, const QString& message) {
    const int row = rowOf(id);
    if (row < 0)
        return;
    m_jobs[row].state = state;
    m_jobs[row].progress = state == Succeeded ? 1.0 : m_jobs[row].progress;
    m_jobs[row].message = message;
    emit dataChanged(index(row), index(row));
    emit activeCountChanged();
}

void JobModel::cancelAllFor(const void* owner) {
    for (Job& j : m_jobs) {
        if (j.owner == owner && j.state == Running && j.cancelFlag)
            j.cancelFlag->store(true);
    }
}

void JobModel::cancel(int row) {
    if (row >= 0 && row < count() && m_jobs[row].state == Running && m_jobs[row].cancelFlag) {
        m_jobs[row].cancelFlag->store(true);
        m_jobs[row].message = tr("Cancelling…");
        emit dataChanged(index(row), index(row), {MessageRole});
    }
}

void JobModel::clearFinished() {
    for (int row = count() - 1; row >= 0; --row) {
        if (m_jobs[row].state != Running) {
            beginRemoveRows({}, row, row);
            m_jobs.erase(m_jobs.begin() + row);
            endRemoveRows();
        }
    }
    emit countChanged();
}

int JobModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : count();
}

QVariant JobModel::data(const QModelIndex& index, int role) const {
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const Job& j = m_jobs[static_cast<size_t>(index.row())];
    switch (role) {
    case Qt::DisplayRole:
    case TitleRole: return j.title;
    case StateRole: return j.state;
    case ProgressRole: return j.progress;
    case MessageRole: return j.message;
    case ActiveRole: return j.state == Running;
    }
    return {};
}

QHash<int, QByteArray> JobModel::roleNames() const {
    return {
        {TitleRole, "title"},
        {StateRole, "state"},
        {ProgressRole, "progress"},
        {MessageRole, "message"},
        {ActiveRole, "active"},
    };
}

} // namespace unnamed::gui
