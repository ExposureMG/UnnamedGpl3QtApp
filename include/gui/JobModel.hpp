#pragma once

#include <QAbstractListModel>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <memory>
#include <vector>

namespace unnamed::gui {

// Background operations (extract, add files, delete, ...) shown in the
// transfers popup: progress, status and cancel.
class JobModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by FileBrowser")
    Q_PROPERTY(int activeCount READ activeCount NOTIFY activeCountChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    enum Role { TitleRole = Qt::UserRole + 1, StateRole, ProgressRole, MessageRole, ActiveRole };
    enum State { Running, Succeeded, Failed, Cancelled };
    Q_ENUM(State)

    explicit JobModel(QObject* parent = nullptr);

    // Registers a running job and returns its id. `owner` identifies the
    // filesystem so its jobs can be cancelled when it is closed.
    int add(const QString& title, const void* owner, std::shared_ptr<std::atomic_bool> cancelFlag);
    void setProgress(int id, double fraction, const QString& message);
    void finish(int id, State state, const QString& message);
    void cancelAllFor(const void* owner);

    Q_INVOKABLE void cancel(int row);
    Q_INVOKABLE void clearFinished();

    int activeCount() const;
    int count() const { return static_cast<int>(m_jobs.size()); }

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void activeCountChanged();
    void countChanged();
    // The user cancelled the running job `id` of `owner` (cancel()).
    void cancelRequested(int id, const void* owner);

private:
    struct Job {
        int id = 0;
        QString title;
        State state = Running;
        double progress = -1; // <0: indeterminate
        QString message;
        const void* owner = nullptr;
        std::shared_ptr<std::atomic_bool> cancelFlag;
    };

    int rowOf(int id) const;

    std::vector<Job> m_jobs;
    int m_nextId = 1;
};

} // namespace unnamed::gui
